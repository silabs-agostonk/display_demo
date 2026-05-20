#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/types.h>

#include "app_input.h"
#include "app_types.h"
#include "ble_hid_app.h"
#include "hid_mouse.h"

LOG_MODULE_REGISTER(bt_connection, LOG_LEVEL_INF);

/* Filter for near devices */
#define RSSI_CONNECT_THRESHOLD              (-30)
#define DIRECT_CONNECT_FALLBACK_DELAY_MS    2000

#define BLE_EVENT_QUEUE_LEN                 16
#define BLE_EVENT_THREAD_STACK_SIZE         3072
#define BLE_EVENT_THREAD_PRIORITY           7

enum ble_event_type {
	BLE_EVENT_START,
	BLE_EVENT_BT_READY,
	BLE_EVENT_DEVICE_FOUND,
	BLE_EVENT_CONNECTED,
	BLE_EVENT_DISCONNECTED,
	BLE_EVENT_SECURITY_CHANGED,
	BLE_EVENT_SET_PAIRING_MODE,
	BLE_EVENT_TOGGLE_PAIRING_MODE,
	BLE_EVENT_PAIRING_COMPLETE,
};

struct ble_event {
	enum ble_event_type type;

	union {
		struct {
			int err;
		} bt_ready;

		struct {
			bt_addr_le_t addr;
			int8_t rssi;
			uint8_t adv_type;
		} device_found;

		struct {
			struct bt_conn *conn;
			uint8_t conn_err;
		} connected;

		struct {
			struct bt_conn *conn;
			uint8_t reason;
		} disconnected;

		struct {
			struct bt_conn *conn;
			bt_security_t level;
			enum bt_security_err err;
		} security_changed;

		struct {
			bool enable;
		} set_pairing_mode;

		struct {
			struct bt_conn *conn;
			bool bonded;
		} pairing_complete;
	};
};

K_MSGQ_DEFINE(ble_event_queue,
	      sizeof(struct ble_event),
	      BLE_EVENT_QUEUE_LEN,
	      sizeof(void *));

K_THREAD_STACK_DEFINE(ble_event_thread_stack, BLE_EVENT_THREAD_STACK_SIZE);
static struct k_thread ble_event_thread_data;
static bool ble_event_thread_started;

static void ble_event_thread(void *p1, void *p2, void *p3);
static void ble_event_handle(const struct ble_event *event);
static int ble_event_post(struct ble_event *event);
static void ble_event_cleanup(struct ble_event *event);

static void start_scan(void);
static void connect_to_addr(const bt_addr_le_t *addr);
static void try_connect_preferred_or_scan(void);
static void start_hids_discovery(struct bt_conn *conn);
static void refresh_bond_state(void);
static void set_pairing_mode_internal(bool enable);

static struct bt_conn *default_conn;
static struct bt_uuid_16 discover_uuid = BT_UUID_INIT_16(0);
static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_subscribe_params subscribe_params;

static uint16_t hids_start_handle;
static uint16_t hids_end_handle;
static uint16_t hids_ctrl_point_handle;
static uint16_t hids_protocol_mode_handle;

static const bt_security_t target_sec = BT_SECURITY_L2;

static bool discovery_started;
static atomic_t scanning;
static atomic_t connecting;
static atomic_t discovering;

static bool have_bond;
static bool pairing_allowed;

static struct k_work_delayable pairing_mode_timeout_work;

static bt_addr_le_t preferred_addr;
static bool have_preferred;
static struct k_work_delayable connect_fallback_scan_work;

static const struct bt_conn_le_create_param create_param_coded =
	BT_CONN_LE_CREATE_PARAM_INIT(BT_CONN_LE_OPT_CODED,
				     BT_GAP_SCAN_FAST_INTERVAL,
				     BT_GAP_SCAN_FAST_WINDOW);

static const struct bt_conn_le_create_param create_param_1m =
	BT_CONN_LE_CREATE_PARAM_INIT(BT_CONN_LE_OPT_NONE,
				     BT_GAP_SCAN_FAST_INTERVAL,
				     BT_GAP_SCAN_FAST_WINDOW);

