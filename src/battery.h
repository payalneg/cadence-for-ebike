#ifndef BATTERY_H_
#define BATTERY_H_

#include <stdint.h>

/* Battery measured via the nRF52 SAADC internal VDD input (no external pin). */

int battery_init(void);

/* Supply (battery) voltage in millivolts, or <0 on error. */
int battery_millivolts(void);

/* Battery level 0..100 % (coin-cell curve: ~3.0 V full, ~2.0 V empty). */
uint8_t battery_percent(void);

#endif /* BATTERY_H_ */
