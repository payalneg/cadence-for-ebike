/*
 * Custom vendor "RPM" service — live rotation speed as uint16 LE centi-RPM.
 * NOTIFY + READ. Standard CSC service is left untouched; this is additive.
 */
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/byteorder.h>

#include "rpm_svc.h"

#define RPM_SVC_UUID  BT_UUID_128_ENCODE(0xcad00001, 0xeb1c, 0x4f1e, 0x9b2a, 0x6f1c0de0cade)
#define RPM_CHAR_UUID BT_UUID_128_ENCODE(0xcad00002, 0xeb1c, 0x4f1e, 0x9b2a, 0x6f1c0de0cade)

static const struct bt_uuid_128 rpm_svc_uuid = BT_UUID_INIT_128(RPM_SVC_UUID);
static const struct bt_uuid_128 rpm_chr_uuid = BT_UUID_INIT_128(RPM_CHAR_UUID);

static int16_t cur_centi_rpm; /* signed: sign = direction */
static bool notify_enabled;

static void ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	notify_enabled = (value == BT_GATT_CCC_NOTIFY);
}

static ssize_t read_rpm(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, uint16_t len, uint16_t offset)
{
	uint8_t le[2];

	sys_put_le16((uint16_t)cur_centi_rpm, le);
	return bt_gatt_attr_read(conn, attr, buf, len, offset, le, sizeof(le));
}

BT_GATT_SERVICE_DEFINE(rpm_svc,
	BT_GATT_PRIMARY_SERVICE(&rpm_svc_uuid),
	BT_GATT_CHARACTERISTIC(&rpm_chr_uuid.uuid,
			       BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, read_rpm, NULL, &cur_centi_rpm),
	BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

void rpm_svc_set_centi_rpm(int16_t centi_rpm)
{
	uint8_t le[2];

	cur_centi_rpm = centi_rpm;
	if (!notify_enabled) {
		return;
	}
	sys_put_le16((uint16_t)centi_rpm, le);
	bt_gatt_notify(NULL, &rpm_svc.attrs[1], le, sizeof(le));
}