uint64_t total_rx_count;

/* This value is exposed to test code */
extern volatile size_t pointer_x;
extern volatile size_t pointer_y;

int bt_connection_hids_ctrl_point_write(uint8_t val);
void bt_connection_enable_pairing_mode(bool enable);

static void ble_event_cleanup(struct ble_event *event)
{
	switch (event->type) {
	case BLE_EVENT_CONNECTED:
		if (event->connected.conn) {
			bt_conn_unref(event->connected.conn);
			event->connected.conn = NULL;
		}
		break;

	case BLE_EVENT_DISCONNECTED:
		if (event->disconnected.conn) {
			bt_conn_unref(event->disconnected.conn);
			event->disconnected.conn = NULL;
		}
		break;

	case BLE_EVENT_SECURITY_CHANGED:
		if (event->security_changed.conn) {
			bt_conn_unref(event->security_changed.conn);
			event->security_changed.conn = NULL;
		}
		break;

	case BLE_EVENT_PAIRING_COMPLETE:
		if (event->pairing_complete.conn) {
			bt_conn_unref(event->pairing_complete.conn);
			event->pairing_complete.conn = NULL;
		}
		break;

	default:
		break;
	}
}

static int ble_event_post(struct ble_event *event)
{
	int err;

	err = k_msgq_put(&ble_event_queue, event, K_NO_WAIT);
	if (err) {
		printk("BLE event queue full, dropping event %d\n", event->type);
		ble_event_cleanup(event);
		return err;
	}

	return 0;
}

static void ble_event_thread_start_once(void)
{
	if (ble_event_thread_started) {
		return;
	}

	ble_event_thread_started = true;

	k_thread_create(&ble_event_thread_data,
			ble_event_thread_stack,
			K_THREAD_STACK_SIZEOF(ble_event_thread_stack),
			ble_event_thread,
			NULL,
			NULL,
			NULL,
			BLE_EVENT_THREAD_PRIORITY,
			0,
			K_NO_WAIT);

	k_thread_name_set(&ble_event_thread_data, "ble_hid_app");
}

static uint8_t notify_func(struct bt_conn *conn,
			   struct bt_gatt_subscribe_params *params,
			   const void *data,
			   uint16_t length)
{
	int err;
	struct mouse_data_element mouse_data_new_element;

	if (!data) {
		printk("[UNSUBSCRIBED]\n");
		params->value_handle = 0U;
		return BT_GATT_ITER_STOP;
	}

	printk("Value: 0x%04X Value handle: 0x%04X\n",
	       params->value,
	       params->value_handle);
	printk("[NOTIFICATION] data %p length %u\n", data, length);

	for (uint16_t i = 0; i < length; i++) {
		printk("%02X ", ((const uint8_t *)data)[i]);
	}
	printk("\n");

	err = hid_mouse_decode_logitech_m196((const uint8_t *)data,
					     length,
					     &mouse_data_new_element);
	if (err) {
		printk("HID mouse decode failed: %d\n", err);
		return BT_GATT_ITER_CONTINUE;
	}

	printk("Button: %d %d | Diff x:%d y:%d\n",
	       mouse_data_new_element.left_button,
	       mouse_data_new_element.right_button,
	       mouse_data_new_element.dx,
	       mouse_data_new_element.dy);

	err = app_input_submit_mouse(&mouse_data_new_element);
	if (err) {
		printk("Mouse input submit failed: %d\n", err);
		return BT_GATT_ITER_CONTINUE;
	}

	total_rx_count++;

	return BT_GATT_ITER_CONTINUE;
}

