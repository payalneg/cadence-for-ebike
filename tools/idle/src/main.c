/* Idle firmware — does nothing, so the J-Link debugger owns the GPIO registers.
 * Used for LED/pin identification by driving pins via nrfjprog --memwr. */
#include <zephyr/kernel.h>

int main(void)
{
	while (1) {
		k_sleep(K_SECONDS(3600));
	}
	return 0;
}
