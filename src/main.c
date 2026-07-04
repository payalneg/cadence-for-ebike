/*
 * BK6LS cadence/speed sensor — custom firmware (nRF52810).
 *
 * Standard CSC service (apps work) + custom 128-bit RPM service (live centi-RPM
 * every 100 ms). Cadence/speed from the rotating gravity vector (accel @0x4C).
 *
 * Power: adaptive. ACTIVE = advertising + 100 Hz sampling. When disconnected and
 * no rotation for IDLE_TIMEOUT, drop to SLEEP: advertising off, 1 s low-power
 * poll; any accelerometer change wakes back to ACTIVE. (Deeper System OFF +
 * hardware motion-INT and accel low-power mode come once the accel's INT/mode
 * registers are pinned down.)
 *
 * NOTE: for a real battery-current measurement, disconnect J-Link — an attached
 * debugger keeps the debug power domain on and inflates current by ~mA.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/services/bas.h>

#include "csc.h"
#include "accel.h"
#include "cadence.h"
#include "rpm_svc.h"
#include "battery.h"

static const struct gpio_dt_spec led_blue = GPIO_DT_SPEC_GET(DT_NODELABEL(led_blue), gpios);
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_NODELABEL(led_red), gpios);

#define SAMPLE_MS       10    /* 100 Hz accel sampling when ACTIVE */
#define RPM_MS          100   /* custom RPM notify period */
#define CSC_MS          1000
#define IDLE_TIMEOUT_MS 20000 /* no motion + disconnected -> SLEEP */
#define SLEEP_POLL_MS   5000  /* low-power poll period in sleep */
#define BATT_MS         30000 /* battery level refresh period */
#define WAKE_DELTA      12    /* accel LSB change that counts as motion */

static volatile bool s_connected;
static bool s_advertising;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
		      BT_UUID_16_ENCODE(BT_UUID_CSC_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void adv_start(void)
{
	if (s_advertising) {
		return;
	}
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (err) {
		printk("adv start failed (%d)\n", err);
		return;
	}
	s_advertising = true;
}

static void adv_stop(void)
{
	if (!s_advertising) {
		return;
	}
	bt_le_adv_stop();
	s_advertising = false;
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed (0x%02x)\n", err);
		return;
	}
	s_connected = true;
	s_advertising = false; /* adv auto-stops on connect */
	printk("Connected\n");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	s_connected = false;
	printk("Disconnected (0x%02x)\n", reason);
	adv_start();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static void leds_init(void)
{
	if (gpio_is_ready_dt(&led_blue)) {
		gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT_INACTIVE);
	}
	if (gpio_is_ready_dt(&led_red)) {
		gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
	}
}

/* Blocking LED pulse — used at the (infrequent) wake/sleep transitions. */
static void led_pulse(const struct gpio_dt_spec *led, int ms)
{
	gpio_pin_set_dt(led, 1);
	k_msleep(ms);
	gpio_pin_set_dt(led, 0);
}

static inline int16_t adiff(int16_t a, int16_t b)
{
	int d = (int)a - (int)b;

	return d < 0 ? -d : d;
}

int main(void)
{
	int err;

	printk("\n=== BK6LS cadence firmware ===\n");

	leds_init();
	led_pulse(&led_blue, 1000); /* power-on = waking up -> blue 1 s */

	if (accel_init()) {
		printk("WARN: accel not ready\n");
	}
	cadence_init(CAD_PLANE_XY); /* crank default; mode switch comes later */

	err = bt_enable(NULL);
	if (err) {
		printk("bt_enable failed (%d)\n", err);
		return 0;
	}
	csc_set_location(CSC_LOC_LEFT_CRANK);
	adv_start();
	printk("Advertising as '%s'\n", CONFIG_BT_DEVICE_NAME);

	if (battery_init() == 0) {
		bt_bas_set_battery_level(battery_percent());
		printk("battery: %u%% (%d mV)\n", battery_percent(), battery_millivolts());
	} else {
		printk("WARN: battery ADC not ready\n");
	}

	int64_t t_prev = k_uptime_get();
	int64_t t_rpm = t_prev + RPM_MS;
	int64_t t_csc = t_prev + CSC_MS;
	int64_t t_batt = t_prev + BATT_MS;
	int64_t t_motion = t_prev;
	int16_t sx = 0, sy = 0, sz = 0; /* sleep baseline */
	bool sleeping = false;

	while (1) {
		int16_t x = 0, y = 0, z = 0;

		if (!sleeping) {
			int64_t now = k_uptime_get();
			uint32_t dt = (uint32_t)(now - t_prev);

			t_prev = now;

			/* Self-heal advertising: restarting adv straight from the
			 * disconnected callback can fail with -ENOMEM (the conn object
			 * isn't freed yet), which would leave us silent until sleep.
			 * Retry here until it takes. adv_start() no-ops if already on. */
			if (!s_connected && !s_advertising) {
				adv_start();
			}

			if (accel_read_xyz(&x, &y, &z) == 0) {
				cadence_update(x, y, z, dt);
			}

			if (now >= t_rpm) {
				t_rpm += RPM_MS;
				cadence_tick_100ms();
				rpm_svc_set_centi_rpm(cadence_centi_rpm());
			}
			if (now >= t_csc) {
				t_csc += CSC_MS;
				uint16_t revs = cadence_total_revs();
				int16_t crpm = cadence_centi_rpm(); /* signed centi-RPM */

				csc_notify(CSC_CRANK_DATA_PRESENT, 0, 0, revs,
					   cadence_last_event_time_1024());
				printk("crpm=%d revs=%u xyz=%d,%d,%d\n",
				       crpm, revs, x, y, z);
			}

			if (now >= t_batt) {
				t_batt += BATT_MS;
				bt_bas_set_battery_level(battery_percent());
			}

			if (cadence_is_moving()) {
				t_motion = now;
			}
			/* Enter SLEEP only when nobody is connected and we've been
			 * still for a while. */
			if (!s_connected && (now - t_motion) > IDLE_TIMEOUT_MS) {
				printk("idle -> sleep\n");
				adv_stop();
				led_pulse(&led_red, 1000); /* falling asleep -> red 1 s */
				/* keep accel sampling (autosleep already low-power) so motion
				 * stays detectable — do NOT freeze ODR here. */
				accel_read_xyz(&sx, &sy, &sz);
				sleeping = true;
			}
			k_msleep(SAMPLE_MS);
		} else {
			/* Low-power poll: CPU deep-idles, then check for motion. */
			k_msleep(SLEEP_POLL_MS);
			int rc = accel_read_xyz(&x, &y, &z);

			printk("sleep poll rc=%d xyz=%d,%d,%d base=%d,%d,%d\n",
			       rc, x, y, z, sx, sy, sz);
			if (rc == 0 &&
			    (adiff(x, sx) > WAKE_DELTA || adiff(y, sy) > WAKE_DELTA ||
			     adiff(z, sz) > WAKE_DELTA)) {
				printk("motion -> wake\n");
				led_pulse(&led_blue, 1000); /* waking up -> blue 1 s */
				sleeping = false;
				cadence_resume();
				adv_start();
				int64_t now = k_uptime_get();

				t_prev = now;
				t_rpm = now + RPM_MS;
				t_csc = now + CSC_MS;
				t_motion = now;
			} else {
				sx = x; sy = y; sz = z;
			}
		}
	}
	return 0;
}
