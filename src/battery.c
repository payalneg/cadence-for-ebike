/*
 * Battery measurement via the nRF52 SAADC internal VDD input (no external pin).
 * Maps supply voltage to a coin-cell percentage for the BLE Battery Service.
 */
#include <zephyr/drivers/adc.h>
#include <errno.h>

#include "battery.h"

#define BATT_FULL_MV 3000 /* CR2032 ~full */
#define BATT_EMPTY_MV 2000 /* CR2032 ~empty */

static const struct adc_dt_spec vbatt = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

int battery_init(void)
{
	if (!adc_is_ready_dt(&vbatt)) {
		return -ENODEV;
	}
	return adc_channel_setup_dt(&vbatt);
}

int battery_millivolts(void)
{
	int16_t raw = 0;
	struct adc_sequence seq = {
		.buffer = &raw,
		.buffer_size = sizeof(raw),
	};

	if (adc_sequence_init_dt(&vbatt, &seq) != 0) {
		return -EIO;
	}
	if (adc_read_dt(&vbatt, &seq) != 0) {
		return -EIO;
	}

	int32_t mv = raw;

	if (adc_raw_to_millivolts_dt(&vbatt, &mv) != 0) {
		return -EIO;
	}
	return mv;
}

uint8_t battery_percent(void)
{
	int mv = battery_millivolts();

	if (mv < 0) {
		return 0;
	}
	if (mv >= BATT_FULL_MV) {
		return 100;
	}
	if (mv <= BATT_EMPTY_MV) {
		return 0;
	}
	return (uint8_t)(((mv - BATT_EMPTY_MV) * 100) / (BATT_FULL_MV - BATT_EMPTY_MV));
}
