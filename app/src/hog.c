/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/settings/settings.h>
#include <zephyr/init.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, 4); // CONFIG_ZMK_LOG_LEVEL);

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>

#include <zmk/ble.h>
#include <zmk/hog.h>
#include <zmk/hid.h>
#include "./launcher/mousekey.h"

enum {
    HIDS_REMOTE_WAKE = BIT(0),
    HIDS_NORMALLY_CONNECTABLE = BIT(1),
};

struct hids_info {
    uint16_t version; /* version number of base USB HID Specification */
    uint8_t code;     /* country HID Device hardware is localized for. */
    uint8_t flags;
} __packed;

struct hids_report {
    uint8_t id;   /* report id */
    uint8_t type; /* report type */
} __packed;

static struct hids_info info = {
    .version = 0x0000,
    .code = 0x00,
    .flags = HIDS_NORMALLY_CONNECTABLE | HIDS_REMOTE_WAKE,
};

enum {
    HIDS_INPUT = 0x01,
    HIDS_OUTPUT = 0x02,
    HIDS_FEATURE = 0x03,
};

static struct hids_report input = {
    .id = ZMK_HID_REPORT_ID_KEYBOARD,
    .type = HIDS_INPUT,
};
#if CONFIG_ADAPATIVE_NKRO
static struct hids_report input_nkro = {
    .id = ZMK_HID_REPORT_ID_KEYBOARD_NKRO,
    .type = HIDS_INPUT,
};
#endif 
static struct hids_report consumer_input = {
    .id = ZMK_HID_REPORT_ID_CONSUMER,
    .type = HIDS_INPUT,
};

static struct hids_report output = {
    .id = ZMK_HID_REPORT_ID_KEYBOARD,
    .type = HIDS_OUTPUT,
};
static struct hids_report mouse_input = {
    .id = ZMK_HID_REPORT_ID_MOUSE,
    .type = HIDS_INPUT,
};
static bool host_requests_notification = false;
static uint8_t ctrl_point;
static uint8_t hids_outp_rep[20];
// static uint8_t proto_mode;
extern uint8_t sec_ready ;

static ssize_t read_hids_info(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                              uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
                             sizeof(struct hids_info));
}

static ssize_t read_hids_report_ref(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                    void *buf, uint16_t len, uint16_t offset) {
    uint8_t *p_data = attr->user_data;
    LOG_DBG("report ref,id:%d,type:%d", p_data[0], p_data[1]);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
                             sizeof(struct hids_report));
}

static ssize_t read_hids_report_map(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                    void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, zmk_hid_report_desc,
                             sizeof(zmk_hid_report_desc));
}

static ssize_t read_hids_input_report(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                      void *buf, uint16_t len, uint16_t offset) {
    struct zmk_hid_keyboard_report_body *report_body = &zmk_hid_get_keyboard_report()->body;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, report_body,
                             sizeof(struct zmk_hid_keyboard_report_body));
}
#if CONFIG_ADAPATIVE_NKRO
static ssize_t read_hids_input_nkro_report(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                      void *buf, uint16_t len, uint16_t offset) {
    
    {
        struct zmk_hid_keyboard_report_body *report_body = &zmk_hid_get_keyboard_report()->body;
        uint8_t buffer[(ZMK_HID_KEYBOARD_NKRO_MAX_USAGE + 1) / 8+1];
        buffer[0]=report_body->modifiers;       
        memcpy(buffer+1,report_body->keys,(ZMK_HID_KEYBOARD_NKRO_MAX_USAGE + 1) / 8);
        return bt_gatt_attr_read(conn, attr, buf, len, offset, buffer,
                                sizeof(struct zmk_hid_keyboard_report_body)-1);
    }                                        
    return 0;
}
#endif 
static ssize_t read_hids_consumer_input_report(struct bt_conn *conn,
                                               const struct bt_gatt_attr *attr, void *buf,
                                               uint16_t len, uint16_t offset) {
    struct zmk_hid_consumer_report_body *report_body = &zmk_hid_get_consumer_report()->body;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, report_body,
                             sizeof(struct zmk_hid_consumer_report_body));
}
static ssize_t read_hids_mouse_input_report(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                            void *buf, uint16_t len, uint16_t offset) {
    report_mouse_t report = mousekey_get_report();
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &report.buttons,
                             sizeof(report_mouse_t) - 1);
}
// static ssize_t write_proto_mode(struct bt_conn *conn,
//                                 const struct bt_gatt_attr *attr,
//                                 const void *buf, uint16_t len, uint16_t offset,
//                                 uint8_t flags)
// {
//     printk("PROTO CHANGED\n");
//     return 0;
// }

