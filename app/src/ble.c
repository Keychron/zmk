/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/init.h>

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#include <zephyr/settings/settings.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/kernel.h>
// #include <zephyr/bluetooth/hci_err.h>
#include "conn_internal.h"
#include <zmk/leds.h>
#include <zmk/endpoints.h>

#if IS_ENABLED(CONFIG_SETTINGS)

#include <zephyr/settings/settings.h>

#endif

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);
#include <zmk/activity.h>
#include <zmk/ble.h>
#include <zmk/keys.h>
#include <zmk/split/bluetooth/uuid.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zephyr/sys/reboot.h>
#include <zmk/usb.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keymap.h>
#if IS_ENABLED(CONFIG_ZMK_BLE_PASSKEY_ENTRY)
#define PASSKEY_DIGITS 6

static struct bt_conn *auth_passkey_entry_conn;
RING_BUF_DECLARE(passkey_entries, PASSKEY_DIGITS);

#endif /* IS_ENABLED(CONFIG_ZMK_BLE_PASSKEY_ENTRY) */

#define ADV_RECONN_TIME_OUT (3000)//(3*60*1000)
#define ADV_PAIR_TIME_OUT (3*60*1000)
#if EN_UPATE_PARAM_DYNAMIC  //disable for sometime send data fail!
void ble_param_update_work_callback(struct k_work *work);
void ble_active_work_callback(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(ble_active_work, ble_active_work_callback);
K_WORK_DELAYABLE_DEFINE(ble_param_update_work, ble_param_update_work_callback);
static uint8_t ble_active;
static uint8_t ble_param_update_try_count =0;
#endif 

void update_conn_param_worker(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(update_conn_param_work, update_conn_param_worker);

bt_addr_le_t * inc_bt_addr(uint8_t index);
void print_device_addr(uint8_t id);
void zmk_24g_pair(void);
void zmk_24g_reconn(void);
void set_dis_value(void);
int load_immediate_value(const char *name, void *dest, size_t len);
bool bat_is_shutdown(void);
void zmk_ble_disconn_active_profile(void);
void zmk_24g_endpoint_disconnect(void);
void ble_active_handler(void);
void set_delay_clear_bonds(uint8_t set);
uint8_t delay_clear_bonds(void);

enum advertising_type {
    ZMK_ADV_NONE,
    ZMK_ADV_DIR,
    ZMK_ADV_CONN,
    ZMK_ADV_RECONN,
    ZMK_ADV_PAIR,
} advertising_status;

static uint8_t adv_state;
void sleep_worker(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(sleep_work, sleep_worker);


static void load_identities(void) {

    bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
    size_t count = ARRAY_SIZE(addrs);

    bt_id_get(addrs, &count);

    LOG_INF("Device has %zu identities\n", count);

    /* Default identity should always exist */
    for (int i = 0; i < count; i++) {
        char addr_str[64] = {0};

        bt_addr_le_to_str(&addrs[i], addr_str, sizeof(addr_str));
        LOG_INF("Device i:%d,addr:%s\n", i, addr_str);
    }

    for (; count < CONFIG_BT_ID_MAX; count++) {
        int err = bt_id_create(NULL, NULL);

        if (err < 0) {
            printk("Cannot create identity (err:%d)\n", err);

            break;
        } else {

            printk("Identity %zu created\n", count);
        }
    }
}

#define CURR_ADV(adv) (adv << 4)

#define ZMK_ADV_CONN_NAME                                                                          \
    BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_ONE_TIME, BT_GAP_ADV_FAST_INT_MIN_2, \
                    BT_GAP_ADV_FAST_INT_MAX_2, NULL)

static struct zmk_ble_profile profiles[ZMK_BLE_PROFILE_COUNT];
static uint8_t active_profile;
enum advertising_type desired_adv = ZMK_ADV_NONE; // change
static uint8_t adv_bt_id;

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

BUILD_ASSERT(DEVICE_NAME_LEN <= 16, "ERROR: BLE device name is too long. Max length: 16");

static const struct bt_data zmk_ble_ad[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
    BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE, 0xC1, 0x03),
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_SOME, 0x12, 0x18, /* HID Service */
                  0x0f, 0x18                       /* Battery Service */
                  ),
};
//add swift pair
static const struct bt_data zmk_ble_ad_win[] = {
    BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE, 0xC1, 0x03),
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_SOME, 0x12, 0x18, /* HID Service */
                  0x0f, 0x18                       /* Battery Service */
                  ),
    //swift pair
    BT_DATA_BYTES(BT_DATA_MANUFACTURER_DATA,0x06,0x00, 0x03,0x00,0x80, 'K','e','y','c','h','r','o','n',' ','K','B')

};
static const struct bt_data zmk_ble_ad_sr[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data zmk_ble_reconn_ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

void adv_timeout_work_callback(struct k_work *work) {
    int err = bt_le_adv_stop();
    advertising_status = ZMK_ADV_NONE;
    blue_led_set_state( LED_PEER_STATE_DISCONNECTED);

    LOG_INF("adv timeout ,stop");
    if (err) {
        LOG_ERR("Failed to stop advertising (err %d)", err);
    }
    k_work_reschedule(&sleep_work, (adv_state ==ZMK_ADV_RECONN)?K_MSEC(40*1000-ADV_RECONN_TIME_OUT):K_NO_WAIT);
    adv_state = ZMK_ADV_NONE;  
}
void sleep_worker(struct k_work *work)
{
    LOG_INF("sleep");
    set_state(ZMK_ACTIVITY_SLEEP);
}
K_WORK_DELAYABLE_DEFINE(adv_timeout_work, adv_timeout_work_callback);
void print_bt_addr(const bt_addr_le_t *addr);
int update_advertising(void);
// static struct k_work_delayable update_advertising_work;
static void update_advertising_callback(struct k_work *work) {
    LOG_INF("update_advertising_callback");
    update_advertising();
}
// K_WORK_DELAYABLE_DEFINE(update_advertising_work, update_advertising_callback);
K_WORK_DEFINE(update_advertising_work, update_advertising_callback);

bool zmk_ble_active_profile_is_connected(void);
bool zmk_ble_active_profile_is_open(void);
int zmk_ble_prof_pair_start(uint8_t index);
int zmk_ble_prof_select(uint8_t index);
void hid_disconnected(struct bt_conn *conn, uint8_t reason);
void insert_conn_object(struct bt_conn *conn);
void load_identities(void);
struct switch_to_adv {
    uint8_t adv_type : 4;
    uint8_t index : 4;
};
struct switch_to_adv switch_state;
// static uint8_t switch_to_pair;
/* Bonded address queue. */
// K_MSGQ_DEFINE(bonds_queue,
//           sizeof(bt_addr_le_t),
//           CONFIG_BT_MAX_PAIRED,
//           4);

static void bond_cnt_cb(const struct bt_bond_info *info, void *user_data) {
    size_t *cnt = user_data;
    if (bt_addr_le_cmp(&info->addr, zmk_ble_active_profile_addr())) {
        // not same
        return;
    }
    (*cnt)++;
}

static size_t bond_check(uint8_t local_id) {
    size_t cnt = 0;

    bt_foreach_bond(local_id, bond_cnt_cb, &cnt);

    // if(cnt==0)
    // {
    //     profiles[local_id].bt_id =0xff;
    // }

    return cnt;
}

// extern struct k_msgq bonds_queue;

// static void bond_find(const struct bt_bond_info *info, void *user_data) {
//     int err;

//     if (bt_addr_le_cmp(&info->addr, zmk_ble_active_profile_addr()) ||
//         zmk_ble_active_profile_is_connected()) {
//         // not same
//         return;
//     }
//     printk("id:%d,bond peer ", active_profile);
//     print_bt_addr(&info->addr);
//     err = k_msgq_put(&bonds_queue, (void *)&info->addr, K_NO_WAIT);
//     if (err) {
//         printk("No space in the queue for the bond.\n");
//     }
// }

static int setup_accept_list(void) {
    int err = bt_le_filter_accept_list_clear();
    if (err) {
        LOG_ERR("Cannot clear filter accept list (err: %d)", err);
        return err;
    }

    err = bt_le_filter_accept_list_add(zmk_ble_active_profile_addr());
    if (err) {
        LOG_ERR("Cannot add peer to filter accept list (err: %d)", err);
        return err;
    }

    return 0;
}
static void advertising_start(void) {
    // if (zmk_ble_active_profile_is_open()) {
    //     desired_adv = ZMK_ADV_CONN;
    // } else if (!zmk_ble_active_profile_is_connected()) {
    //     desired_adv = ZMK_ADV_RECONN;
    // }

    // LOG_INF("advertising_start,desired_adv:%d\n",desired_adv);
    // k_work_submit(&update_advertising_work);
    if(adv_state!=ZMK_ADV_PAIR)
        zmk_ble_prof_select(active_profile);
}

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

static bt_addr_le_t peripheral_addrs[ZMK_SPLIT_BLE_PERIPHERAL_COUNT];

#endif /* IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) */

static void raise_profile_changed_event() {
    // ZMK_EVENT_RAISE(new_zmk_ble_active_profile_changed((struct zmk_ble_active_profile_changed){
    //     .index = active_profile, .profile = &profiles[active_profile]}));
    LOG_INF("raise_profile_changed_event");
    if (!zmk_ble_active_profile_is_connected() && switch_state.adv_type) {
        switch (switch_state.adv_type) {
        case ZMK_ADV_PAIR:
            switch_state.adv_type = 0;
            zmk_ble_prof_pair_start(switch_state.index);
            break;
        case ZMK_ADV_RECONN:
            switch_state.adv_type = 0;
            zmk_ble_prof_select(switch_state.index);
            break;
        }
    }
}

static void raise_profile_changed_event_callback(struct k_work *work) {
    raise_profile_changed_event();
}

K_WORK_DEFINE(raise_profile_changed_event_work, raise_profile_changed_event_callback);

bool zmk_ble_active_profile_is_open() {
    return !bt_addr_le_cmp(&profiles[active_profile].peer, BT_ADDR_LE_ANY);
}
void copy_profile_to_same_peer(const bt_addr_le_t *peer) {
    for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
        if (!bt_addr_le_cmp(&profiles[i].peer, peer) &&
            (profiles[i].bt_id != profiles[active_profile].bt_id)) {
            bt_id_reset(profiles[i].bt_id,inc_bt_addr(profiles[i].bt_id), NULL);
            profiles[i].bt_id = profiles[active_profile].bt_id;
            // profiles[i].is_rpa = profiles[active_profile].is_rpa;
            profiles[i].bonded = profiles[active_profile].bonded;
            char setting_name[20];
            sprintf(setting_name, "ble/profiles/%d", i);
            settings_save_one(setting_name, &profiles[i], sizeof(struct zmk_ble_profile));
            LOG_INF("copy profle %d to %d", active_profile, i);
        }
    }
}
void set_profile_address(uint8_t index, const bt_addr_le_t *addr) {
    char setting_name[20];
    char addr_str[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

    memcpy(&profiles[index].peer, addr, sizeof(bt_addr_le_t));
    sprintf(setting_name, "ble/profiles/%d", index);
    LOG_DBG("Setting profile addr for %s to %s", setting_name, addr_str);
    settings_save_one(setting_name, &profiles[index], sizeof(struct zmk_ble_profile));
    k_work_submit(&raise_profile_changed_event_work);
}
void save_profile(uint8_t index) {
    char setting_name[20];
    sprintf(setting_name, "ble/profiles/%d", index);
    settings_save_one(setting_name, &profiles[index], sizeof(struct zmk_ble_profile));
}

bool zmk_ble_active_profile_is_connected() {
    struct bt_conn *conn;
    struct bt_conn_info info;
    bt_addr_le_t *addr = zmk_ble_active_profile_addr();
    char addr_str[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
    // LOG_INF("active profie:%d,peer:%s",active_profile,addr_str);
    if (!bt_addr_le_cmp(addr, BT_ADDR_LE_ANY)) {
        return false;
    } else if ((conn = bt_conn_lookup_addr_le(profiles[active_profile].bt_id, addr)) == NULL) {
        LOG_INF("pro:%d,conn found none", active_profile);
        return false;
    }

    bt_conn_get_info(conn, &info);

    bt_conn_unref(conn);

    return info.state == BT_CONN_STATE_CONNECTED;
}

#define CHECKED_ADV_STOP()                                                                         \
    err = bt_le_adv_stop();                                                                        \
    advertising_status = ZMK_ADV_NONE;                                                             \
    if (err) {                                                                                     \
        LOG_ERR("Failed to stop advertising (err %d)", err);                                       \
        return err;                                                                                \
    }

#define _CHECKED_DIR_ADV()                                                                         \
    addr = zmk_ble_active_profile_addr();                                                          \
    conn = bt_conn_lookup_addr_le(BT_ID_DEFAULT, addr);                                            \
    if (conn != NULL) { /* TODO: Check status of connection */                                     \
        LOG_DBG("Skipping advertising, profile host is already connected");                        \
        bt_conn_unref(conn);                                                                       \
        return 0;                                                                                  \
    }                                                                                              \
    err = bt_le_adv_start(BT_LE_ADV_CONN_DIR_LOW_DUTY(addr), zmk_ble_ad, ARRAY_SIZE(zmk_ble_ad),   \
                          NULL, 0);                                                                \
    if (err) {                                                                                     \
        LOG_ERR("Advertising failed to start (err %d)", err);                                      \
        return err;                                                                                \
    }                                                                                              \
    advertising_status = ZMK_ADV_DIR;
// adv_param.options |= BT_LE_ADV_OPT_DIR_ADDR_RPA;
#define CHECKED_DIR_ADV()                                                                          \
    {                                                                                              \
        char addr_buf[BT_ADDR_LE_STR_LEN];                                                         \
        addr = zmk_ble_active_profile_addr();                                                      \
        struct bt_le_adv_param adv_param;                                                          \
        adv_param = *BT_LE_ADV_CONN_DIR(addr);                                                     \
        adv_param.id = adv_bt_id;                                                                  \
        if (IS_ENABLED(CONFIG_BT_PRIVACY)) {                                                       \
            adv_param.options |= BT_LE_ADV_OPT_DIR_ADDR_RPA;                                       \
        }                                                                                          \
        adv_param.options |= BT_LE_ADV_OPT_USE_IDENTITY;                                           \
        int err = bt_le_adv_start(&adv_param, NULL, 0, NULL, 0);                                   \
        if (err) {                                                                                 \
            LOG_ERR("Directed advertising failed to start\n");                                     \
            return err;                                                                            \
        }                                                                                          \
        bt_addr_le_to_str(addr, addr_buf, BT_ADDR_LE_STR_LEN);                                     \
        LOG_INF("Direct advertising to %s started,prof:%d,id:%d\n", addr_buf, active_profile,      \
                profiles[active_profile].bt_id);                                                   \
        advertising_status = ZMK_ADV_DIR;                                                          \
    }

#define CHECKED_OPEN_ADV()                                                                         \
    {                                                                                              \
        struct bt_le_adv_param adv_param;                                                          \
        adv_param = *ZMK_ADV_CONN_NAME;                                                            \
        adv_param.id = adv_bt_id;                                                                  \
        if (IS_ENABLED(CONFIG_BT_PRIVACY)) {                                                       \
            adv_param.options |= BT_LE_ADV_OPT_DIR_ADDR_RPA;                                       \
        }                                                                                          \
        adv_param.options |= BT_LE_ADV_OPT_USE_IDENTITY;                                           \
        if (enable_filter == 0) {                                                                  \
            adv_param.options |= BT_LE_ADV_OPT_FILTER_SCAN_REQ;                                    \
            adv_param.options |= BT_LE_ADV_OPT_FILTER_CONN;                                        \
        }                                                                                          \
        err = bt_le_adv_start(&adv_param, zmk_ble_ad, ARRAY_SIZE(zmk_ble_ad), NULL, 0);            \
        if (err) {                                                                                 \
            LOG_ERR("Advertising failed to start (err %d)", err);                                  \
            return err;                                                                            \
        }                                                                                          \
        advertising_status = ZMK_ADV_CONN;                                                         \
    }

static int enable_filter = -1;
void checked_adv_stop(void) {
    int err = bt_le_adv_stop();
    advertising_status = ZMK_ADV_NONE;
    if (err) {
        LOG_ERR("Failed to stop advertising (err %d)", err);
    }
    return;
}

void checked_dir_adv(void) {
    char addr_buf[BT_ADDR_LE_STR_LEN];
    bt_addr_le_t *addr = zmk_ble_active_profile_addr();
    struct bt_le_adv_param adv_param;
    adv_param = *BT_LE_ADV_CONN_DIR(addr);
    adv_param.id = adv_bt_id;
    if (profiles[active_profile].is_rpa) {
        adv_param.options |= BT_LE_ADV_OPT_DIR_ADDR_RPA;
        LOG_INF("dir add adv opt: RPA");
    }
    if (!IS_ENABLED(CONFIG_BT_PRIVACY)) {
        adv_param.options |= BT_LE_ADV_OPT_USE_IDENTITY;
        LOG_INF("dir add adv opt: IDENTITY,id:%d", adv_bt_id);
    }
    int err = bt_le_adv_start(&adv_param, NULL, 0, NULL, 0);
    if (err) {
        LOG_ERR("Directed advertising failed to start:%d\n", err);
        return;
    }
    bt_addr_le_to_str(addr, addr_buf, BT_ADDR_LE_STR_LEN);
    LOG_INF("Direct advertising to %s started,prof:%d,id:%d\n", addr_buf, active_profile,
            profiles[active_profile].bt_id);
    advertising_status = ZMK_ADV_DIR;
}

void checked_open_adv(void) {
    struct bt_le_adv_param adv_param;
    adv_param = *ZMK_ADV_CONN_NAME;
    adv_param.id = adv_bt_id;

    adv_param.options |= BT_LE_ADV_OPT_USE_IDENTITY;
    LOG_INF("adv opt: IDENTITY,id:%d", adv_bt_id);
    if (enable_filter == 0) {
        adv_param.options |= BT_LE_ADV_OPT_FILTER_SCAN_REQ;
        adv_param.options |= BT_LE_ADV_OPT_FILTER_CONN;
        LOG_INF("adv opt: FILTER");
    }
    int err;
    if(adv_state == ZMK_ADV_RECONN)
    {
        err = bt_le_adv_start(&adv_param, zmk_ble_reconn_ad, ARRAY_SIZE(zmk_ble_reconn_ad), NULL, 0);
    }
    else
    {
        if(zmk_keymap_highest_layer_active()>=2)
        {
            err=bt_le_adv_start(&adv_param, zmk_ble_ad_win, ARRAY_SIZE(zmk_ble_ad_win), zmk_ble_ad_sr, ARRAY_SIZE(zmk_ble_ad_sr));
        }
        else
            err = bt_le_adv_start(&adv_param, zmk_ble_ad, ARRAY_SIZE(zmk_ble_ad), NULL, 0);
    }
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return;
    }
    advertising_status = ZMK_ADV_CONN;
}

int update_advertising() {
    // int err = 0;
    // bt_addr_le_t *addr;
    enable_filter = -1;
    keyboad_led_set_onoff(0);
    if (desired_adv == ZMK_ADV_RECONN) {
        if (bond_check(profiles[active_profile].bt_id)) {
            desired_adv = ZMK_ADV_DIR;
            enable_filter = setup_accept_list();
            blue_led_set_state( LED_PEER_STATE_RECONN);
        } else {
            desired_adv = ZMK_ADV_CONN;
            blue_led_set_state( LED_PEER_STATE_RECONN);
        }
    } else if (desired_adv == ZMK_ADV_PAIR) {
        desired_adv = ZMK_ADV_CONN;
        blue_led_set_state( LED_PEER_STATE_PAIR);
        k_work_cancel_delayable(&sleep_work);
    }

    LOG_DBG("advertising from %d to %d,adv id:%d", advertising_status, desired_adv, adv_bt_id);

    switch (desired_adv + CURR_ADV(advertising_status)) {
    case ZMK_ADV_NONE + CURR_ADV(ZMK_ADV_DIR):
    case ZMK_ADV_NONE + CURR_ADV(ZMK_ADV_CONN):
        checked_adv_stop();
        break;
    case ZMK_ADV_DIR + CURR_ADV(ZMK_ADV_DIR):
    case ZMK_ADV_DIR + CURR_ADV(ZMK_ADV_CONN):
        checked_adv_stop();
        checked_dir_adv();
        break;
    case ZMK_ADV_DIR + CURR_ADV(ZMK_ADV_NONE):
        checked_dir_adv();
        break;
    case ZMK_ADV_CONN + CURR_ADV(ZMK_ADV_CONN):
    case ZMK_ADV_CONN + CURR_ADV(ZMK_ADV_DIR):
        {
            checked_adv_stop();
            checked_open_adv();
            k_timeout_t timeout = (adv_state== ZMK_ADV_PAIR) ? K_MSEC(ADV_PAIR_TIME_OUT):K_MSEC(ADV_RECONN_TIME_OUT);
            k_work_reschedule(&adv_timeout_work, timeout);
        }
        break;
    case ZMK_ADV_CONN + CURR_ADV(ZMK_ADV_NONE):
        {
            checked_open_adv();
            k_timeout_t timeout = (adv_state== ZMK_ADV_PAIR) ? K_MSEC(ADV_PAIR_TIME_OUT):K_MSEC(ADV_RECONN_TIME_OUT);
            k_work_reschedule(&adv_timeout_work, timeout);
        }
        break;
    }

    return 0;
};

static struct k_work_delayable factory_recover_work;
static void factory_recover_work_cb(struct k_work *work)
{
    static uint8_t state =0;
    if(state==0)
    {
        led_recover();
        k_work_reschedule(&factory_recover_work, K_MSEC(2800));
        state++;
    }
    else
    {
        k_msleep(200);
        sys_reboot(0);
    }
}
void zmk_factory_recover(void)
{
    if(get_current_transport()==ZMK_TRANSPORT_BLE)
    {
        zmk_ble_disconn_active_profile();    
        k_msleep(100);
        bt_le_adv_stop();
        zmk_ble_clear_bonds();
    }
    else
    {
        zmk_24g_endpoint_disconnect();
        set_delay_clear_bonds(1);
    }
#if CONFIG_ZMK_LAUNCHER
    void via_ee_delete(void);
    via_ee_delete();
#endif    
    settings_delete("fn_exchange");

    
    k_work_init_delayable(&factory_recover_work, factory_recover_work_cb);
    k_work_reschedule(&factory_recover_work, K_MSEC(200));
}
int zmk_ble_clear_bonds(void) 
{
    char setting_name[20];
    LOG_DBG("");
    
    int i;
    for(i=1;i<CONFIG_BT_ID_MAX;i++)
    {
       int ret= bt_id_reset(i, inc_bt_addr(i), NULL); 
       LOG_DBG("bt id reset:%d",ret);
    }

    for(i=0;i<ZMK_BLE_PROFILE_COUNT;i++)
    {
        sprintf(setting_name, "ble/profiles/%d", i);
        settings_delete(setting_name);
    }
    settings_delete("ble/active_profile");


    return 0;
};


int zmk_ble_active_profile_index() { return active_profile; }

#if IS_ENABLED(CONFIG_SETTINGS)
static void ble_save_profile_work(struct k_work *work) {
    LOG_INF("-->ble_save_profile");
    settings_save_one("ble/active_profile", &active_profile, sizeof(active_profile));
}

static struct k_work_delayable ble_save_work;
#endif

static int ble_save_profile() {
#if IS_ENABLED(CONFIG_SETTINGS)
    return k_work_reschedule(&ble_save_work, K_MSEC(2500));
#else
    return 0;
#endif
}
int get_empty_bt_id(void) {
    uint8_t ret = -1;
    uint8_t id;
    for (id = 1; id < CONFIG_BT_ID_MAX; id++) { //not use id=0;
        uint8_t used = 0;
        for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
            if ((id == profiles[i].bt_id) && (profiles[i].bt_id != 0x0f)) {
                used = 1;
                break;
            }
        }
        if (!used) {
            ret = id;
            break;
        }
    }
    LOG_INF("get empty id:%d", ret);
    return ret;
}
int zmk_ble_prof_pair_start(uint8_t index) {
    if (index >= ZMK_BLE_PROFILE_COUNT) {
        return -ERANGE;
    }
    if(get_current_transport()==ZMK_TRANSPORT_24G)
    {
        if(index ==3)
            zmk_24g_pair();
        return 0;
    }
    if(get_current_transport()!=ZMK_TRANSPORT_BLE || (index>=3))
    {
        return -ENOTSUP;
    }
    if (zmk_ble_active_profile_is_connected()) // wait for disconnect!
    {
        struct bt_conn *conn = NULL;
        bt_addr_le_t *addr = zmk_ble_active_profile_addr();
        LOG_INF("wait for disconnect %d", active_profile);
        conn = bt_conn_lookup_addr_le(profiles[active_profile].bt_id, addr);
        if (conn != NULL) {
            bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            bt_conn_unref(conn);
        }
        switch_state.adv_type = ZMK_ADV_PAIR;
        switch_state.index = index;
        return -ERANGE;
    }

    LOG_DBG("profile %d,pairing", index);
    if (active_profile != index) {
        active_profile = index;
        ble_save_profile();
        raise_profile_changed_event();
    }

    if ((profiles[active_profile].bt_id == 0x0f) ||
        (bond_check(profiles[active_profile].bt_id) > 0)) {
        int id = get_empty_bt_id();
        if (id >= 0) {
            adv_bt_id = id;
        } else {
            LOG_ERR("bt id get failed");
        }
    } else
        adv_bt_id = profiles[active_profile].bt_id;

    profiles[active_profile].is_rpa = 0;
    LOG_INF("in pair,prof:%d,adv bt id:%d", active_profile, adv_bt_id);
    print_device_addr(adv_bt_id);

    desired_adv = ZMK_ADV_PAIR;
    adv_state =ZMK_ADV_PAIR;
    k_work_submit(&update_advertising_work);

    return 0;
}
int zmk_ble_prof_select(uint8_t index) {
    if (index >= ZMK_BLE_PROFILE_COUNT) {
        return -ERANGE;
    }
    if(get_current_transport() == ZMK_TRANSPORT_24G)
    {
        if(index ==3)
        {
          zmk_24g_reconn();
        }
        return 0;
    }
    if(get_current_transport()!=ZMK_TRANSPORT_BLE || (index>=3))
    {
        return -ENOTSUP;
    }
    LOG_DBG("active profile %d need to :%d", active_profile, index);
    if (active_profile == index && zmk_ble_active_profile_is_connected()) {
        return 0;
    }

    if (zmk_ble_active_profile_is_connected()) // wait for disconnect!
    {
        struct bt_conn *conn = NULL;
        bt_addr_le_t *addr = zmk_ble_active_profile_addr();
        LOG_INF("wait for disconnect %d", active_profile);
        conn = bt_conn_lookup_addr_le(profiles[active_profile].bt_id, addr);
        if (conn != NULL) {
            bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            bt_conn_unref(conn);
        }

        switch_state.adv_type = ZMK_ADV_RECONN;
        switch_state.index = index;
        return -ERANGE;
    }
    if(active_profile !=index)
    {
        active_profile = index;
        ble_save_profile();
    }

    if ((profiles[active_profile].bt_id == 0x0f)) {
        int id = get_empty_bt_id();
        if (id >= 0) {
            adv_bt_id = id;
            profiles[active_profile].is_rpa = 0;
            LOG_INF("prof:%d,bt id:%d", active_profile, adv_bt_id);
        } else {
            LOG_ERR("bt id get failed");
        }
    } else
        adv_bt_id = profiles[active_profile].bt_id;

    print_device_addr(adv_bt_id);

    desired_adv = ZMK_ADV_RECONN;
    adv_state = ZMK_ADV_RECONN;
    k_work_submit(&update_advertising_work);

    // raise_profile_changed_event();

    return 0;
};

