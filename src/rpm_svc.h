#ifndef RPM_SVC_H_
#define RPM_SVC_H_

#include <stdint.h>

/*
 * Custom vendor service exposing live rotation speed.
 * Characteristic value = SIGNED int16 little-endian centi-RPM (RPM x100), so the
 * sign indicates rotation direction (forward +, reverse -). NOTIFY+READ, pushed
 * every 100 ms. Range +/-327.67 RPM. UUIDs (128-bit):
 *   service: cad00001-eb1c-4f1e-9b2a-6f1c0de0cade
 *   char:    cad00002-eb1c-4f1e-9b2a-6f1c0de0cade
 */
void rpm_svc_set_centi_rpm(int16_t centi_rpm);

#endif /* RPM_SVC_H_ */