static uint8_t discover_func(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     struct bt_gatt_discover_params *params)
{
	int err;

	/* Discovery finished */
	if (!attr) {
		printk("Discover complete\n");
		discovery_started = false;
		atomic_set(&discovering, 0);

		/* Stop the discover procedure cleanly */
		memset(params, 0, sizeof(*params));
		return BT_GATT_ITER_STOP;
	}

	printk("[ATTRIBUTE] handle %u\n", attr->handle);

	/* 1) Primary service discovery: HID service 0x1812 */
	if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_HIDS)) {
		const struct bt_gatt_service_val *svc = attr->user_data;

		hids_start_handle = attr->handle;
		hids_end_handle = svc->end_handle;

		/* Next: discover HID Report characteristic(s) within HID service */
		memcpy(&discover_uuid, BT_UUID_HIDS_REPORT, sizeof(discover_uuid));
		discover_params.uuid = &discover_uuid.uuid;
		discover_params.start_handle = hids_start_handle + 1;
		discover_params.end_handle = hids_end_handle;
		discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			printk("Discover REPORT char failed (err %d)\n", err);
		}

		return BT_GATT_ITER_STOP;
	}

	/* 2) Characteristic discovery: HID Report characteristic 0x2A4D */
	if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_HIDS_REPORT)) {
		subscribe_params.value_handle = bt_gatt_attr_value_handle(attr);

		/* Next: find CCC descriptor (0x2902) in HID service range */
		memcpy(&discover_uuid, BT_UUID_GATT_CCC, sizeof(discover_uuid));
		discover_params.uuid = &discover_uuid.uuid;
		discover_params.start_handle = attr->handle + 1;
		discover_params.end_handle = hids_end_handle;
		discover_params.type = BT_GATT_DISCOVER_DESCRIPTOR;

		err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			printk("Discover CCC descriptor failed (err %d)\n", err);
		}

		return BT_GATT_ITER_STOP;
	}

	/* 3) Descriptor discovery: CCC (0x2902) */
	if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_GATT_CCC)) {
		/* Iterate until we actually hit the CCC attribute */
		if (bt_uuid_cmp(attr->uuid, BT_UUID_GATT_CCC)) {
			return BT_GATT_ITER_CONTINUE;
		}

		subscribe_params.notify = notify_func;
		subscribe_params.value = BT_GATT_CCC_NOTIFY;
		subscribe_params.ccc_handle = attr->handle;

		err = bt_gatt_subscribe(conn, &subscribe_params);
		if (err && err != -EALREADY) {
			printk("Subscribe failed (err %d)\n", err);
		} else {
			printk("[SUBSCRIBED]\n");
		}

		/* Next: Protocol Mode characteristic */
		memcpy(&discover_uuid, BT_UUID_HIDS_PROTOCOL_MODE,
		       sizeof(discover_uuid));
		discover_params.uuid = &discover_uuid.uuid;
		discover_params.start_handle = hids_start_handle + 1;
		discover_params.end_handle = hids_end_handle;
		discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			printk("Discover Protocol Mode failed (err %d)\n", err);
		}

		return BT_GATT_ITER_STOP;
	}

	/* 4) Characteristic discovery: Protocol Mode 0x2A4E */
	if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_HIDS_PROTOCOL_MODE)) {
		uint8_t val = 0x01; /* Report Protocol */

		hids_protocol_mode_handle = bt_gatt_attr_value_handle(attr);

		err = bt_gatt_write_without_response(conn,
						     hids_protocol_mode_handle,
						     &val,
						     sizeof(val),
						     false);
		if (err) {
			printk("Write Protocol Mode failed (err %d)\n", err);
		}

		/* Next: Control Point characteristic */
		memcpy(&discover_uuid, BT_UUID_HIDS_CTRL_POINT,
		       sizeof(discover_uuid));
		discover_params.uuid = &discover_uuid.uuid;
		discover_params.start_handle = hids_start_handle + 1;
		discover_params.end_handle = hids_end_handle;
		discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			printk("Discover Control Point failed (err %d)\n", err);
		}

		return BT_GATT_ITER_STOP;
	}

	/* 5) Characteristic discovery: Control Point 0x2A4C */
	if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_HIDS_CTRL_POINT)) {
		hids_ctrl_point_handle = bt_gatt_attr_value_handle(attr);

		/* Optional: send wake */
		(void)bt_connection_hids_ctrl_point_write(0x01);

		return BT_GATT_ITER_STOP;
	}

	return BT_GATT_ITER_CONTINUE;
}

