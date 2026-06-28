/*
 * Accelerometer driver — BMA-register-compatible 3-axis accel at I2C 0x4C.
 * Data registers 0x02..0x07 = X/Y/Z, 16-bit little-endian signed (LSB bit0 is a
 * new-data flag; harmless for angle computation). Accessed via the Zephyr I2C
 * API directly (no dedicated Zephyr driver for this clone part).
 */
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <errno.h>

#include "accel.h"

#define ACCEL_ADDR    0x4C
#define REG_XYZ_BASE  0x02

static const struct device *const i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));

static int wr(uint8_t reg, uint8_t val)
{
	return i2c_reg_write_byte(i2c, ACCEL_ADDR, reg, val);
}

int accel_init(void)
{
	if (!device_is_ready(i2c)) {
		return -ENODEV;
	}

	/*
	 * MiraMEMS DA-family (BMA-register-compatible) 3-axis accel. It powers up
	 * in suspend (data regs read 0); the stock FW configures it and so must we.
	 * Order matters: CONFIG(0x00) has a soft-reset bit, so reset FIRST, then set
	 * RANGE/ODR, and write MODE_BW(0x11) LAST so PWR_OFF=0 (normal mode) sticks.
	 * Register decode (DA datasheet): MODE_BW bit7=PWR_OFF(0=normal); 0x07 =
	 * normal + BW + autosleep. RANGE 0x43=±16g. ODR 0x05=31.25 Hz.
	 */
	/*
	 * EXACT stock-firmware init sequence (reverse-engineered from the stock FW
	 * disassembly @0x24fca, see HARDWARE.md). The stock FW never writes reg 0x00
	 * — writing 0x00 wedges this chip. Includes the active/motion interrupt
	 * config (0x20/0x21/0x28/0x1A) which we reuse for wake-on-motion later.
	 */
	static const uint8_t init_seq[][2] = {
		{0x09, 0x00}, {0x0F, 0x42}, {0x20, 0x01}, {0x21, 0x80},
		{0x28, 0x00}, {0x1A, 0x00}, {0x20, 0x01}, {0x21, 0x00},
		{0x21, 0x81}, {0x20, 0x00}, {0x20, 0x00}, {0x21, 0x00},
		{0x21, 0x80}, {0x10, 0x01}, {0x11, 0x07}, {0x15, 0x34},
		{0x0F, 0x42}, {0x10, 0x05},
	};
	int rc = 0;

	for (size_t i = 0; i < ARRAY_SIZE(init_seq); i++) {
		rc |= wr(init_seq[i][0], init_seq[i][1]);
	}
	k_msleep(20);
	printk("accel init wr rc=%d\n", rc);
	accel_debug_dump();
	return 0;
}

void accel_set_lowpower(bool low)
{
	/* Lower the output data rate in sleep to cut accel current; restore for
	 * active sampling. ODR reg 0x10: 0x00 = slowest (~1 Hz), 0x05 = 31.25 Hz. */
	wr(0x10, low ? 0x00 : 0x05);
}

void accel_debug_dump(void)
{
	uint8_t d[0x12] = {0};
	int rc = i2c_burst_read(i2c, ACCEL_ADDR, 0x00, d, sizeof(d));

	printk("acc rc=%d cfg00=%02x id01=%02x rng0F=%02x odr10=%02x mode11=%02x dat=%d,%d,%d\n",
	       rc, d[0], d[1], d[0x0F], d[0x10], d[0x11],
	       (int16_t)(d[2] | (d[3] << 8)),
	       (int16_t)(d[4] | (d[5] << 8)),
	       (int16_t)(d[6] | (d[7] << 8)));
}

int accel_read_xyz(int16_t *x, int16_t *y, int16_t *z)
{
	uint8_t d[6];
	int rc = i2c_burst_read(i2c, ACCEL_ADDR, REG_XYZ_BASE, d, sizeof(d));

	if (rc) {
		return rc;
	}
	*x = (int16_t)sys_get_le16(&d[0]);
	*y = (int16_t)sys_get_le16(&d[2]);
	*z = (int16_t)sys_get_le16(&d[4]);
	return 0;
}