static void input_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    host_requests_notification = (value == BT_GATT_CCC_NOTIFY) ? 1 : 0;
}

void keyboad_led_set_onoff(uint8_t led_state);

static ssize_t hids_outp_rep_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                   const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    uint8_t *value = attr->user_data;
    LOG_DBG("hids_outp_rep_write");
    if (offset + len > sizeof(hids_outp_rep)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    memcpy(value + offset, buf, len);
    LOG_HEXDUMP_INF(buf, len, "ble  report");
    const uint8_t *report = buf;
    // if(report[0]==ZMK_HID_REPORT_ID_KEYBOARD)
    {
        keyboad_led_set_onoff(report[0]);
    }

    return len;
}
static ssize_t write_ctrl_point(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    uint8_t *value = attr->user_data;

    if (offset + len > sizeof(ctrl_point)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    memcpy(value + offset, buf, len);
    LOG_HEXDUMP_INF(buf, len, "write_ctrl_point");
    return len;
}

/* HID Service Declaration */
BT_GATT_SERVICE_DEFINE(
    hog_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),
    //    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_PROTOCOL_MODE, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
    //                           BT_GATT_PERM_WRITE, NULL, write_proto_mode, &proto_mode),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ, BT_GATT_PERM_READ, read_hids_info,
                           NULL, &info),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ, BT_GATT_PERM_READ_ENCRYPT,
                           read_hids_report_map, NULL, NULL),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT, read_hids_input_report, NULL, NULL),
    BT_GATT_CCC(input_ccc_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, read_hids_report_ref,
                       NULL, &input),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT, read_hids_consumer_input_report, NULL, NULL),
    BT_GATT_CCC(input_ccc_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, read_hids_report_ref,
                       NULL, &consumer_input),
    // mouse reprot
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT, read_hids_mouse_input_report, NULL, NULL),
    BT_GATT_CCC(input_ccc_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, read_hids_report_ref,
                       NULL, &mouse_input),
#if CONFIG_ADAPATIVE_NKRO                       
    //add nkro report                       
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT, read_hids_input_nkro_report, NULL, NULL),
    BT_GATT_CCC(input_ccc_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, read_hids_report_ref,
                       NULL, &input_nkro),     
#endif                                         
    // led out
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE, NULL, hids_outp_rep_write, hids_outp_rep),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, read_hids_report_ref,
                       NULL, &output),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE, NULL, write_ctrl_point, &ctrl_point),

);

struct bt_conn *destination_connection() {
    struct bt_conn *conn;
    bt_addr_le_t *addr = zmk_ble_active_profile_addr();
    // LOG_DBG("Address pointer %p", addr);
    if (!bt_addr_le_cmp(addr, BT_ADDR_LE_ANY)) {
        LOG_WRN("Not sending, no active address for current profile");
        return NULL;
    } else if ((conn = bt_conn_lookup_addr_le(zmk_ble_get_active_profile_bt_id(), addr)) == NULL) {
        LOG_WRN("Not sending, not connected to active profile");
        return NULL;
    }

    return conn;
}

K_THREAD_STACK_DEFINE(hog_q_stack, CONFIG_ZMK_BLE_THREAD_STACK_SIZE);

struct k_work_q hog_work_q;

K_MSGQ_DEFINE(zmk_hog_keyboard_msgq, sizeof(struct zmk_hid_keyboard_report_body),
              CONFIG_ZMK_BLE_KEYBOARD_REPORT_QUEUE_SIZE, 4);