int zmk_ble_prof_next() {
    LOG_DBG("");
    return zmk_ble_prof_select((active_profile + 1) % ZMK_BLE_PROFILE_COUNT);
};

int zmk_ble_prof_prev() {
    LOG_DBG("");
    return zmk_ble_prof_select((active_profile + ZMK_BLE_PROFILE_COUNT - 1) %
                               ZMK_BLE_PROFILE_COUNT);
};

bt_addr_le_t *zmk_ble_active_profile_addr() { return &profiles[active_profile].peer; }

char *zmk_ble_active_profile_name() { return profiles[active_profile].name; }

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

int zmk_ble_put_peripheral_addr(const bt_addr_le_t *addr) {
    for (int i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT; i++) {
        // If the address is recognized and already stored in settings, return
        // index and no additional action is necessary.
        if (bt_addr_le_cmp(&peripheral_addrs[i], addr) == 0) {
            LOG_DBG("Found existing peripheral address in slot %d", i);
            return i;
        } else {
            char addr_str[BT_ADDR_LE_STR_LEN];
            bt_addr_le_to_str(&peripheral_addrs[i], addr_str, sizeof(addr_str));
            LOG_DBG("peripheral slot %d occupied by %s", i, addr_str);
        }

        // If the peripheral address slot is open, store new peripheral in the
        // slot and return index. This compares against BT_ADDR_LE_ANY as that
        // is the zero value.
        if (bt_addr_le_cmp(&peripheral_addrs[i], BT_ADDR_LE_ANY) == 0) {
            char addr_str[BT_ADDR_LE_STR_LEN];
            bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
            LOG_DBG("Storing peripheral %s in slot %d", addr_str, i);
            bt_addr_le_copy(&peripheral_addrs[i], addr);

            char setting_name[32];
            sprintf(setting_name, "ble/peripheral_addresses/%d", i);
            settings_save_one(setting_name, addr, sizeof(bt_addr_le_t));

            return i;
        }
    }

    // The peripheral does not match a known peripheral and there is no
    // available slot.
    return -ENOMEM;
}

