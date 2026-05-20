

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_connection, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include <inttypes.h>
#include <zephyr/input/input.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/atomic.h>

#include <zephyr/settings/settings.h>

#include "app_types.h"
#include "ble_hid_app.h"
#include "hid_mouse.h"



static void start_scan(void);

static struct bt_conn *default_conn;

static struct bt_uuid_16 discover_uuid = BT_UUID_INIT_16(0);
static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_subscribe_params subscribe_params;

static uint16_t hids_start_handle;
static uint16_t hids_end_handle;
static uint16_t hids_ctrl_point_handle;
static uint16_t hids_protocol_mode_handle;

int bt_connection_hids_ctrl_point_write(uint8_t val);
void bt_connection_enable_pairing_mode(bool enable);

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

/* Filter for near devices */
#define RSSI_CONNECT_THRESHOLD (-30)
#define DIRECT_CONNECT_FALLBACK_DELAY_MS  2000

uint64_t total_rx_count; /* This value is exposed to test code */

extern volatile size_t pointer_x;
extern volatile size_t pointer_y;

extern struct k_msgq mouse_data_queue;

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

	/* Passing data into queue */
	while (k_msgq_put(&mouse_data_queue,
			  &mouse_data_new_element,
			  K_NO_WAIT) != 0) {
		/* Message queue is full: purge old data and try again. */
		k_msgq_purge(&mouse_data_queue);
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
        hids_end_handle   = svc->end_handle;

        /* Next: discover HID Report characteristic(s) within HID service */
        memcpy(&discover_uuid, BT_UUID_HIDS_REPORT, sizeof(discover_uuid));
        discover_params.uuid         = &discover_uuid.uuid;
        discover_params.start_handle = hids_start_handle + 1;
        discover_params.end_handle   = hids_end_handle;
        discover_params.type         = BT_GATT_DISCOVER_CHARACTERISTIC;

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
        discover_params.uuid         = &discover_uuid.uuid;
        discover_params.start_handle = attr->handle + 1;
        discover_params.end_handle   = hids_end_handle;
        discover_params.type         = BT_GATT_DISCOVER_DESCRIPTOR;

        err = bt_gatt_discover(conn, &discover_params);
        if (err) {
            printk("Discover CCC descriptor failed (err %d)\n", err);
        }
        return BT_GATT_ITER_STOP;
    }

    /* 3) Descriptor discovery: CCC (0x2902) */
    if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_GATT_CCC)) {
        // Iterating until we actually hit the CCC attribute
        if (bt_uuid_cmp(attr->uuid, BT_UUID_GATT_CCC)) {
            return BT_GATT_ITER_CONTINUE;
        }

        subscribe_params.notify     = notify_func;
        subscribe_params.value      = BT_GATT_CCC_NOTIFY;
        subscribe_params.ccc_handle = attr->handle;

        err = bt_gatt_subscribe(conn, &subscribe_params);
        if (err && err != -EALREADY) {
            printk("Subscribe failed (err %d)\n", err);
        } else {
            printk("[SUBSCRIBED]\n");
        }

        /* Next: Protocol Mode characteristic */
        memcpy(&discover_uuid, BT_UUID_HIDS_PROTOCOL_MODE, sizeof(discover_uuid));
        discover_params.uuid         = &discover_uuid.uuid;
        discover_params.start_handle = hids_start_handle + 1;
        discover_params.end_handle   = hids_end_handle;
        discover_params.type         = BT_GATT_DISCOVER_CHARACTERISTIC;

        err = bt_gatt_discover(conn, &discover_params);
        if (err) {
            printk("Discover Protocol Mode failed (err %d)\n", err);
        }
        return BT_GATT_ITER_STOP;
    }

    /* 4) Characteristic discovery: Protocol Mode 0x2A4E */
    if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_HIDS_PROTOCOL_MODE)) {
        hids_protocol_mode_handle = bt_gatt_attr_value_handle(attr);

        uint8_t val = 0x01; /* Report Protocol */
        err = bt_gatt_write_without_response(conn, hids_protocol_mode_handle,
                                             &val, sizeof(val), false);
        if (err) {
            printk("Write Protocol Mode failed (err %d)\n", err);
        }

        /* Next: Control Point characteristic */
        memcpy(&discover_uuid, BT_UUID_HIDS_CTRL_POINT, sizeof(discover_uuid));
        discover_params.uuid         = &discover_uuid.uuid;
        discover_params.start_handle = hids_start_handle + 1;
        discover_params.end_handle   = hids_end_handle;
        discover_params.type         = BT_GATT_DISCOVER_CHARACTERISTIC;

        err = bt_gatt_discover(conn, &discover_params);
        if (err) {
            printk("Discover Control Point failed (err %d)\n", err);
        }
        return BT_GATT_ITER_STOP;
    }

    /* 5) Characteristic discovery: Control Point 0x2A4C */
    if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_HIDS_CTRL_POINT)) {
        hids_ctrl_point_handle = bt_gatt_attr_value_handle(attr);

        // Optional: send wake 
        (void)bt_connection_hids_ctrl_point_write(0x01);

        return BT_GATT_ITER_STOP;
    }

    return BT_GATT_ITER_CONTINUE;
}