static void bond_pick_first(const struct bt_bond_info *info, void *user_data)
{
	ARG_UNUSED(user_data);

	/* With CONFIG_BT_MAX_PAIRED=1, there will be only one. */
	memcpy(&preferred_addr, &info->addr, sizeof(preferred_addr));
	have_preferred = true;
}

static void refresh_bond_state(void)
{
	have_bond = false;
	have_preferred = false;

	bt_foreach_bond(BT_ID_DEFAULT, bond_pick_first, NULL);

	if (have_preferred) {
		char s[BT_ADDR_LE_STR_LEN];

		have_bond = true;
		bt_addr_le_to_str(&preferred_addr, s, sizeof(s));
		printk("Preferred bonded device: %s\n", s);
	} else {
		printk("No bonded peers\n");
	}
}

static void connect_fallback_scan_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	if (default_conn || atomic_get(&connecting)) {
		/* Connection attempt still ongoing or connected; don't scan. */
		return;
	}

	start_scan();
}

static void connect_to_addr(const bt_addr_le_t *addr)
{
	int err;

	if (default_conn || atomic_get(&connecting)) {
		return;
	}

	if (atomic_get(&scanning)) {
		err = bt_le_scan_stop();
		if (!err) {
			atomic_set(&scanning, 0);
		}
	}

	if (!atomic_cas(&connecting, 0, 1)) {
		return;
	}

	printk("Creating connection with Coded PHY support\n");

	err = bt_conn_le_create(addr,
				&create_param_coded,
				BT_LE_CONN_PARAM_DEFAULT,
				&default_conn);
	if (err) {
		printk("Create coded PHY connection failed (err %d)\n", err);
		printk("Creating non-Coded PHY connection\n");

		err = bt_conn_le_create(addr,
					&create_param_1m,
					BT_LE_CONN_PARAM_DEFAULT,
					&default_conn);
		if (err) {
			printk("Create connection failed (err %d)\n", err);
			atomic_set(&connecting, 0);
			default_conn = NULL;
			start_scan();
			return;
		}
	}
}

static void try_connect_preferred_or_scan(void)
{
	if (!have_preferred) {
		start_scan();
		return;
	}

	/*
	 * Try direct connect. With RPA devices this may time out; we schedule
	 * scan fallback.
	 */
	connect_to_addr(&preferred_addr);

	/*
	 * If the address is stale (RPA), this connect may never complete.
	 * Scan fallback handles that case.
	 */
	k_work_schedule(&connect_fallback_scan_work,
			K_MSEC(DIRECT_CONNECT_FALLBACK_DELAY_MS));
}

static void handle_device_found(const struct ble_event *event)
{
	char dev[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(&event->device_found.addr, dev, sizeof(dev));

	printk("[DEVICE]: %s (type %u) RSSI %d\n",
	       dev,
	       event->device_found.adv_type,
	       event->device_found.rssi);

	/* If no bond, only connect when user enabled pairing mode. */
	if (!have_bond && !pairing_allowed) {
		return;
	}

	if (default_conn || atomic_get(&connecting)) {
		return;
	}

	/* Only connectable advertisements. */
	if (!(event->device_found.adv_type == BT_GAP_ADV_TYPE_ADV_IND ||
	      event->device_found.adv_type == BT_GAP_ADV_TYPE_ADV_DIRECT_IND ||
	      event->device_found.adv_type == BT_GAP_ADV_TYPE_EXT_ADV)) {
		return;
	}

	if (event->device_found.rssi < RSSI_CONNECT_THRESHOLD) {
		return;
	}

	/*
	 * Connect to the first near connectable device.
	 * If it is not our bonded device, pairing will be cancelled.
	 */
	connect_to_addr(&event->device_found.addr);
}

static void device_found(const bt_addr_le_t *addr,
			 int8_t rssi,
			 uint8_t type,
			 struct net_buf_simple *ad)
{
	struct ble_event event = {
		.type = BLE_EVENT_DEVICE_FOUND,
	};

	ARG_UNUSED(ad);

	event.device_found.addr = *addr;
	event.device_found.rssi = rssi;
	event.device_found.adv_type = type;

	(void)ble_event_post(&event);
}

static void start_scan(void)
{
	int err;

	if (default_conn || atomic_get(&connecting)) {
		return;
	}

	if (atomic_get(&scanning)) {
		return;
	}

	struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_ACTIVE,
		.options = BT_LE_SCAN_OPT_CODED,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};

	err = bt_le_scan_start(&scan_param, device_found);
	if (err) {
		printk("Scanning with Coded PHY support failed (err %d)\n", err);
		printk("Scanning without Coded PHY\n");

		scan_param.options &= ~BT_LE_SCAN_OPT_CODED;

		err = bt_le_scan_start(&scan_param, device_found);
		if (err) {
			printk("Scanning failed to start (err %d)\n", err);
			return;
		}
	}

	atomic_set(&scanning, 1);
	printk("Scanning successfully started\n");
}