#endif /* IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) */

#if IS_ENABLED(CONFIG_SETTINGS)

static int ble_profiles_handle_set(const char *name, size_t len, settings_read_cb read_cb,
                                   void *cb_arg) {
    const char *next;

    LOG_DBG("Setting BLE value %s", name);

    if (settings_name_steq(name, "profiles", &next) && next) {
        char *endptr;
        uint8_t idx = strtoul(next, &endptr, 10);
        if (*endptr != '\0') {
            LOG_WRN("Invalid profile index: %s", next);
            return -EINVAL;
        }

        if (len != sizeof(struct zmk_ble_profile)) {
            LOG_ERR("Invalid profile size (got %d expected %d)", len,
                    sizeof(struct zmk_ble_profile));
            return -EINVAL;
        }

        if (idx >= ZMK_BLE_PROFILE_COUNT) {
            LOG_WRN("Profile address for index %d is larger than max of %d", idx,
                    ZMK_BLE_PROFILE_COUNT);
            return -EINVAL;
        }

        int err = read_cb(cb_arg, &profiles[idx], sizeof(struct zmk_ble_profile));
        if (err <= 0) {
            LOG_ERR("Failed to handle profile address from settings (err %d)", err);
            return err;
        }

        char addr_str[BT_ADDR_LE_STR_LEN];
        bt_addr_le_to_str(&profiles[idx].peer, addr_str, sizeof(addr_str));

        LOG_DBG("Loaded %s address for profile %d", addr_str, idx);
    } else if (settings_name_steq(name, "active_profile", &next) && !next) {
        if (len != sizeof(active_profile)) {
            return -EINVAL;
        }

        int err = read_cb(cb_arg, &active_profile, sizeof(active_profile));
        if (err <= 0) {
            LOG_ERR("Failed to handle active profile from settings (err %d)", err);
            return err;
        }
    }
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    else if (settings_name_steq(name, "peripheral_addresses", &next) && next) {
        if (len != sizeof(bt_addr_le_t)) {
            return -EINVAL;
        }

        int i = atoi(next);
        if (i < 0 || i >= ZMK_SPLIT_BLE_PERIPHERAL_COUNT) {
            LOG_ERR("Failed to store peripheral address in memory");
        } else {
            int err = read_cb(cb_arg, &peripheral_addrs[i], sizeof(bt_addr_le_t));
            if (err <= 0) {
                LOG_ERR("Failed to handle peripheral address from settings (err %d)", err);
                return err;
            }
        }
    }
#endif

    return 0;
};