static bool eir_found_comment(struct bt_data *data, void *user_data)
{
	bt_addr_le_t *addr = user_data;
	int i;

	printk("[AD]: %u data_len %u\n", data->type, data->data_len);

	switch (data->type) {
	case BT_DATA_UUID16_SOME:
	case BT_DATA_UUID16_ALL:
		if (data->data_len % sizeof(uint16_t) != 0U) {
			printk("AD malformed\n");
			return true;
		}

		for (i = 0; i < data->data_len; i += sizeof(uint16_t)) {
			const struct bt_uuid *uuid;
			uint16_t u16;
			int err;

			memcpy(&u16, &data->data[i], sizeof(u16));
			uuid = BT_UUID_DECLARE_16(sys_le16_to_cpu(u16));

			/* Only connect to devices advertising the HID Service (0x1812) */
			if (bt_uuid_cmp(uuid, BT_UUID_HIDS)) {
				continue;
			}

			/* Already connected/connecting? keep parsing other ADs */
			if (default_conn || atomic_get(&connecting)) {
				return false;
			}

			/* Stop scanning before creating a connection */
			err = bt_le_scan_stop();
			if (err) {
				printk("Stop LE scan failed (err %d)\n", err);
				return false;
			}
			atomic_set(&scanning, 0);

			/* Claim the “connecting” state */
			if (!atomic_cas(&connecting, 0, 1)) {
				/* Someone else started connecting */
				return false;
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

					/* Give up and resume scanning */
					atomic_set(&connecting, 0);
					default_conn = NULL;
					start_scan();
				}
			}

			/* We found our target and initiated a connection. */
			return false;
		}
		break;

	default:
		break;
	}

	/* Continue parsing other AD structures */
	return true;
}

static void bond_pick_first(const struct bt_bond_info *info, void *user_data)
{
    // With CONFIG_BT_MAX_PAIRED=1, there will be onyl one
    memcpy(&preferred_addr, &info->addr, sizeof(preferred_addr));
    have_preferred = true;
}

