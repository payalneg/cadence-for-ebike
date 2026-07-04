/*
 * Cadence/speed estimation from the rotating gravity vector (see cadence.h).
 * Software float (no FPU) — fine at ~100 Hz.
 *
 * Pipeline per accel sample:
 *   1. Low-pass the raw accel vector (ACC_LPF_ALPHA) to kill high-freq noise.
 *   2. Only trust the angle when the in-plane |g| is a large fraction of |g|
 *      total (i.e. we're really looking at the rotation plane, not noise).
 *   3. Feed the measured angle into an alpha-beta tracker — the steady-state
 *      (constant-gain) form of a constant-velocity Kalman filter. It carries
 *      two states: the crank angle and its angular velocity omega. omega IS the
 *      cadence, estimated directly instead of by differencing a noisy angle over
 *      a window — that differencing was what made the old RPM jump 20<->70 at a
 *      steady pedal. The gains follow the Benedict-Bordner criterion
 *      (beta = alpha^2/(2-alpha)) for a critically-damped response, and dt is
 *      taken from the real sample spacing so gaps (gated-out samples) are OK.
 *
 * Revolutions come from the (signed) angle rolling over, so rest jitter makes no
 * phantom revs, and a stopped crank drives omega -> 0 on its own.
 */
#include <math.h>
#include <zephyr/kernel.h>

#include "cadence.h"

#define TWO_PI 6.28318530718f
#define PI_F   3.14159265359f

#define INPLANE_FRAC  0.40f  /* in-plane |g| must be >= 40% of total |g| */
#define RPM_DEADBAND  4.0f   /* |RPM| below this -> 0 */
#define EMA_ALPHA     0.50f  /* light final smoothing of the reported RPM */
#define ACC_LPF_ALPHA 0.35f  /* low-pass on raw accel to reduce atan2 jitter */

/* alpha-beta (steady-state Kalman, constant-velocity) tracker gains.
 * alpha smooths the angle, beta the velocity; smaller = smoother but laggier.
 * beta is derived from alpha (Benedict-Bordner) for a critically-damped step. */
#define AB_ALPHA 0.08f
#define AB_BETA  (AB_ALPHA * AB_ALPHA / (2.0f - AB_ALPHA)) /* ~0.00333 */

/* If valid angle samples are farther apart than this, the constant-velocity
 * prediction is stale — re-seed instead of trusting a huge innovation. */
#define MAX_DT_S 0.5f

static enum cadence_plane s_plane;
static bool s_have_prev;
static float s_theta;          /* tracked crank angle, wrapped to (-pi, pi] */
static float s_omega;          /* tracked angular velocity, rad/s (signed) */
static uint32_t s_pending_dt_ms; /* elapsed time since last valid angle update */
static int32_t s_rev_count;    /* net revolutions (signed; sign = direction) */
static float s_ema_rpm;        /* reported RPM (signed) */
static uint16_t s_last_evt_1024;

/* Low-pass on the raw accel vector (see ACC_LPF_ALPHA). */
static bool s_lpf_have;
static float s_fx, s_fy, s_fz;

void cadence_init(enum cadence_plane plane)
{
	s_plane = plane;
	s_have_prev = false;
	s_theta = 0;
	s_omega = 0;
	s_pending_dt_ms = 0;
	s_rev_count = 0;
	s_ema_rpm = 0;
	s_last_evt_1024 = 0;
	s_lpf_have = false;
	s_fx = s_fy = s_fz = 0;
}

static void pick_plane(int16_t x, int16_t y, int16_t z, float *a, float *b)
{
	switch (s_plane) {
	case CAD_PLANE_YZ:
		*a = (float)y; *b = (float)z; break;
	case CAD_PLANE_XZ:
		*a = (float)x; *b = (float)z; break;
	case CAD_PLANE_XY:
	default:
		*a = (float)x; *b = (float)y; break;
	}
}

/* Wrap an angle delta into (-pi, pi]. */
static float wrap_pi(float d)
{
	while (d > PI_F) {
		d -= TWO_PI;
	}
	while (d < -PI_F) {
		d += TWO_PI;
	}
	return d;
}