struct settings_handler profiles_handler = {.name = "ble", .h_set = ble_profiles_handle_set};
#endif /* IS_ENABLED(CONFIG_SETTINGS) */

static bool is_conn_active_profile(const struct bt_conn *conn) {
    return bt_addr_le_cmp(bt_conn_get_dst(conn), &profiles[active_profile].peer) == 0;
}

static void connected(struct bt_conn *conn, uint8_t err) {
    char addr[BT_ADDR_LE_STR_LEN];
    struct bt_conn_info info;

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_DBG("Connected thread: %p,conn id:%d", k_current_get(), conn->id);
    if (err) {
        if (err == BT_HCI_ERR_ADV_TIMEOUT) {
            printk("Direct advertising to %s timed out\n", addr);
            advertising_status = ZMK_ADV_NONE;
            desired_adv = ZMK_ADV_CONN;
            k_work_submit(&update_advertising_work);

        } else {
            printk("Failed to connect to %s (%u)\n", addr, err);
        }
        return;
    }
    bt_conn_get_info(conn, &info);

    if (info.role != BT_CONN_ROLE_PERIPHERAL) {
        LOG_DBG("SKIPPING FOR ROLE %d", info.role);
        return;
    }

    advertising_status = ZMK_ADV_NONE;
    adv_state = advertising_status;
    k_work_cancel_delayable(&adv_timeout_work);
    blue_led_set_state( LED_PEER_STATE_CONNECTED);

    profiles[active_profile].connected=1;

    LOG_DBG("Connected %s", addr);
    int ret = bt_conn_set_security(conn, BT_SECURITY_L2);
    if (ret) {
        LOG_ERR("Failed to set security,err:%d", ret); //,id:%d", ret,conn->id);
    }

    if (is_conn_active_profile(conn)) {
        LOG_DBG("Active profile connected");
        k_work_submit(&raise_profile_changed_event_work);
    }
#if EN_UPATE_PARAM_DYNAMIC    
    k_work_reschedule(&ble_active_work,K_MSEC(15000));
#endif   
    k_work_reschedule(&update_conn_param_work,K_MSEC(5000));  
}