static void handle_connected(struct bt_conn *conn, uint8_t conn_err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (conn_err) {
		printk("Failed to connect to %s (%u)\n", addr, conn_err);
		atomic_set(&connecting, 0);

		if (default_conn) {
			bt_conn_unref(default_conn);
			default_conn = NULL;
		}

		start_scan();
		return;
	}

	printk("Connected: %s\n", addr);

	atomic_set(&connecting, 0);
	k_work_cancel_delayable(&connect_fallback_scan_work);

	if (conn != default_conn) {
		return;
	}

	total_rx_count = 0U;

	/* This should re-encrypt via existing bond for the mouse. */
	err = bt_conn_set_security(conn, target_sec);
	if (err) {
		printk("bt_conn_set_security(%u) failed (err %d)\n",
		       target_sec,
		       err);
	}
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	struct ble_event event = {
		.type = BLE_EVENT_CONNECTED,
	};

	event.connected.conn = bt_conn_ref(conn);
	event.connected.conn_err = conn_err;

	(void)ble_event_post(&event);
}

static void start_hids_discovery(struct bt_conn *conn)
{
	int err;

	if (!atomic_cas(&discovering, 0, 1)) {
		return;
	}

	discovery_started = true;

	memset(&discover_params, 0, sizeof(discover_params));
	memset(&subscribe_params, 0, sizeof(subscribe_params));

	memcpy(&discover_uuid, BT_UUID_HIDS, sizeof(discover_uuid));

	discover_params.uuid = &discover_uuid.uuid;
	discover_params.func = discover_func;
	discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	discover_params.type = BT_GATT_DISCOVER_PRIMARY;

	err = bt_gatt_discover(conn, &discover_params);
	if (err) {
		printk("Discover failed (err %d)\n", err);
		discovery_started = false;
		atomic_set(&discovering, 0);
	}
}

static void handle_disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Disconnected: %s, reason 0x%02x %s\n",
	       addr,
	       reason,
	       bt_hci_err_to_str(reason));

	/* 1) If we had a subscription, unsubscribe first. */
	if (subscribe_params.value_handle) {
		int e = bt_gatt_unsubscribe(conn, &subscribe_params);

		printk("Unsubscribe: %d\n", e);
	}

	/* 2) Cancel any outstanding GATT procedures that use discover_params. */
	if (atomic_get(&discovering)) {
		bt_gatt_cancel(conn, &discover_params);
		atomic_set(&discovering, 0);
		discovery_started = false;
	}

	/* 3) Drop conn ref. */
	if (default_conn == conn) {
		bt_conn_unref(default_conn);
		default_conn = NULL;
	}

	atomic_set(&connecting, 0);
	atomic_set(&scanning, 0);

	/* 4) Clear only your own state/handles. */
	hids_start_handle = 0;
	hids_end_handle = 0;
	hids_ctrl_point_handle = 0;
	hids_protocol_mode_handle = 0;

	/* 5) Reset subscribe params fields you own. */
	subscribe_params.value_handle = 0;
	subscribe_params.ccc_handle = 0;

	refresh_bond_state();
	start_scan();
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	struct ble_event event = {
		.type = BLE_EVENT_DISCONNECTED,
	};

	event.disconnected.conn = bt_conn_ref(conn);
	event.disconnected.reason = reason;

	(void)ble_event_post(&event);
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	printk("Pairing cancelled: %s\n", addr);
}

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	printk("Passkey for %s: %06u\n", addr, passkey);
}

