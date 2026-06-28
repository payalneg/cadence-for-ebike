#ifndef CADENCE_H_
#define CADENCE_H_

#include <stdint.h>
#include <stdbool.h>

/*
 * Cadence/speed from a 3-axis accelerometer: as the crank (or wheel) turns, the
 * gravity vector rotates in the plane perpendicular to the rotation axis. We
 * track that angle to count revolutions and estimate RPM. Which axis pair is the
 * rotation plane depends on mounting (crank vs wheel) — selectable to support the
 * battery-reinsert mode switch.
 */
enum cadence_plane {
	CAD_PLANE_XY,
	CAD_PLANE_YZ,
	CAD_PLANE_XZ,
};

void cadence_init(enum cadence_plane plane);

/* Feed a new accel sample taken dt_ms after the previous one. */
void cadence_update(int16_t x, int16_t y, int16_t z, uint32_t dt_ms);

/* Call every 100 ms to update the windowed RPM estimate. */
void cadence_tick_100ms(void);

/* Resume after a low-power sleep: keep cumulative revs but drop the stale
 * previous-angle so the first post-wake sample doesn't create a phantom jump. */
void cadence_resume(void);

uint16_t cadence_total_revs(void);           /* cumulative revolutions (unsigned, for CSC) */
uint16_t cadence_last_event_time_1024(void); /* time of last whole-rev crossing, 1/1024 s */
int16_t cadence_centi_rpm(void);             /* smoothed instantaneous RPM x100, SIGNED (direction) */
bool cadence_is_moving(void);                /* rotation seen in the last few seconds */

#endif /* CADENCE_H_ */
