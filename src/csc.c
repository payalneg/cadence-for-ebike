/*
 * Cycling Speed and Cadence (CSC) service — UUID 0x1816.
 * Adapted from the Zephyr peripheral_csc sample (Apache-2.0, (c) 2016 Intel).
 * Service attribute order is kept identical to the sample so the notify handle
 * csc_svc.attrs[1] (Measurement) and indicate handle attrs[8] (SC Control Point)
 * remain correct.
 */
#include <zephyr/types.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include "csc.h"

/* CSC sensor locations supported */
#define CSC_LOC_OTHER       0x00
#define CSC_LOC_FRONT_WHEEL 0x04
#define CSC_LOC_RIGHT_CRANK 0x06

/* CSC Feature bits */
#define CSC_FEAT_WHEEL_REV     BIT(0)
#define CSC_FEAT_CRANK_REV     BIT(1)
#define CSC_FEAT_MULTI_SENSORS BIT(2)
#define CSC_FEATURE (CSC_FEAT_WHEEL_REV | CSC_FEAT_CRANK_REV | CSC_FEAT_MULTI_SENSORS)

/* SC Control Point opcodes */
#define SC_CP_OP_SET_CWR      0x01
#define SC_CP_OP_UPDATE_LOC   0x03
#define SC_CP_OP_REQ_SUPP_LOC 0x04
#define SC_CP_OP_RESPONSE     0x10

/* SC Control Point response values */
#define SC_CP_RSP_SUCCESS     0x01
#define SC_CP_RSP_OP_NOT_SUPP 0x02
#define SC_CP_RSP_INVAL_PARAM 0x03

/* CSC application error codes */
#define CSC_ERR_CCC_CONFIG 0x81

static uint8_t supported_locations[] = {
	CSC_LOC_OTHER, CSC_LOC_FRONT_WHEEL, CSC_LOC_REAR_WHEEL,
	CSC_LOC_LEFT_CRANK, CSC_LOC_RIGHT_CRANK,
};
static uint8_t sensor_location = CSC_LOC_LEFT_CRANK;
static uint32_t c_wheel_revs; /* settable via SET_CWR */
static bool ctrl_point_configured;

static void ctrl_point_ind(struct bt_conn *conn, uint8_t req_op, uint8_t status,
			   const void *data, uint16_t data_len);

static void ctrl_point_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ctrl_point_configured = value == BT_GATT_CCC_INDICATE;
}

static ssize_t read_location(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			     void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
				 sizeof(sensor_location));
}

static ssize_t read_csc_feature(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				void *buf, uint16_t len, uint16_t offset)
{
	uint16_t feature = CSC_FEATURE;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &feature, sizeof(feature));
}

struct write_sc_ctrl_point_req {
	uint8_t op;
	union {
		uint32_t cwr;
		uint8_t location;
	};
} __packed;

static ssize_t write_ctrl_point(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	const struct write_sc_ctrl_point_req *req = buf;
	uint8_t status;

	if (!ctrl_point_configured) {
		return BT_GATT_ERR(CSC_ERR_CCC_CONFIG);
	}
	if (!len) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	switch (req->op) {
	case SC_CP_OP_SET_CWR:
		if (len != sizeof(req->op) + sizeof(req->cwr)) {
			status = SC_CP_RSP_INVAL_PARAM;
			break;
		}
		c_wheel_revs = sys_le32_to_cpu(req->cwr);
		status = SC_CP_RSP_SUCCESS;
		break;
	case SC_CP_OP_UPDATE_LOC:
		if (len != sizeof(req->op) + sizeof(req->location)) {
			status = SC_CP_RSP_INVAL_PARAM;
			break;
		}
		if (req->location == sensor_location) {
			status = SC_CP_RSP_SUCCESS;
			break;
		}
		status = SC_CP_RSP_INVAL_PARAM;
		for (int i = 0; i < ARRAY_SIZE(supported_locations); i++) {
			if (supported_locations[i] == req->location) {
				sensor_location = req->location;
				status = SC_CP_RSP_SUCCESS;
				break;
			}
		}
		break;
	case SC_CP_OP_REQ_SUPP_LOC:
		if (len != sizeof(req->op)) {
			status = SC_CP_RSP_INVAL_PARAM;
			break;
		}
		ctrl_point_ind(conn, req->op, SC_CP_RSP_SUCCESS, supported_locations,
			       sizeof(supported_locations));
		return len;
	default:
		status = SC_CP_RSP_OP_NOT_SUPP;
	}

	ctrl_point_ind(conn, req->op, status, NULL, 0);
	return len;
}

/* Service layout MUST stay in this order (see attrs[1]/attrs[8] below). */
BT_GATT_SERVICE_DEFINE(csc_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_CSC),
	BT_GATT_CHARACTERISTIC(BT_UUID_CSC_MEASUREMENT, BT_GATT_CHRC_NOTIFY,
			       0x00, NULL, NULL, NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(BT_UUID_SENSOR_LOCATION, BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, read_location, NULL, &sensor_location),
	BT_GATT_CHARACTERISTIC(BT_UUID_CSC_FEATURE, BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, read_csc_feature, NULL, NULL),
	BT_GATT_CHARACTERISTIC(BT_UUID_SC_CONTROL_POINT,
			       BT_GATT_CHRC_WRITE | BT_GATT_CHRC_INDICATE,
			       BT_GATT_PERM_WRITE, NULL, write_ctrl_point, &sensor_location),
	BT_GATT_CCC(ctrl_point_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

struct sc_ctrl_point_ind {
	uint8_t op;
	uint8_t req_op;
	uint8_t status;
	uint8_t data[];
} __packed;

static void ctrl_point_ind(struct bt_conn *conn, uint8_t req_op, uint8_t status,
			   const void *data, uint16_t data_len)
{
	uint8_t buf[sizeof(struct sc_ctrl_point_ind) + sizeof(supported_locations)];
	struct sc_ctrl_point_ind *ind = (void *)buf;

	ind->op = SC_CP_OP_RESPONSE;
	ind->req_op = req_op;
	ind->status = status;
	if (data && data_len) {
		memcpy(ind->data, data, data_len);
	}
	bt_gatt_notify(conn, &csc_svc.attrs[8], buf, sizeof(*ind) + data_len);
}

void csc_set_location(uint8_t loc)
{
	sensor_location = loc;
}

void csc_notify(uint8_t flags, uint32_t cwr, uint16_t lwet, uint16_t ccr, uint16_t lcet)
{
	uint8_t buf[1 + 4 + 2 + 2 + 2];
	uint16_t len = 0;

	buf[len++] = flags;
	if (flags & CSC_WHEEL_DATA_PRESENT) {
		sys_put_le32(cwr, &buf[len]);
		len += 4;
		sys_put_le16(lwet, &buf[len]);
		len += 2;
	}
	if (flags & CSC_CRANK_DATA_PRESENT) {
		sys_put_le16(ccr, &buf[len]);
		len += 2;
		sys_put_le16(lcet, &buf[len]);
		len += 2;
	}
	bt_gatt_notify(NULL, &csc_svc.attrs[1], buf, len);
}
