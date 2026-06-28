#ifndef CSC_H_
#define CSC_H_

#include <stdint.h>

/* CSC Measurement flags */
#define CSC_WHEEL_DATA_PRESENT (1u << 0)
#define CSC_CRANK_DATA_PRESENT (1u << 1)

/* Common CSC sensor locations */
#define CSC_LOC_LEFT_CRANK 0x05
#define CSC_LOC_REAR_WHEEL 0x0c

/* Set the value reported by the Sensor Location characteristic. */
void csc_set_location(uint8_t loc);

/*
 * Send a CSC Measurement notification to all subscribers.
 *   flags : CSC_WHEEL_DATA_PRESENT and/or CSC_CRANK_DATA_PRESENT
 *   cwr   : cumulative wheel revolutions (uint32)
 *   lwet  : last wheel event time, units 1/1024 s (uint16)
 *   ccr   : cumulative crank revolutions (uint16)
 *   lcet  : last crank event time, units 1/1024 s (uint16)
 */
void csc_notify(uint8_t flags, uint32_t cwr, uint16_t lwet, uint16_t ccr, uint16_t lcet);

#endif /* CSC_H_ */