void cadence_update(int16_t x, int16_t y, int16_t z, uint32_t dt_ms)
{
	float a, b;

	/* 1. Low-pass the raw vector first: at 100 Hz this removes the
	 * high-frequency accel noise that would otherwise feed the tracker. */
	if (!s_lpf_have) {
		s_fx = (float)x;
		s_fy = (float)y;
		s_fz = (float)z;
		s_lpf_have = true;
	} else {
		s_fx += ACC_LPF_ALPHA * ((float)x - s_fx);
		s_fy += ACC_LPF_ALPHA * ((float)y - s_fy);
		s_fz += ACC_LPF_ALPHA * ((float)z - s_fz);
	}

	int16_t fx = (int16_t)s_fx, fy = (int16_t)s_fy, fz = (int16_t)s_fz;

	pick_plane(fx, fy, fz, &a, &b);

	float total = sqrtf(s_fx * s_fx + s_fy * s_fy + s_fz * s_fz);
	float inplane = sqrtf(a * a + b * b);

	/* Accumulate elapsed time even for samples we're about to gate out, so the
	 * tracker's dt reflects the real spacing between trusted angles. */
	s_pending_dt_ms += dt_ms;

	/* 2. Not the rotation plane (or no gravity): don't update the angle. omega
	 * is retained and keeps coasting the prediction. */
	if (total < 1.0f || inplane < INPLANE_FRAC * total) {
		return;
	}

	float angle = atan2f(b, a); /* -pi..pi */

	if (!s_have_prev) {
		s_theta = angle;
		s_omega = 0.0f;
		s_have_prev = true;
		s_pending_dt_ms = 0;
		return;
	}

	float dt = (float)s_pending_dt_ms * 0.001f;

	s_pending_dt_ms = 0;
	if (dt <= 0.0f) {
		dt = 0.01f; /* guard against a zero/negative interval */
	}
	if (dt > MAX_DT_S) {
		/* Prediction is stale after a long gap — re-seed, don't lurch. */
		s_theta = angle;
		s_omega = 0.0f;
		return;
	}

	/* 3. alpha-beta update. Predict, then correct with the wrapped innovation. */
	float theta_pred = s_theta + s_omega * dt;
	float innov = wrap_pi(angle - theta_pred);

	s_theta = theta_pred + AB_ALPHA * innov;
	s_omega = s_omega + (AB_BETA / dt) * innov;

	/* Roll the tracked angle back into (-pi, pi], counting whole revolutions as
	 * it crosses. Net (signed) rev count -> direction preserved. */
	int32_t rev_delta = 0;

	while (s_theta > PI_F) {
		s_theta -= TWO_PI;
		rev_delta++;
	}
	while (s_theta < -PI_F) {
		s_theta += TWO_PI;
		rev_delta--;
	}
	if (rev_delta) {
		s_rev_count += rev_delta;
		uint32_t up = (uint32_t)k_uptime_get();

		s_last_evt_1024 = (uint16_t)(((uint64_t)up * 1024u) / 1000u);
	}
}

void cadence_resume(void)
{
	/* Keep cumulative revs; drop the stale angle/velocity so the first post-wake
	 * sample re-seeds the tracker instead of producing a phantom jump. */
	s_have_prev = false;
	s_lpf_have = false;
	s_omega = 0.0f;
	s_pending_dt_ms = 0;
	s_ema_rpm = 0.0f;
}

void cadence_tick_100ms(void)
{
	/* omega is already a smooth velocity estimate; convert to RPM (signed) and
	 * apply a deadband + a light EMA just for display steadiness. */
	float rpm = s_omega * (60.0f / TWO_PI);

	if (fabsf(rpm) < RPM_DEADBAND) {
		rpm = 0.0f;
	}
	s_ema_rpm += EMA_ALPHA * (rpm - s_ema_rpm);
}

uint16_t cadence_total_revs(void)
{
	int32_t r = s_rev_count < 0 ? -s_rev_count : s_rev_count;

	return (uint16_t)r;
}

uint16_t cadence_last_event_time_1024(void)
{
	return s_last_evt_1024;
}

int16_t cadence_centi_rpm(void)
{
	float c = s_ema_rpm * 100.0f; /* signed centi-RPM */

	if (c > 32767.0f) {
		c = 32767.0f;
	}
	if (c < -32768.0f) {
		c = -32768.0f;
	}
	return (int16_t)c;
}

bool cadence_is_moving(void)
{
	return (s_ema_rpm > 5.0f) || (s_ema_rpm < -5.0f); /* sustained rotation, either way */
}