static void refresh_bond_state(void)
{
    have_bond = false;
    have_preferred = false;

    bt_foreach_bond(BT_ID_DEFAULT, bond_pick_first, NULL);

    if (have_preferred) {
        have_bond = true;
        char s[BT_ADDR_LE_STR_LEN];
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
        // Connection attempt still ongoing or connected; don't scan
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
    err = bt_conn_le_create(addr, &create_param_coded,
                            BT_LE_CONN_PARAM_DEFAULT, &default_conn);
    if (err) {
        printk("Create coded PHY connection failed (err %d)\n", err);
        printk("Creating non-Coded PHY connection\n");
        err = bt_conn_le_create(addr, &create_param_1m,
                                BT_LE_CONN_PARAM_DEFAULT, &default_conn);
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

    /* Try direct connect. With RPA devices this may time out; we schedule scan fallback. */
    connect_to_addr(&preferred_addr);

    /* If the address is stale (RPA), this connect may never complete -> scan fallback */
    k_work_schedule(&connect_fallback_scan_work, K_MSEC(DIRECT_CONNECT_FALLBACK_DELAY_MS));
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
                         struct net_buf_simple *ad)
{
    char dev[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(addr, dev, sizeof(dev));

    printk("[DEVICE]: %s (type %u) AD len %u RSSI %d\n", dev, type, ad->len, rssi);

    // If no bond, only connect when user enabled pairing mode
	if (!have_bond && !pairing_allowed) {
		return;
	}

    if (default_conn || atomic_get(&connecting)) {
        return;
    }

    // Only connectable advertisements
    if (!(type == BT_GAP_ADV_TYPE_ADV_IND ||
          type == BT_GAP_ADV_TYPE_ADV_DIRECT_IND ||
          type == BT_GAP_ADV_TYPE_EXT_ADV)) {
        return;
    }

    if (rssi < RSSI_CONNECT_THRESHOLD) {
        return;
    }

    // Connect to the first near connactable decive
	// If it's not our bonded device, pairing will cancelled
    connect_to_addr(addr);
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
        .type     = BT_LE_SCAN_TYPE_ACTIVE,
        .options  = BT_LE_SCAN_OPT_CODED,
        .interval = BT_GAP_SCAN_FAST_INTERVAL,
        .window   = BT_GAP_SCAN_FAST_WINDOW,
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

static void connected(struct bt_conn *conn, uint8_t conn_err)
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
        printk("bt_conn_set_security(%u) failed (err %d)\n", target_sec, err);
    }
}

static bool discovery_started;

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

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    printk("Disconnected: %s, reason 0x%02x %s\n",
           addr, reason, bt_hci_err_to_str(reason));

    /* 1) If we had a subscription, unsubscribe first */
    if (subscribe_params.value_handle) {
        int e = bt_gatt_unsubscribe(conn, &subscribe_params);
        printk("Unsubscribe: %d\n", e);
    }

    /* 2) Cancel any outstanding GATT procedures that use discover_params */
    if (atomic_get(&discovering)) {
        bt_gatt_cancel(conn, &discover_params);
        atomic_set(&discovering, 0);
        discovery_started = false;
    }

    /* 3) Drop conn ref */
    if (default_conn == conn) {
        bt_conn_unref(default_conn);
        default_conn = NULL;
    }

    atomic_set(&connecting, 0);
    atomic_set(&scanning, 0);

    /* 4) Clear only your own state/handles */
    hids_start_handle = 0;
    hids_end_handle = 0;
    hids_ctrl_point_handle = 0;
    hids_protocol_mode_handle = 0;

    /* 5) Reset subscribe params fields you own (optional, but safe) */
    subscribe_params.value_handle = 0;
    subscribe_params.ccc_handle = 0;
    /* keep subscribe_params.notify/value assigned if you want */

    refresh_bond_state();
    start_scan();
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
    printk("Numeric comparison: %06u (auto-accept passkey confirm)\n", passkey);
    bt_conn_auth_passkey_confirm(conn);
}

static void auth_passkey_entry(struct bt_conn *conn)
{
    /* Replace with real input (UART/CLI/button) */
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
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    printk("Pairing complete: %s bonded=%u\n", addr, bonded);

    if (bonded) {
        bt_connection_enable_pairing_mode(false);
        /* refresh bond state if you track have_bond/have_preferred */
        refresh_bond_state();
    }
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

static void security_changed(struct bt_conn *conn, bt_security_t level,
                             enum bt_security_err err)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (err) {
        printk("Security failed: %s level %u err %d\n", addr, level, err);
        return;
    }

    printk("Security changed: %s level %u\n", addr, level);

    if (conn == default_conn && !discovery_started && level >= target_sec) {
        start_hids_discovery(conn);
    }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

int bt_connection_hids_ctrl_point_write(uint8_t val){
	if (!default_conn) return -ENOTCONN;
	if (!hids_ctrl_point_handle) return -EINVAL;
	return bt_gatt_write_without_response(default_conn, hids_ctrl_point_handle, &val, sizeof(val), false);
}

static void pairing_mode_timeout_fn(struct k_work *work)
{
    ARG_UNUSED(work);
    bt_connection_enable_pairing_mode(false);
}

static void bt_enable_ready_cb(int e)
{
    if (e) {
        printk("bt_enable_ready_cb err %d\n", e);
        return;
    }

    int err = settings_load();
    if (err) {
        printk("settings_load failed (err %d)\n", err);
    }

    err = bt_conn_auth_cb_register(&auth_cb);
    printk("auth_cb_register: %d\n", err);

    err = bt_conn_auth_info_cb_register(&auth_info_cb);
    printk("auth_info_cb_register: %d\n", err);

    k_work_init_delayable(&connect_fallback_scan_work, connect_fallback_scan_fn);
    k_work_init_delayable(&pairing_mode_timeout_work, pairing_mode_timeout_fn);

    pairing_allowed = false;
    refresh_bond_state();

    printk("Bluetooth initialized\n");

    if (have_bond) {
        try_connect_preferred_or_scan();
    } else {
        printk("No bond. Press Button0 to enable pairing mode.\n");
        //start_scan();
    }
}

void bt_connection_enable_pairing_mode(bool enable)
{
    pairing_allowed = enable;
    printk("Pairing mode: %s\n", enable ? "ENABLED" : "DISABLED");

    if (enable) {
        start_scan();
    }
}


void bt_connection_enable(void)
{
    int err = bt_enable(bt_enable_ready_cb);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return;
    }
}

void ble_hid_app_start(void)
{

	bt_connection_enable();

}



static void button_input_cb(struct input_event *evt, void *user_data)
{
	if (evt->sync == 0) {
		return;
	}

	if (evt->code != 2) {
    	return;
	}

	printk("Button %d %s at %" PRIu32 "\n", evt->code, evt->value ? "pressed" : "released", k_cycle_get_32());

	if (evt->value){
		// Button pressed
		// No matter which button...

		/* Toggle pairing mode on each press */
		bool new_state = !pairing_allowed;
		bt_connection_enable_pairing_mode(new_state);

		/* If enabling, auto-disable after 60 seconds */
		if (new_state) {
			k_work_schedule(&pairing_mode_timeout_work, K_SECONDS(60));
		} else {
			k_work_cancel_delayable(&pairing_mode_timeout_work);
		}

	}

}

INPUT_CALLBACK_DEFINE(NULL, button_input_cb, NULL);