static void disconnected(struct bt_conn *conn, uint8_t reason) {
    char addr[BT_ADDR_LE_STR_LEN];
    struct bt_conn_info info;
    if(!zmk_usb_is_powered())
        keyboad_led_set_onoff(0);
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_DBG("Disconnected from %s (reason 0x%02x)", addr, reason);

    bt_conn_get_info(conn, &info);

    if (info.role != BT_CONN_ROLE_PERIPHERAL) {
        LOG_DBG("SKIPPING FOR ROLE %d", info.role);
        return;
    }
    profiles[active_profile].connected=0;
    // We need to do this in a work callback, otherwise the advertising update will still see the
    // connection for a profile as active, and not start advertising yet.
    // k_work_submit(&update_advertising_work);
    if ((reason != BT_HCI_ERR_REMOTE_USER_TERM_CONN) &&
        (reason != BT_HCI_ERR_LOCALHOST_TERM_CONN) && (reason != BT_HCI_ERR_REMOTE_POWER_OFF)) {
        advertising_start();
    }
    else
    {
        if(switch_state.adv_type==0)
            set_state(ZMK_ACTIVITY_SLEEP);
    }

    if (is_conn_active_profile(conn)) {
        LOG_DBG("Active profile disconnected");
        k_work_submit(&raise_profile_changed_event_work);
    }
    k_work_cancel_delayable(&update_conn_param_work);
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err) {
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (!err) {
        LOG_DBG("Security changed: %s level %u", addr, level);
        if (level >= BT_SECURITY_L2) {
            
            if (bt_addr_le_is_rpa(bt_conn_get_dst(conn))) {
                profiles[active_profile].is_rpa = 1;
                LOG_INF("host:%s is rpa", addr);
            }
            if(!profiles[active_profile].bonded)
            {
                profiles[active_profile].bonded =1;
                set_profile_address(active_profile, bt_conn_get_dst(conn));
                LOG_DBG("NOT BOND,SAVE AGAIN");
            }
        }
    } else {
        LOG_ERR("Security failed: %s level %u err %d", addr, level, err);
        if(err ==BT_SECURITY_ERR_PIN_OR_KEY_MISSING) 
        {
            // bt_unpair(conn->id, bt_conn_get_dst(conn));
            bt_id_reset(conn->id,inc_bt_addr(conn->id),NULL);
            profiles[active_profile].is_rpa = 0;
            profiles[active_profile].bonded = 0;
            set_profile_address(active_profile, BT_ADDR_LE_ANY);
            LOG_INF("clear pair info");
            blue_led_set_state( LED_PEER_STATE_FAILE);
        }
    }
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency,
                             uint16_t timeout) {
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_DBG("%s: interval %d latency %d timeout %d", addr, interval, latency, timeout);
#if EN_UPATE_PARAM_DYNAMIC
    k_work_cancel_delayable(&ble_param_update_work);
    ble_param_update_try_count=0;
#endif     
}