uint8_t zmk_ble_is_connected(void);
void clear_keys_work_cb(struct k_work *work)
{
    if (zmk_ble_is_connected() == 0)
    {
        LOG_WRN("clear keys:%d!",k_msgq_num_used_get(&zmk_hog_keyboard_msgq));
        struct zmk_hid_keyboard_report_body report;
        while(k_msgq_num_used_get(&zmk_hog_keyboard_msgq)>0)
        {
            k_msgq_get(&zmk_hog_keyboard_msgq,&report,K_NO_WAIT);
        }
    }
}
K_WORK_DELAYABLE_DEFINE(clear_keys_work,clear_keys_work_cb);
void wait_to_send_work_cb(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(wait_to_send_work,wait_to_send_work_cb);
void slow_send_work_cb(struct k_work *work){};
K_WORK_DELAYABLE_DEFINE(slow_send_work,slow_send_work_cb);

static uint8_t first_keys_count;
static bool enabled_send_queue=true;
void send_keyboard_report_callback(struct k_work *work) {
    struct zmk_hid_keyboard_report_body report;
    if (zmk_ble_is_connected() == 0)
    {
        LOG_WRN("queue num:%d",k_msgq_num_used_get(&zmk_hog_keyboard_msgq));
        enabled_send_queue=true;
        while(k_msgq_num_used_get(&zmk_hog_keyboard_msgq)>12)
        {
            k_msgq_get(&zmk_hog_keyboard_msgq,&report,K_NO_WAIT);
        }
        k_work_reschedule(&clear_keys_work,K_MSEC(3000));
        return;
    }
    if(sec_ready ==0) {
        return;
    }
    LOG_WRN("tx queue total:%d",k_msgq_num_used_get(&zmk_hog_keyboard_msgq));
    uint8_t tx_count=0;
    while (k_msgq_get(&zmk_hog_keyboard_msgq, &report, K_NO_WAIT) == 0) {

        struct bt_conn *conn = destination_connection();
        if (conn == NULL) {
            LOG_ERR("con NULL");
            return;
        }
        struct bt_gatt_notify_params notify_params = {
            .attr = &hog_svc.attrs[5],
            .data = &report,
#if CONFIG_ADAPATIVE_NKRO               
            .len  = CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE+2,
#else            
            .len  = sizeof(report),
#endif             
            .func = NULL,
        };
        
#if CONFIG_ADAPATIVE_NKRO     
        uint8_t buffer[(ZMK_HID_KEYBOARD_NKRO_MAX_USAGE + 1) / 8+1];
        if(report._reserved)
        {
            // LOG_ERR("NKRO");
            // LOG_HEXDUMP_ERR(&report,sizeof(struct zmk_hid_keyboard_report_body),"nkro");
            
            buffer[0]=report.modifiers;       
            memcpy(buffer+1,report.keys,(ZMK_HID_KEYBOARD_NKRO_MAX_USAGE + 1) / 8);

            notify_params.attr = &hog_svc.attrs[17];
            notify_params.data = buffer;
            notify_params.len = sizeof(report)-1;
        }
#endif         
        // LOG_HEXDUMP_ERR(notify_params.data,notify_params.len,"rp");
        // LOG_ERR("gatt send,len:%d",notify_params.len);
        int err = bt_gatt_notify_cb(conn, &notify_params);
        if (err) {
            LOG_ERR("Error notifying %d", err);
        }
        bt_conn_unref(conn);
        // LOG_HEXDUMP_INF(notify_params.data, notify_params.len, "hog");
        LOG_ERR("tx count:%d,tx err:%d,queue left:%d,first:%d",tx_count+1,err,k_msgq_num_used_get(&zmk_hog_keyboard_msgq),first_keys_count);
        if(first_keys_count>0)
        {
            first_keys_count --;
            k_work_reschedule(&wait_to_send_work,K_MSEC(45));
            break;
        }
        if(k_work_delayable_is_pending(&slow_send_work)&&k_msgq_num_used_get(&zmk_hog_keyboard_msgq))
        {
            k_work_reschedule(&wait_to_send_work,K_MSEC(14));
            break;
        }
        if((++tx_count>=4) &&k_msgq_num_used_get(&zmk_hog_keyboard_msgq) )
        {
            // LOG_ERR("wait to send>>");
            k_work_reschedule(&wait_to_send_work,K_MSEC(30));
            break;
        }
        if (k_msgq_num_used_get(&zmk_hog_keyboard_msgq) == 0) {
            // LOG_DBG("msgq empty");
            break;
        }
    }

    LOG_ERR(">>");
}

K_WORK_DEFINE(hog_keyboard_work, send_keyboard_report_callback);

int zmk_hog_send_keyboard_report(struct zmk_hid_keyboard_report_body *report) {
    int err = k_msgq_put(&zmk_hog_keyboard_msgq, report, K_MSEC(100));
    if (err) {
        switch (err) {
        case -EAGAIN: {
            LOG_WRN("Keyboard message queue full, popping first message and queueing again");
            struct zmk_hid_keyboard_report_body discarded_report;
            k_msgq_get(&zmk_hog_keyboard_msgq, &discarded_report, K_NO_WAIT);
            return zmk_hog_send_keyboard_report(report);
        }
        default:
            LOG_WRN("Failed to queue keyboard report to send (%d)", err);
            return err;
        }
    }
    if(!k_work_delayable_is_pending(&wait_to_send_work)) {
        err = k_work_submit_to_queue(&hog_work_q, &hog_keyboard_work);
        LOG_DBG("k_work_submit_to_queue:%d", err);
    }
    else
    {
        LOG_ERR("wait some time to send");
    }
    return 0;
};

K_MSGQ_DEFINE(zmk_hog_consumer_msgq, sizeof(struct zmk_hid_consumer_report_body),
              CONFIG_ZMK_BLE_CONSUMER_REPORT_QUEUE_SIZE, 4);

void send_consumer_report_callback(struct k_work *work) {
    struct zmk_hid_consumer_report_body report;

    while (k_msgq_get(&zmk_hog_consumer_msgq, &report, K_NO_WAIT) == 0) {
        struct bt_conn *conn = destination_connection();
        if (conn == NULL) {
            return;
        }

        struct bt_gatt_notify_params notify_params = {
            .attr = &hog_svc.attrs[9],
            .data = &report,
            .len = sizeof(report),
        };

        int err = bt_gatt_notify_cb(conn, &notify_params);
        if (err) {
            LOG_DBG("Error notifying %d", err);
        }

        bt_conn_unref(conn);
    }
};

K_WORK_DEFINE(hog_consumer_work, send_consumer_report_callback);

int zmk_hog_send_consumer_report(struct zmk_hid_consumer_report_body *report) {
    int err = k_msgq_put(&zmk_hog_consumer_msgq, report, K_MSEC(100));
    if (err) {
        switch (err) {
        case -EAGAIN: {
            LOG_WRN("Consumer message queue full, popping first message and queueing again");
            struct zmk_hid_consumer_report_body discarded_report;
            k_msgq_get(&zmk_hog_consumer_msgq, &discarded_report, K_NO_WAIT);
            return zmk_hog_send_consumer_report(report);
        }
        default:
            LOG_WRN("Failed to queue consumer report to send (%d)", err);
            return err;
        }
    }

    k_work_submit_to_queue(&hog_work_q, &hog_consumer_work);

    return 0;
};

K_MSGQ_DEFINE(zmk_hog_mouse_msgq, sizeof(report_mouse_t), 10, 4);

void send_mouse_report_callback(struct k_work *work) {
    report_mouse_t report;

    while (k_msgq_get(&zmk_hog_mouse_msgq, &report, K_NO_WAIT) == 0) {
        struct bt_conn *conn = destination_connection();
        if (conn == NULL) {
            return;
        }

        struct bt_gatt_notify_params notify_params = {
            .attr = &hog_svc.attrs[13],
            .data = &report.buttons,
            .len = sizeof(report) - 1,
        };

        int err = bt_gatt_notify_cb(conn, &notify_params);
        if (err) {
            LOG_DBG("Error notifying %d", err);
        }

        bt_conn_unref(conn);
    }
};

K_WORK_DEFINE(hog_mouse_work, send_mouse_report_callback);

int zmk_hog_send_mouse_report(report_mouse_t *report) {
    int err = k_msgq_put(&zmk_hog_mouse_msgq, report, K_MSEC(100));
    if (err) {
        switch (err) {
        case -EAGAIN: {
            LOG_WRN("Consumer message queue full, popping first message and queueing again");
            report_mouse_t discarded_report;
            k_msgq_get(&zmk_hog_mouse_msgq, &discarded_report, K_NO_WAIT);
            return zmk_hog_send_mouse_report(report);
        }
        default:
            LOG_WRN("Failed to queue mouse report to send (%d)", err);
            return err;
        }
    }

    k_work_submit_to_queue(&hog_work_q, &hog_mouse_work);

    return 0;
};

int zmk_hog_init(const struct device *_arg) {
    static const struct k_work_queue_config queue_config = {.name = "HID Over GATT Send Work"};
    k_work_queue_start(&hog_work_q, hog_q_stack, K_THREAD_STACK_SIZEOF(hog_q_stack),
                       CONFIG_ZMK_BLE_THREAD_PRIORITY, &queue_config);

    return 0;
}

SYS_INIT(zmk_hog_init, APPLICATION, CONFIG_ZMK_BLE_INIT_PRIORITY);

void hog_send_queue_data_on_connection(void) {  

    uint8_t nums = k_msgq_num_used_get(&zmk_hog_keyboard_msgq);
    if (nums) {
        if(enabled_send_queue)
        {
            enabled_send_queue =false;
            first_keys_count = nums;            
            LOG_ERR("-->send queue keys!:%d",first_keys_count);
        }
        k_work_reschedule(&wait_to_send_work,K_MSEC(100));
    }
    k_work_reschedule(&slow_send_work,K_MSEC(1200));
}
void wait_to_send_work_cb(struct k_work *work)
{
    if (k_msgq_num_used_get(&zmk_hog_keyboard_msgq)) {
        LOG_ERR("-->send queue,total:%d",k_msgq_num_used_get(&zmk_hog_keyboard_msgq));
        k_work_submit_to_queue(&hog_work_q, &hog_keyboard_work);
    }
}
void clear_queue_keys(void)
{
    k_work_reschedule(&clear_keys_work,K_MSEC(10));
}