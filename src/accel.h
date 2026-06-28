#ifndef ACCEL_H_
#define ACCEL_H_

#include <stdint.h>
#include <stdbool.h>

/* 3-axis accelerometer at I2C 0x4C (BMA-register compatible). See HARDWARE.md. */

int accel_init(void);

/* Read raw signed 16-bit X/Y/Z (regs 0x02..0x07, little-endian). Returns 0 on success. */
int accel_read_xyz(int16_t *x, int16_t *y, int16_t *z);

/* Lower (true) or restore (false) the accel output data rate for low-power sleep. */
void accel_set_lowpower(bool low);

/* Print key accel registers + data over RTT (debug). */
void accel_debug_dump(void);

#endif /* ACCEL_H_ */