static struct bt_conn_cb conn_callbacks = {
    .connected = connected,
    .disconnected = disconnected,
    .security_changed = security_changed,
    .le_param_updated = le_param_updated,
};


static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey) {
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_DBG("Passkey for %s: %06u", addr, passkey);
}


#if IS_ENABLED(CONFIG_ZMK_BLE_PASSKEY_ENTRY)

static void auth_passkey_entry(struct bt_conn *conn) {
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_DBG("Passkey entry requested for %s", addr);
    ring_buf_reset(&passkey_entries);
    auth_passkey_entry_conn = bt_conn_ref(conn);
}

#endif

static void auth_cancel(struct bt_conn *conn) {
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

#if IS_ENABLED(CONFIG_ZMK_BLE_PASSKEY_ENTRY)
    if (auth_passkey_entry_conn) {
        bt_conn_unref(auth_passkey_entry_conn);
        auth_passkey_entry_conn = NULL;
    }

    ring_buf_reset(&passkey_entries);
#endif

    LOG_DBG("Pairing cancelled: %s", addr);
}

static enum bt_security_err auth_pairing_accept(struct bt_conn *conn,
                                                const struct bt_conn_pairing_feat *const feat) {
    struct bt_conn_info info;
    bt_conn_get_info(conn, &info);

    LOG_DBG("role %d, open? %s", info.role, zmk_ble_active_profile_is_open() ? "yes" : "no");
    if (info.role == BT_CONN_ROLE_PERIPHERAL && !zmk_ble_active_profile_is_open()) {
        LOG_WRN("Rejecting pairing request to taken profile %d", active_profile);
        return BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
    }

    return BT_SECURITY_ERR_SUCCESS;
};

static void auth_pairing_complete(struct bt_conn *conn, bool bonded) {
    struct bt_conn_info info;
    char addr[BT_ADDR_LE_STR_LEN];
    const bt_addr_le_t *dst = bt_conn_get_dst(conn);

    bt_addr_le_to_str(dst, addr, sizeof(addr));
    bt_conn_get_info(conn, &info);

    if (info.role != BT_CONN_ROLE_PERIPHERAL) {
        LOG_DBG("SKIPPING FOR ROLE %d", info.role);
        return;
    }

    // if (!zmk_ble_active_profile_is_open()) {
    //     LOG_ERR("Pairing completed but current profile is not open: %s", addr);
    //     bt_unpair(BT_ID_DEFAULT, dst);
    //     return;
    // }
    if (profiles[active_profile].bt_id != conn->id) 
    {
        // if (profiles[active_profile].bt_id != 0x0f) 
        // {
        //     if(conn->id==0)
        //     {
        //         LOG_DBG("-->unpair bt 0\n");
        //         bt_unpair(conn->id, bt_conn_get_dst(conn));
        //     }
        //     else
        //     {
        //         bt_id_reset(profiles[active_profile].bt_id, inc_bt_addr(profiles[active_profile].bt_id), NULL);
        //     }
        //     LOG_INF("reset bt id:%d,new:%d", profiles[active_profile].bt_id, conn->id);
        // }
        profiles[active_profile].bt_id = conn->id;
    }

    profiles[active_profile].bonded=1;
    set_profile_address(active_profile, dst);
    // due to CONFIG_BT_ID_UNPAIR_MATCHING_BONDS
    copy_profile_to_same_peer(dst);
};

static struct bt_conn_auth_cb zmk_ble_auth_cb_display = {
    .pairing_accept = auth_pairing_accept,
    .passkey_display = auth_passkey_display,

#if IS_ENABLED(CONFIG_ZMK_BLE_PASSKEY_ENTRY)
    .passkey_entry = auth_passkey_entry,
#endif
    .cancel = auth_cancel,
};


static struct bt_conn_auth_info_cb zmk_ble_auth_info_cb_display = {
    .pairing_complete = auth_pairing_complete,
};

static void zmk_ble_ready(int err) {
    LOG_DBG("ready? %d", err);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return;
    }

    // update_advertising();
    // advertising_start();
    profiles[active_profile].connected=0;
    // settings_subsys_init();

    // err = settings_register(&profiles_handler);
    // if (err) {
    //     LOG_ERR("Failed to setup the profile settings handler (err %d)", err);
    //     return ;
    // }

    // settings_load_subtree("ble");
    advertising_start();
}