static void auth_passkey_confirm(struct bt_conn *conn, unsigned int passkey)
{
	printk("Numeric comparison: %06u (auto-accept passkey confirm)\n",
	       passkey);
	bt_conn_auth_passkey_confirm(conn);
}

static void auth_passkey_entry(struct bt_conn *conn)
{
	/* Replace with real input (UART/CLI/button). */
	unsigned int pk = 123456;

	printk("Entering passkey: %06u\n", pk);
	bt_conn_auth_passkey_entry(conn, pk);
}

static void auth_pairing_confirm(struct bt_conn *conn)
{
	if (!pairing_allowed) {
		printk("Pairing not allowed; cancelling\n");
		bt_conn_auth_cancel(conn);
		return;
	}

	printk("Pairing confirm requested (allowed)\n");
	bt_conn_auth_pairing_confirm(conn);
}

static struct bt_conn_auth_cb auth_cb = {
	.passkey_display = auth_passkey_display,
	.passkey_confirm = auth_passkey_confirm,
	.passkey_entry = auth_passkey_entry,
	.pairing_confirm = auth_pairing_confirm,
	.cancel = auth_cancel,
};

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	struct ble_event event = {
		.type = BLE_EVENT_PAIRING_COMPLETE,
	};

	event.pairing_complete.conn = bt_conn_ref(conn);
	event.pairing_complete.bonded = bonded;

	(void)ble_event_post(&event);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing failed: %s, reason=%d\n", addr, reason);
	bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

static struct bt_conn_auth_info_cb auth_info_cb = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

static void handle_pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing complete: %s bonded=%u\n", addr, bonded);

	if (bonded) {
		set_pairing_mode_internal(false);
		k_work_cancel_delayable(&pairing_mode_timeout_work);
		refresh_bond_state();
	}
}

static void handle_security_changed(struct bt_conn *conn,
				    bt_security_t level,
				    enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		printk("Security failed: %s level %u err %d\n",
		       addr,
		       level,
		       err);
		return;
	}

	printk("Security changed: %s level %u\n", addr, level);

	if (conn == default_conn && !discovery_started && level >= target_sec) {
		start_hids_discovery(conn);
	}
}

static void security_changed(struct bt_conn *conn,
			     bt_security_t level,
			     enum bt_security_err err)
{
	struct ble_event event = {
		.type = BLE_EVENT_SECURITY_CHANGED,
	};

	event.security_changed.conn = bt_conn_ref(conn);
	event.security_changed.level = level;
	event.security_changed.err = err;

	(void)ble_event_post(&event);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

int bt_connection_hids_ctrl_point_write(uint8_t val)
{
	if (!default_conn) {
		return -ENOTCONN;
	}

	if (!hids_ctrl_point_handle) {
		return -EINVAL;
	}

	return bt_gatt_write_without_response(default_conn,
					      hids_ctrl_point_handle,
					      &val,
					      sizeof(val),
					      false);
}

static void pairing_mode_timeout_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	bt_connection_enable_pairing_mode(false);
}

static void bt_enable_ready_cb(int e)
{
	struct ble_event event = {
		.type = BLE_EVENT_BT_READY,
	};

	event.bt_ready.err = e;

	(void)ble_event_post(&event);
}

