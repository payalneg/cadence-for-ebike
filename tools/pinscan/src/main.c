/*
 * pinscan firmware #1 — hardware bring-up for the BK6LS nRF52810 cadence sensor.
 * Pinout was extracted from the stock FW's live registers (see HARDWARE.md).
 *
 * Confirmed so far: I2C SCL=P0.30, SDA=P0.28; a sensor ACKs at 0x4C (3-axis
 * accelerometer territory). This build:
 *   1) Re-confirms the I2C bus (both orderings) and dumps regs 0x00..0x3F of the
 *      device at 0x4C so the exact part can be fingerprinted.
 *   2) "Blink-count" LED ID: candidate output pin at list-index i blinks (i+1)
 *      times with a long gap; count blinks per color via the USB microscope.
 * All over SEGGER RTT (J-Link); no UART/peripheral drivers used.
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

static const struct device *const gpio = DEVICE_DT_GET(DT_NODELABEL(gpio0));

static const uint8_t out_pins[] = { 4, 5, 6, 9, 10, 12, 16, 18 };
static const uint8_t in_pins[] = { 15, 25 };
#define I2C_PIN_A 28
#define I2C_PIN_B 30
#define IMU_ADDR  0x4C

/* ===================== software open-drain I2C ===================== */
static uint8_t PIN_SCL, PIN_SDA;

static inline void dly(void) { k_busy_wait(5); } /* ~100 kHz */
static inline void scl_hi(void) { gpio_pin_configure(gpio, PIN_SCL, GPIO_INPUT | GPIO_PULL_UP); }
static inline void scl_lo(void) { gpio_pin_configure(gpio, PIN_SCL, GPIO_OUTPUT); gpio_pin_set_raw(gpio, PIN_SCL, 0); }
static inline void sda_hi(void) { gpio_pin_configure(gpio, PIN_SDA, GPIO_INPUT | GPIO_PULL_UP); }
static inline void sda_lo(void) { gpio_pin_configure(gpio, PIN_SDA, GPIO_OUTPUT); gpio_pin_set_raw(gpio, PIN_SDA, 0); }
static inline int sda_rd(void) { return gpio_pin_get_raw(gpio, PIN_SDA); }

static void i2c_start(void) { sda_hi(); scl_hi(); dly(); sda_lo(); dly(); scl_lo(); dly(); }
static void i2c_stop(void) { sda_lo(); dly(); scl_hi(); dly(); sda_hi(); dly(); }

static int i2c_wr(uint8_t b) /* 0 = ACK, 1 = NACK */
{
	for (int i = 0; i < 8; i++) {
		if (b & 0x80) { sda_hi(); } else { sda_lo(); }
		b <<= 1;
		dly(); scl_hi(); dly(); scl_lo(); dly();
	}
	sda_hi();
	dly(); scl_hi(); dly();
	int ack = sda_rd();
	scl_lo(); dly();
	return ack;
}

static uint8_t i2c_rd(int send_ack)
{
	uint8_t b = 0;
	sda_hi();
	for (int i = 0; i < 8; i++) {
		dly(); scl_hi(); dly();
		b = (b << 1) | (sda_rd() & 1);
		scl_lo();
	}
	if (send_ack) { sda_lo(); } else { sda_hi(); }
	dly(); scl_hi(); dly(); scl_lo(); sda_hi(); dly();
	return b;
}

static bool i2c_ping(uint8_t a7)
{
	i2c_start();
	int ack = i2c_wr((a7 << 1) | 0);
	i2c_stop();
	return ack == 0;
}

static int i2c_rdreg(uint8_t a7, uint8_t reg, uint8_t *val)
{
	i2c_start();
	if (i2c_wr((a7 << 1) | 0)) { i2c_stop(); return -1; }
	if (i2c_wr(reg)) { i2c_stop(); return -1; }
	i2c_start();
	if (i2c_wr((a7 << 1) | 1)) { i2c_stop(); return -1; }
	*val = i2c_rd(0);
	i2c_stop();
	return 0;
}

static void bus(uint8_t scl, uint8_t sda)
{
	PIN_SCL = scl; PIN_SDA = sda;
	sda_hi(); scl_hi();
	k_msleep(2);
}

static void scan_bus(uint8_t scl, uint8_t sda)
{
	bus(scl, sda);
	printk("\n[I2C] scan SCL=P0.%02u SDA=P0.%02u :", scl, sda);
	int found = 0;
	for (uint8_t a = 0x08; a <= 0x77; a++) {
		if (i2c_ping(a)) { printk(" 0x%02X", a); found++; }
	}
	printk(found ? "\n" : "  (none)\n");
}

static void dump_regs(uint8_t a7)
{
	bus(I2C_PIN_B, I2C_PIN_A); /* known-good: SCL=P0.30, SDA=P0.28 */
	printk("\n[I2C] reg dump 0x%02X (0x00..0x3F):\n", a7);
	for (uint8_t base = 0; base < 0x40; base += 16) {
		printk("  %02X:", base);
		for (uint8_t o = 0; o < 16; o++) {
			uint8_t v = 0;
			if (i2c_rdreg(a7, base + o, &v)) { printk(" --"); }
			else { printk(" %02X", v); }
		}
		printk("\n");
	}
}

/* ===================== LED identification ===================== */
#define ON_MS 220
#define OFF_MS 220
#define GAP_MS 2200

static void led_off(uint8_t p) { gpio_pin_configure(gpio, p, GPIO_INPUT); } /* hi-Z = off */

int main(void)
{
	printk("\n\n=== PINSCAN #1 : nRF52810 cadence sensor bring-up ===\n");
	printk("Build: " __DATE__ " " __TIME__ "\n");
	if (!device_is_ready(gpio)) {
		printk("ERROR: gpio0 not ready\n");
		return 0;
	}

	for (size_t i = 0; i < ARRAY_SIZE(out_pins); i++) { led_off(out_pins[i]); }
	for (size_t i = 0; i < ARRAY_SIZE(in_pins); i++) { gpio_pin_configure(gpio, in_pins[i], GPIO_INPUT); }

	uint32_t round = 0;
	while (1) {
		scan_bus(I2C_PIN_A, I2C_PIN_B);
		scan_bus(I2C_PIN_B, I2C_PIN_A);
		dump_regs(IMU_ADDR);
		led_off(I2C_PIN_A); led_off(I2C_PIN_B);

		printk("\n--------- LED blink-count sweep, round %u ---------\n", ++round);
		printk("(count blinks of each color; index N -> N blinks)\n");
		for (size_t i = 0; i < ARRAY_SIZE(out_pins); i++) {
			uint8_t p = out_pins[i];
			int n = (int)i + 1;
			printk("index %d  ->  P0.%02u : %d blink(s)\n", n, p, n);
			gpio_pin_configure(gpio, p, GPIO_OUTPUT);
			for (int b = 0; b < n; b++) {
				gpio_pin_set_raw(gpio, p, 1); k_msleep(ON_MS);
				gpio_pin_set_raw(gpio, p, 0); k_msleep(OFF_MS);
			}
			led_off(p);
			k_msleep(GAP_MS);
		}
		for (size_t i = 0; i < ARRAY_SIZE(in_pins); i++) {
			printk("INT candidate P0.%02u level = %d\n",
			       in_pins[i], gpio_pin_get_raw(gpio, in_pins[i]));
		}
	}
	return 0;
}