// static int zmk_ble_init(const struct device *_arg)
int zmk_ble_init(const struct device *_arg) {

    // uint8_t addr[]={0x31,0x10,0x23,0x26,0x2c,0xdc};
    // int ret=bt_ctlr_set_public_addr(addr);
    // LOG_INF("bt_ctlr_set_public_addr,ret:%d\n",ret);
    static bool inited =false;
    if(bat_is_shutdown()) return 0;
    if(inited) 
    {
        advertising_start();
        return 0;
    }
    inited =true;

    for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++)
        profiles[i].bt_id = 0x0f;
    int err = bt_enable(NULL);

    if (err) {
        LOG_ERR("BLUETOOTH FAILED (%d)", err);
        return err;
    }
     // leds_init(NULL);

#if IS_ENABLED(CONFIG_SETTINGS)
    LOG_INF("settings init");
    settings_subsys_init();
    

    err = settings_register(&profiles_handler);
    if (err) {
        LOG_ERR("Failed to setup the profile settings handler (err %d)", err);
        return err;
    }

    k_work_init_delayable(&ble_save_work, ble_save_profile_work);

    settings_load_subtree("ble");
    settings_load_subtree("bt");
    load_identities();
    
    set_dis_value();
    // advertising_start();

    if(delay_clear_bonds())
    {
        zmk_ble_clear_bonds();
        set_delay_clear_bonds(0);
    }

#endif

#if IS_ENABLED(CONFIG_ZMK_BLE_CLEAR_BONDS_ON_START)
    LOG_WRN("Clearing all existing BLE bond information from the keyboard");

    bt_unpair(BT_ID_DEFAULT, NULL);

    for (int i = 0; i < 8; i++) {
        char setting_name[15];
        sprintf(setting_name, "ble/profiles/%d", i);

        err = settings_delete(setting_name);
        if (err) {
            LOG_ERR("Failed to delete setting: %d", err);
        }
    }

    // Hardcoding a reasonable hardcoded value of peripheral addresses
    // to clear so we properly clear a split central as well.
    for (int i = 0; i < 8; i++) {
        char setting_name[32];
        sprintf(setting_name, "ble/peripheral_addresses/%d", i);

        err = settings_delete(setting_name);
        if (err) {
            LOG_ERR("Failed to delete setting: %d", err);
        }
    }

#endif // IS_ENABLED(CONFIG_ZMK_BLE_CLEAR_BONDS_ON_START)

    bt_conn_cb_register(&conn_callbacks);
    // bt_conn_auth_cb_register(&zmk_ble_auth_cb_display);
    bt_conn_auth_info_cb_register(&zmk_ble_auth_info_cb_display);

    zmk_ble_ready(0);

    // enable_llpm_mode();

    return 0;
}

#if IS_ENABLED(CONFIG_ZMK_BLE_PASSKEY_ENTRY)

static bool zmk_ble_numeric_usage_to_value(const zmk_key_t key, const zmk_key_t one,
                                           const zmk_key_t zero, uint8_t *value) {
    if (key < one || key > zero) {
        return false;
    }

    *value = (key == zero) ? 0 : (key - one + 1);
    return true;
}

