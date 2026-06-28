/*
 * Cadence/speed estimation from the rotating gravity vector (see cadence.h).
 * Software float (no FPU) — fine at ~100 Hz.
 *
 * - Angle is only trusted when the in-plane gravity component is a large fraction
 *   of total |g| (i.e. we are looking at the real rotation plane, not noise).
 * - Revolutions come from the net (signed) accumulated angle, so jitter at rest
 *   makes no phantom revolutions.
 * - Instantaneous RPM is computed over a 100 ms window (call cadence_tick_100ms),
 *   which averages per-sample jitter so a stationary sensor reads ~0.
 */
#include <math.h>
#include <zephyr/kernel.h>

#include "cadence.h"

#define TWO_PI 6.28318530718f
#define PI_F   3.14159265359f

#define INPLANE_FRAC 0.40f /* in-plane |g| must be >= 40% of total |g| */
#define RPM_DEADBAND 4.0f  /* RPM below this (per 100 ms window) -> 0 */
#define EMA_ALPHA    0.4f

static enum cadence_plane s_plane;
static bool s_have_prev;
static float s_prev_angle;
static float s_accum;          /* net signed angle, rad */
static float s_accum_prev_win; /* snapshot for windowed RPM */
static int32_t s_rev_count;    /* net revolutions from s_accum */
static float s_ema_rpm;
static uint16_t s_last_evt_1024;

void cadence_init(enum cadence_plane plane)
{
	s_plane = plane;
	s_have_prev = false;
	s_prev_angle = 0;
	s_accum = 0;
	s_accum_prev_win = 0;
	s_rev_count = 0;
	s_ema_rpm = 0;
	s_last_evt_1024 = 0;
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

void cadence_update(int16_t x, int16_t y, int16_t z, uint32_t dt_ms)
{
	float a, b;

	ARG_UNUSED(dt_ms);
	pick_plane(x, y, z, &a, &b);

	float total = sqrtf((float)x * x + (float)y * y + (float)z * z);
	float inplane = sqrtf(a * a + b * b);

	/* Not the rotation plane (or no gravity): don't update the angle. */
	if (total < 1.0f || inplane < INPLANE_FRAC * total) {
		return;
	}

	float angle = atan2f(b, a); /* -pi..pi */

	if (!s_have_prev) {
		s_prev_angle = angle;
		s_have_prev = true;
		return;
	}

	float d = angle - s_prev_angle;

	if (d > PI_F) {
		d -= TWO_PI;
	} else if (d < -PI_F) {
		d += TWO_PI;
	}
	s_prev_angle = angle;
	s_accum += d;

	int32_t newrev = (int32_t)(s_accum / TWO_PI);

	if (newrev != s_rev_count) {
		s_rev_count = newrev;
		uint32_t up = (uint32_t)k_uptime_get();

		s_last_evt_1024 = (uint16_t)(((uint64_t)up * 1024u) / 1000u);
	}
}

void cadence_resume(void)
{
	s_have_prev = false;
	s_accum_prev_win = s_accum;
	s_ema_rpm = 0.0f;
}

void cadence_tick_100ms(void)
{
	float d = s_accum - s_accum_prev_win;

	s_accum_prev_win = s_accum;

	/* SIGNED: sign carries rotation direction (forward +, reverse -). */
	float rpm = d / TWO_PI * 600.0f; /* revs in 0.1 s -> per min */

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