static void handle_bt_ready(int e)
{
	int err;

	if (e) {
		printk("bt_enable_ready_cb err %d\n", e);
		return;
	}

	err = settings_load();
	if (err) {
		printk("settings_load failed (err %d)\n", err);
	}

	err = bt_conn_auth_cb_register(&auth_cb);
	printk("auth_cb_register: %d\n", err);

	err = bt_conn_auth_info_cb_register(&auth_info_cb);
	printk("auth_info_cb_register: %d\n", err);

	k_work_init_delayable(&connect_fallback_scan_work,
			      connect_fallback_scan_fn);
	k_work_init_delayable(&pairing_mode_timeout_work,
			      pairing_mode_timeout_fn);

	pairing_allowed = false;

	refresh_bond_state();

	printk("Bluetooth initialized\n");

	if (have_bond) {
		try_connect_preferred_or_scan();
	} else {
		printk("No bond. Press Button0 to enable pairing mode.\n");
	}
}

static void set_pairing_mode_internal(bool enable)
{
	pairing_allowed = enable;

	printk("Pairing mode: %s\n", enable ? "ENABLED" : "DISABLED");

	if (enable) {
		start_scan();
	}
}

void bt_connection_enable_pairing_mode(bool enable)
{
	struct ble_event event = {
		.type = BLE_EVENT_SET_PAIRING_MODE,
	};

	event.set_pairing_mode.enable = enable;

	(void)ble_event_post(&event);
}

void bt_connection_enable(void)
{
	struct ble_event event = {
		.type = BLE_EVENT_START,
	};

	ble_event_thread_start_once();

	(void)ble_event_post(&event);
}

void ble_hid_app_start(void)
{
	bt_connection_enable();
}

static void handle_start(void)
{
	int err;

	err = bt_enable(bt_enable_ready_cb);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}
}

static void handle_toggle_pairing_mode(void)
{
	bool new_state = !pairing_allowed;

	set_pairing_mode_internal(new_state);

	/* If enabling, auto-disable after 60 seconds. */
	if (new_state) {
		k_work_schedule(&pairing_mode_timeout_work, K_SECONDS(60));
	} else {
		k_work_cancel_delayable(&pairing_mode_timeout_work);
	}
}

static void ble_event_handle(const struct ble_event *event)
{
	switch (event->type) {
	case BLE_EVENT_START:
		handle_start();
		break;

	case BLE_EVENT_BT_READY:
		handle_bt_ready(event->bt_ready.err);
		break;

	case BLE_EVENT_DEVICE_FOUND:
		handle_device_found(event);
		break;

	case BLE_EVENT_CONNECTED:
		handle_connected(event->connected.conn,
				 event->connected.conn_err);
		break;

	case BLE_EVENT_DISCONNECTED:
		handle_disconnected(event->disconnected.conn,
				    event->disconnected.reason);
		break;

	case BLE_EVENT_SECURITY_CHANGED:
		handle_security_changed(event->security_changed.conn,
					event->security_changed.level,
					event->security_changed.err);
		break;

	case BLE_EVENT_SET_PAIRING_MODE:
		set_pairing_mode_internal(event->set_pairing_mode.enable);
		break;

	case BLE_EVENT_TOGGLE_PAIRING_MODE:
		handle_toggle_pairing_mode();
		break;

	case BLE_EVENT_PAIRING_COMPLETE:
		handle_pairing_complete(event->pairing_complete.conn,
					event->pairing_complete.bonded);
		break;

	default:
		printk("Unhandled BLE event %d\n", event->type);
		break;
	}
}

static void ble_event_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		struct ble_event event;

		k_msgq_get(&ble_event_queue, &event, K_FOREVER);
		ble_event_handle(&event);
		ble_event_cleanup(&event);
	}
}

static void button_input_cb(struct input_event *evt, void *user_data)
{
	struct ble_event event = {
		.type = BLE_EVENT_TOGGLE_PAIRING_MODE,
	};

	ARG_UNUSED(user_data);

	if (evt->sync == 0) {
		return;
	}

	if (evt->code != 2) {
		return;
	}

	printk("Button %d %s at %" PRIu32 "\n",
	       evt->code,
	       evt->value ? "pressed" : "released",
	       k_cycle_get_32());

	if (evt->value) {
		(void)ble_event_post(&event);
	}
}

INPUT_CALLBACK_DEFINE(NULL, button_input_cb, NULL);