static int zmk_ble_handle_key_user(struct zmk_keycode_state_changed *event) {
    zmk_key_t key = event->keycode;

    LOG_DBG("key %d", key);

    if (!auth_passkey_entry_conn) {
        LOG_DBG("No connection for passkey entry");
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (!event->state) {
        LOG_DBG("Key released, ignoring");
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (key == HID_USAGE_KEY_KEYBOARD_ESCAPE) {
        bt_conn_auth_cancel(auth_passkey_entry_conn);
        return ZMK_EV_EVENT_HANDLED;
    }

    if (key == HID_USAGE_KEY_KEYBOARD_RETURN || key == HID_USAGE_KEY_KEYBOARD_RETURN_ENTER) {
        uint8_t digits[PASSKEY_DIGITS];
        uint32_t count = ring_buf_get(&passkey_entries, digits, PASSKEY_DIGITS);

        uint32_t passkey = 0;
        for (int i = 0; i < count; i++) {
            passkey = (passkey * 10) + digits[i];
        }

        LOG_DBG("Final passkey: %d", passkey);
        bt_conn_auth_passkey_entry(auth_passkey_entry_conn, passkey);
        bt_conn_unref(auth_passkey_entry_conn);
        auth_passkey_entry_conn = NULL;
        return ZMK_EV_EVENT_HANDLED;
    }

    uint8_t val;
    if (!(zmk_ble_numeric_usage_to_value(key, HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION,
                                         HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS, &val) ||
          zmk_ble_numeric_usage_to_value(key, HID_USAGE_KEY_KEYPAD_1_AND_END,
                                         HID_USAGE_KEY_KEYPAD_0_AND_INSERT, &val))) {
        LOG_DBG("Key not a number, ignoring");
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ring_buf_space_get(&passkey_entries) <= 0) {
        uint8_t discard_val;
        ring_buf_get(&passkey_entries, &discard_val, 1);
    }
    ring_buf_put(&passkey_entries, &val, 1);
    LOG_DBG("value entered: %d, digits collected so far: %d", val,
            ring_buf_size_get(&passkey_entries));

    return ZMK_EV_EVENT_HANDLED;
}
#endif /* IS_ENABLED(CONFIG_ZMK_BLE_PASSKEY_ENTRY) */

static int zmk_ble_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *kc_state;
    
    kc_state = as_zmk_keycode_state_changed(eh);

    if ((kc_state != NULL) && !bat_is_shutdown()) {
        if(kc_state->state) {
            LOG_DBG(".");
            if(get_current_transport()==ZMK_TRANSPORT_BLE){
                LOG_DBG("profiles:%d,bonded:%d,connected:%d",active_profile,profiles[active_profile].bonded,profiles[active_profile].connected);
                if(!profiles[active_profile].connected  )
                {
                    advertising_start();
                }
#if EN_UPATE_PARAM_DYNAMIC                
                else
                {
                    ble_active_handler();
                }
#endif                 
            }
            else if(get_current_transport()==ZMK_TRANSPORT_24G)
            {
                zmk_24g_reconn();
            }
        }
    }

    return 0;
}

ZMK_LISTENER(zmk_ble, zmk_ble_listener);
ZMK_SUBSCRIPTION(zmk_ble, zmk_keycode_state_changed);


// SYS_INIT(zmk_ble_init, APPLICATION, CONFIG_ZMK_BLE_INIT_PRIORITY);

uint8_t zmk_ble_get_active_profile(void) { return active_profile; }

uint8_t zmk_ble_get_active_profile_bt_id(void) { return profiles[active_profile].bt_id; }

uint8_t zmk_ble_is_connected(void) {return profiles[active_profile].connected;}

void zmk_ble_disconn_active_profile(void) {
    if (zmk_ble_active_profile_is_connected()) // wait for disconnect!
    {
        struct bt_conn *conn;
        bt_addr_le_t *addr = zmk_ble_active_profile_addr();
        LOG_INF("try for disconnect %d", active_profile);
        conn = bt_conn_lookup_addr_le(profiles[active_profile].bt_id, addr);
        if (conn != NULL) {
            bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            bt_conn_unref(conn);
        }
    }
}
void zmk_ble_endpoint_disconnect(void)
{
    if(get_current_transport()==ZMK_TRANSPORT_BLE)
    {
        struct bt_conn *conn = NULL;
        bt_addr_le_t *addr = zmk_ble_active_profile_addr();

        conn = bt_conn_lookup_addr_le(profiles[active_profile].bt_id, addr);
        if (conn != NULL) {
            bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            bt_conn_unref(conn);
        }
        k_work_reschedule(&adv_timeout_work,K_MSEC(20));
    }
}


static bt_addr_le_t inced_bt_addr;
bt_addr_le_t * inc_bt_addr(uint8_t index)
{
    bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
    size_t count = ARRAY_SIZE(addrs);
    bt_id_get(addrs, &count);
    LOG_DBG("index:%d,count:%d",index,count);
    if(index <count)
    {
        char addr_str[30] = {0};
        bt_addr_le_to_str(&addrs[index], addr_str, sizeof(addr_str));
        LOG_INF("old Device addr:%s\n", addr_str);
        addrs[index].a.val[0] +=1;
        bt_addr_le_to_str(&addrs[index], addr_str, sizeof(addr_str));
        LOG_INF("Device addr:%s\n", addr_str);
        memcpy(&inced_bt_addr,&addrs[index],sizeof(bt_addr_le_t));
        return &inced_bt_addr;
    }
    else
    {
        return NULL;
    }
}

void print_device_addr(uint8_t id)
{
    bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
    size_t count = ARRAY_SIZE(addrs);
    bt_id_get(addrs, &count);
    if(id <count)
    {
        char addr_str[64] = {0};

        bt_addr_le_to_str(&addrs[id], addr_str, sizeof(addr_str));
        LOG_INF("Device addr:%s\n", addr_str);

    }
    else
    {
        LOG_INF("Device addr NULL");
    }
}

void set_dis_value(void)
{
    int rc;
    char sn[20]="SN2024013114230078";
    rc = load_immediate_value("bt/dis/serial", sn, strlen(sn));
    if (rc == -ENOENT) {
        uint8_t deviceid[4]={0};
        for (uint8_t i=0; i< sizeof(deviceid); i++)
        {
             deviceid[i] = (uint8_t) (NRF_FICR->DEVICEID[0]>>(i*8));
        }   
        sprintf(&sn[10],"%02x%02x%02x%02x",deviceid[0],deviceid[1],deviceid[2],deviceid[3]);
        settings_save_one("bt/dis/serial",sn,strlen(sn));
        LOG_DBG("set sn:%s",sn);
    } 
    else
    {
        LOG_DBG("sn is:%s",sn);
    }
}
#if EN_UPATE_PARAM_DYNAMIC
int update_ble_conn_param(void)
{
    int rc=0;
    struct bt_le_conn_param *slow_param =BT_LE_CONN_PARAM(40, 40,30,400);
    struct bt_le_conn_param *active_param =BT_LE_CONN_PARAM(6, 12,30,400);
    struct bt_conn *conn;
    bt_addr_le_t *addr = zmk_ble_active_profile_addr();
    
    conn = bt_conn_lookup_addr_le(profiles[active_profile].bt_id, addr);
    LOG_DBG("conn:%p,active:%d",conn,ble_active);
    if (conn != NULL) {
        rc= bt_conn_le_param_update(conn,ble_active?active_param:slow_param);
        LOG_DBG("rc:%d",rc);
        bt_conn_unref(conn);
    }
    return rc;
}

void ble_active_work_callback(struct k_work *work)
{
    ble_active=0;
    LOG_DBG(".");
    k_work_reschedule(&ble_param_update_work,K_NO_WAIT);
}
void ble_param_update_work_callback(struct k_work *work)
{
    LOG_DBG("try_count:%d",ble_param_update_try_count);
    int rc =update_ble_conn_param();
    if(rc!=0 && rc !=-EALREADY)
    {
        if(++ble_param_update_try_count <=3)
        {
            k_work_reschedule(&ble_param_update_work,K_MSEC(1000));
        }
    }
    else
        ble_param_update_try_count =0;
}
void ble_active_handler(void)
{
    ble_active =1;
    struct bt_conn *conn;
    bt_addr_le_t *addr = zmk_ble_active_profile_addr();
    
    conn = bt_conn_lookup_addr_le(profiles[active_profile].bt_id, addr);
    if(conn !=NULL)
    {
        struct bt_conn_info info ;
        bt_conn_get_info(conn,&info);
        LOG_DBG("conn interval:%d,latency:%d,timeout:%d,count:%d",info.le.interval,info.le.latency,info.le.timeout,ble_param_update_try_count);
        if(info.le.interval>24)
        {
            if(ble_param_update_try_count==0)
            {
                //int rc=update_ble_conn_param();
                ble_param_update_try_count=1;
                //if(rc !=0 && rc != -EALREADY)
                {
                    // k_work_cancel_delayable(&ble_param_update_work);
                    k_work_reschedule(&ble_param_update_work,K_MSEC(500));
                }
            }
        }
        bt_conn_unref(conn);
        k_work_reschedule(&ble_active_work,K_MSEC(15000));
    }
}
#endif 

void update_conn_param_worker(struct k_work *work)
{
    int rc=0;
    struct bt_le_conn_param *active_param =BT_LE_CONN_PARAM(6, 12,30,400);
    struct bt_conn *conn;
    bt_addr_le_t *addr = zmk_ble_active_profile_addr();
    
    conn = bt_conn_lookup_addr_le(profiles[active_profile].bt_id, addr);
    LOG_DBG("conn:%p",conn);
    if (conn != NULL) {
        LOG_ERR("ch inteval:%d",active_param->interval_max);
        rc= bt_conn_le_param_update(conn,active_param);
        LOG_ERR("rc:%d",rc);
        bt_conn_unref(conn);
    }
}

uint8_t delay_clear_bonds(void)
{
    uint8_t delay_clear_bond=0;
    int rc =load_immediate_value("delay_clear_bond", &delay_clear_bond, sizeof(delay_clear_bond));
    if (rc == -ENOENT) {
        delay_clear_bond = 0;
        LOG_DBG("delay_clear_bond:%d,default",delay_clear_bond);
    }
    else if(rc ==0)
    {
        LOG_DBG("delay_clear_bond:%d",delay_clear_bond);
    }
    return delay_clear_bond;
}
void set_delay_clear_bonds(uint8_t set)
{
    uint8_t delay_clear_bond=set;
    LOG_ERR("set_delay_clear_bonds:%d",set);
    settings_save_one("delay_clear_bond", &delay_clear_bond, sizeof(delay_clear_bond));
}