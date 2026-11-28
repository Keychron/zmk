/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/init.h>
#include <zephyr/settings/settings.h>

#include <stdio.h>

#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <zmk/usb_hid.h>
#include <zmk/hog.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/events/ppt_conn_state_changed.h>
#include <zmk/events/endpoint_changed.h>
#if IS_ENABLED(CONFIG_ZMK_PPT)
#include <zmk/ppt/keyboard_ppt_app.h>
#endif
#include "trace.h"

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

void lowpower_settings(void);
void winlock_led_onoff(uint8_t onoff);

#undef CONFIG_SETTINGS // disable save settings;
uint8_t *get_keyboard_report(size_t *len);
#define USE_HARDWARE_SELECT_TRANSPORT
#ifdef USE_HARDWARE_SELECT_TRANSPORT
enum zmk_transport get_hardware_select_transport(void);
static void disc_worker(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(disc_work, disc_worker);
#endif

#define DEFAULT_TRANSPORT                                                                          \
    COND_CODE_1(IS_ENABLED(CONFIG_ZMK_BLE), (ZMK_TRANSPORT_BLE), (ZMK_TRANSPORT_USB))

static struct zmk_endpoint_instance current_instance = {};
static enum zmk_transport preferred_transport =
    ZMK_TRANSPORT_USB; /* Used if multiple endpoints are ready */

static void update_current_endpoint(void);

#if IS_ENABLED(CONFIG_SETTINGS)
static void endpoints_save_preferred_work(struct k_work *work) {
    settings_save_one("endpoints/preferred", &preferred_transport, sizeof(preferred_transport));
}

static struct k_work_delayable endpoints_save_work;
#endif

static int endpoints_save_preferred(void) {
#if IS_ENABLED(CONFIG_SETTINGS)
    return k_work_reschedule(&endpoints_save_work, K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
#else
    return 0;
#endif
}

bool zmk_endpoint_instance_eq(struct zmk_endpoint_instance a, struct zmk_endpoint_instance b) {
    if (a.transport != b.transport) {
        return false;
    }

    switch (a.transport) {
    case ZMK_TRANSPORT_USB:
        return true;

    case ZMK_TRANSPORT_BLE:
        return a.ble.profile_index == b.ble.profile_index;

    case ZMK_TRANSPORT_PPT:
        return true;
    }

    LOG_ERR("Invalid transport %d", a.transport);
    return false;
}

int zmk_endpoint_instance_to_str(struct zmk_endpoint_instance endpoint, char *str, size_t len) {
    switch (endpoint.transport) {
    case ZMK_TRANSPORT_USB:
        return snprintf(str, len, "USB");

    case ZMK_TRANSPORT_BLE:
        return snprintf(str, len, "BLE:%d", endpoint.ble.profile_index);

    case ZMK_TRANSPORT_PPT:
        return snprintf(str, len, "PPT");

    default:
        return snprintf(str, len, "Invalid");
    }
}

#define INSTANCE_INDEX_OFFSET_USB 0
#define INSTANCE_INDEX_OFFSET_BLE ZMK_ENDPOINT_USB_COUNT
#define INSTANCE_INDEX_OFFSET_PPT (INSTANCE_INDEX_OFFSET_BLE + ZMK_ENDPOINT_BLE_COUNT)

int zmk_endpoint_instance_to_index(struct zmk_endpoint_instance endpoint) {
    switch (endpoint.transport) {
    case ZMK_TRANSPORT_USB:
        return INSTANCE_INDEX_OFFSET_USB;

    case ZMK_TRANSPORT_BLE:
        return INSTANCE_INDEX_OFFSET_BLE + endpoint.ble.profile_index;

    case ZMK_TRANSPORT_PPT:
        return INSTANCE_INDEX_OFFSET_PPT;
    }

    LOG_ERR("Invalid transport %d", endpoint.transport);
    return 0;
}

int zmk_endpoints_select_transport(enum zmk_transport transport) {
    LOG_DBG("Selected endpoint transport %d", transport);

    if (preferred_transport == transport) {
        return 0;
    }

    preferred_transport = transport;

    endpoints_save_preferred();

    update_current_endpoint();

    return 0;
}

int zmk_endpoints_toggle_transport(void) {
    enum zmk_transport new_transport =
        (preferred_transport == ZMK_TRANSPORT_USB) ? ZMK_TRANSPORT_BLE : ZMK_TRANSPORT_USB;
    return zmk_endpoints_select_transport(new_transport);
}

struct zmk_endpoint_instance zmk_endpoints_selected(void) {
    return current_instance;
}
#if CONFIG_ADAPATIVE_NKRO

extern struct zmk_adapative_nkro adapative_nkro;
int zmk_usb_hid_send_report(const uint8_t *report, size_t len);
bool nkro_changed(void);
bool kb_changed(void);

int transport_send(struct zmk_hid_keyboard_report *report, uint8_t len) {
    int err = 0;
    switch (current_instance.transport) {
    case ZMK_TRANSPORT_USB: {
        err = zmk_usb_hid_send_report((uint8_t *)report, len);
        if (err) {
            LOG_ERR("FAILED TO SEND OVER USB: %d", err);
        }
    } break;
    case ZMK_TRANSPORT_PPT: {
        err = zmk_ppt_send_keyboard_report((uint8_t *)&report->body, len - 1);
        if (err) {
            LOG_ERR("FAILED TO SEND OVER PPT: %d", err);
        }
    } break;
    case ZMK_TRANSPORT_BLE: {
        err = zmk_hog_send_keyboard_report(&report->body);
        if (err) {
            LOG_ERR("FAILED TO SEND OVER HOG: %d", err);
        }
    } break;
    }
    return err;
}
static int send_keyboard_report(void) {
    extern struct zmk_adapative_nkro adapative_nkro;
    int ret = 0;
    if (zmk_usb_hid_is_boot_protocol()) {
        /* The host only understands 8-byte boot reports without report IDs, so it must not
         * get the report-ID-prefixed 6KRO report or the NKRO bitmap report. */
        bool changed = kb_changed();
        changed |= nkro_changed();
        if (!changed) {
            return 0;
        }
        if (current_instance.transport == ZMK_TRANSPORT_USB) {
            return zmk_usb_hid_send_keyboard_report();
        }
        /* Over 2.4G the 6KRO body is already in boot layout; only drop the NKRO report. */
        struct zmk_hid_keyboard_report *report = zmk_hid_get_keyboard_report();
        report->report_id = ZMK_HID_REPORT_ID_KEYBOARD;
        report->body._reserved = 0;
        return transport_send(report, CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE + 3);
    }
    if (kb_changed()) {
        struct zmk_hid_keyboard_report *report = zmk_hid_get_keyboard_report();
        report->report_id = ZMK_HID_REPORT_ID_KEYBOARD;
        report->body._reserved = 0;
        uint8_t len = CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE + 3;
        ret = transport_send(report, len);
    }
    if (nkro_changed()) {
        adapative_nkro.nkro_report.report_id = ZMK_HID_REPORT_ID_KEYBOARD_NKRO;
        adapative_nkro.nkro_report.body.modifiers = 0;
        adapative_nkro.nkro_report.body._reserved = 1;
        struct zmk_hid_keyboard_report *report = &adapative_nkro.nkro_report;
        uint8_t len = sizeof(adapative_nkro.nkro_report);
        ret = transport_send(report, len);
    }
    return ret;
}
#else
static int send_keyboard_report(void) {
    switch (current_instance.transport) {
    case ZMK_TRANSPORT_USB: {
#if IS_ENABLED(CONFIG_ZMK_USB)
        int err = zmk_usb_hid_send_keyboard_report();
        if (err) {
            LOG_ERR("FAILED TO SEND OVER USB: %d", err);
        }
        return err;
#else
        LOG_ERR("USB endpoint is not supported");
        return -ENOTSUP;
#endif /* IS_ENABLED(CONFIG_ZMK_USB) */
    }
    case ZMK_TRANSPORT_PPT: {
        size_t len = 0;
        uint8_t *p_data = get_keyboard_report(&len);
        return zmk_ppt_send_keyboard_report(
            p_data + 1, zmk_get_nkro_status() ? len - 1 : CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE + 2);
    } break;
    case ZMK_TRANSPORT_BLE: {
#if IS_ENABLED(CONFIG_ZMK_BLE)
        struct zmk_hid_keyboard_report *keyboard_report = zmk_hid_get_keyboard_report();
        int err = zmk_hog_send_keyboard_report(&keyboard_report->body);
        // int err = zmk_hog_send_keyboard_report(keyboard_report);
        if (err) {
            LOG_ERR("FAILED TO SEND OVER HOG: %d", err);
        }
        return err;
#else
        LOG_ERR("BLE HOG endpoint is not supported");
        return -ENOTSUP;
#endif /* IS_ENABLED(CONFIG_ZMK_BLE) */
    }

        //     case ZMK_TRANSPORT_PPT: {
        // #if IS_ENABLED(CONFIG_ZMK_PPT)
        //         int err = zmk_ppt_send_keyboard_report();
        //         if (err) {
        //             LOG_ERR("FAILED TO SEND OVER PPT: %d", err);
        //         }
        //         return err;
        // #else
        //         LOG_ERR("PPT endpoint is not supported");
        //         return -ENOTSUP;
        // #endif /* IS_ENABLED(CONFIG_ZMK_PPT) */
        //     }
    }

    LOG_ERR("Unhandled endpoint transport %d", current_instance.transport);
    return -ENOTSUP;
}
#endif
static int send_consumer_report(void) {
    switch (current_instance.transport) {
    case ZMK_TRANSPORT_USB: {
#if IS_ENABLED(CONFIG_ZMK_USB)
        int err = zmk_usb_hid_send_consumer_report();
        if (err) {
            LOG_ERR("FAILED TO SEND OVER USB: %d", err);
        }
        return err;
#else
        LOG_ERR("USB endpoint is not supported");
        return -ENOTSUP;
#endif /* IS_ENABLED(CONFIG_ZMK_USB) */
    }
    case ZMK_TRANSPORT_PPT: {
        return zmk_ppt_send_consumer_report((uint8_t *)&zmk_hid_get_consumer_report()->body, 2);
    }

    break;
    case ZMK_TRANSPORT_BLE: {
#if IS_ENABLED(CONFIG_ZMK_BLE)
        struct zmk_hid_consumer_report *consumer_report = zmk_hid_get_consumer_report();
        int err = zmk_hog_send_consumer_report(&consumer_report->body);
        if (err) {
            LOG_ERR("FAILED TO SEND OVER HOG: %d", err);
        }
        return err;
#else
        LOG_ERR("BLE HOG endpoint is not supported");
        return -ENOTSUP;
#endif /* IS_ENABLED(CONFIG_ZMK_BLE) */
    }

        //     case ZMK_TRANSPORT_PPT: {
        // #if IS_ENABLED(CONFIG_ZMK_PPT)
        //         int err = zmk_ppt_send_consumer_report();
        //         if (err) {
        //             LOG_ERR("FAILED TO SEND OVER ppt: %d", err);
        //         }
        //         return err;
        // #else
        //         LOG_ERR("PPT endpoint is not supported");
        //         return -ENOTSUP;
        // #endif /* IS_ENABLED(CONFIG_ZMK_PPT) */
        //     }
    }
    LOG_ERR("Unhandled endpoint transport %d", current_instance.transport);
    return -ENOTSUP;
}

int zmk_endpoints_send_report(uint16_t usage_page) {

    LOG_DBG("usage page 0x%02X", usage_page);
    switch (usage_page) {
    case HID_USAGE_KEY:
        return send_keyboard_report();

    case HID_USAGE_CONSUMER:
        return send_consumer_report();
    }

    LOG_ERR("Unsupported usage page %d", usage_page);
    return -ENOTSUP;
}

#if IS_ENABLED(CONFIG_ZMK_MOUSE)
int zmk_endpoints_send_mouse_report() {
    switch (current_instance.transport) {
    case ZMK_TRANSPORT_USB: {
#if IS_ENABLED(CONFIG_ZMK_USB)
        int err = zmk_usb_hid_send_mouse_report();
        if (err) {
            LOG_ERR("FAILED TO SEND OVER USB: %d", err);
        }
        return err;
#else
        LOG_ERR("USB endpoint is not supported");
        return -ENOTSUP;
#endif /* IS_ENABLED(CONFIG_ZMK_USB) */
    }

    case ZMK_TRANSPORT_BLE: {
#if IS_ENABLED(CONFIG_ZMK_BLE)
        struct zmk_hid_mouse_report *mouse_report = zmk_hid_get_mouse_report();
        int err = zmk_hog_send_mouse_report(&mouse_report->body);
        if (err) {
            LOG_ERR("FAILED TO SEND OVER HOG: %d", err);
        }
        return err;
#else
        LOG_ERR("BLE HOG endpoint is not supported");
        return -ENOTSUP;
#endif /* IS_ENABLED(CONFIG_ZMK_BLE) */
    }
    case ZMK_TRANSPORT_PPT: {
#if IS_ENABLED(CONFIG_ZMK_PPT)
        struct zmk_hid_mouse_report *mouse_report = zmk_hid_get_mouse_report();
        int err =
            zmk_ppt_send_mouse_report((uint8_t *)&mouse_report->body, sizeof(mouse_report->body));
        if (err) {
            LOG_ERR("FAILED TO SEND OVER HOG: %d", err);
        }
        return err;
#else
        LOG_ERR("BLE HOG endpoint is not supported");
        return -ENOTSUP;
#endif /* IS_ENABLED(CONFIG_ZMK_BLE) */
    }
    }

    LOG_ERR("Unhandled endpoint transport %d", current_instance.transport);
    return -ENOTSUP;
}
#endif // IS_ENABLED(CONFIG_ZMK_MOUSE)

#if IS_ENABLED(CONFIG_SETTINGS)

static int endpoints_handle_set(const char *name, size_t len, settings_read_cb read_cb,
                                void *cb_arg) {
    LOG_DBG("Setting endpoint value %s", name);

    if (settings_name_steq(name, "preferred", NULL)) {
        if (len != sizeof(enum zmk_transport)) {
            LOG_ERR("Invalid endpoint size (got %d expected %d)", len, sizeof(enum zmk_transport));
            return -EINVAL;
        }

        int err = read_cb(cb_arg, &preferred_transport, sizeof(enum zmk_transport));
        if (err <= 0) {
            LOG_ERR("Failed to read preferred endpoint from settings (err %d)", err);
            return err;
        }

        update_current_endpoint();
    }

    return 0;
}

struct settings_handler endpoints_handler = {.name = "endpoints", .h_set = endpoints_handle_set};
#endif /* IS_ENABLED(CONFIG_SETTINGS) */
#ifndef USE_HARDWARE_SELECT_TRANSPORT
static bool is_usb_ready(void) {
#if IS_ENABLED(CONFIG_ZMK_USB)
    return zmk_usb_is_hid_ready();
#else
    return false;
#endif
}

static bool is_ble_ready(void) {
#if IS_ENABLED(CONFIG_ZMK_BLE)
    return zmk_ble_active_profile_is_connected();
#else
    return false;
#endif
}

static bool is_ppt_ready(void) {
#if IS_ENABLED(CONFIG_ZMK_PPT)
    return zmk_ppt_is_ready();
#else
    return false;
#endif
}
#endif

static enum zmk_transport get_selected_transport(void) {
#ifndef USE_HARDWARE_SELECT_TRANSPORT
    if (is_ble_ready()) {
        if (is_usb_ready()) {
            LOG_DBG("Both ble and usb endpoint transports are ready. Using %d",
                    preferred_transport);
            return preferred_transport;
        }

        LOG_DBG("Only BLE is ready.");
        return ZMK_TRANSPORT_BLE;
    }
    if (is_ppt_ready()) {
        if (is_usb_ready()) {
            LOG_DBG("Both ppt and usb endpoint transports are ready. Using %d",
                    preferred_transport);
            return preferred_transport;
        }

        LOG_DBG("Only ppt is ready.");
        return ZMK_TRANSPORT_PPT;
    }
    if (is_usb_ready()) {
        LOG_DBG("Only USB is ready.");
        return ZMK_TRANSPORT_USB;
    }

    LOG_DBG("No endpoint transports are ready.");
    return DEFAULT_TRANSPORT;
#else
    return get_hardware_select_transport();
#endif
}

static struct zmk_endpoint_instance get_selected_instance(void) {
    struct zmk_endpoint_instance instance = {.transport = get_selected_transport()};

    switch (instance.transport) {
#if IS_ENABLED(CONFIG_ZMK_BLE)
    case ZMK_TRANSPORT_BLE:
        instance.ble.profile_index = zmk_ble_active_profile_index();
        break;
#endif // IS_ENABLED(CONFIG_ZMK_BLE)

    default:
        // No extra data for this transport.
        break;
    }

    return instance;
}

static int zmk_endpoints_init(void) {
    int zmk_settings_check(void);
    zmk_settings_check();
#if IS_ENABLED(CONFIG_SETTINGS)
    settings_subsys_init();

    int err = settings_register(&endpoints_handler);
    if (err) {
        LOG_ERR("Failed to register the endpoints settings handler (err %d)", err);
        return err;
    }

    k_work_init_delayable(&endpoints_save_work, endpoints_save_preferred_work);

    settings_load_subtree("endpoints");
#endif

    current_instance = get_selected_instance();

    return 0;
}
static K_SEM_DEFINE(wait_disc, 0, 1);

static void disc_worker(struct k_work *work) {
    if (current_instance.transport == ZMK_TRANSPORT_PPT) {
        LOG_WRN("stop ppt sync");
        // keyboard_ppt_stop_sync();
        void zmk_ppt_disconnect(void);
        zmk_ppt_disconnect();
    } else if (current_instance.transport == ZMK_TRANSPORT_BLE) {
        // disconnect ble;
        zmk_ble_prof_disconnect(current_instance.ble.profile_index);
    }
    k_sem_give(&wait_disc);
}
static void disc_transport(void) {
    LOG_WRN("disc_transport:%d", current_instance.transport);
    if (current_instance.transport == ZMK_TRANSPORT_PPT) {
        LOG_WRN("stop ppt sync");
        // keyboard_ppt_stop_sync();
        void zmk_ppt_disconnect(void);
        zmk_ppt_disconnect();
    } else if (current_instance.transport == ZMK_TRANSPORT_BLE) {
        // disconnect ble;
        zmk_ble_prof_disconnect(current_instance.ble.profile_index);
    }
}
static void disconnect_current_endpoint(void) {
    zmk_hid_keyboard_clear();
    zmk_hid_consumer_clear();
#if IS_ENABLED(CONFIG_ZMK_MOUSE)
    zmk_hid_mouse_clear();
#endif // IS_ENABLED(CONFIG_ZMK_MOUSE)

    zmk_endpoints_send_report(HID_USAGE_KEY);
    zmk_endpoints_send_report(HID_USAGE_CONSUMER);

    // k_sem_reset(&wait_disc);
    // k_work_schedule(&disc_work,K_MSEC(30));
    disc_transport();
    // k_msleep(30);
}

static void update_current_endpoint(void) {
    struct zmk_endpoint_instance new_instance = get_selected_instance();
    LOG_WRN("update_current_endpoint,cur:%d,new:%d", current_instance.transport,
            new_instance.transport);
    if (!zmk_endpoint_instance_eq(new_instance, current_instance)) {
        // Cancel all current keypresses so keys don't stay held on the old endpoint.
        disconnect_current_endpoint();
        // k_sem_take(&wait_disc,K_FOREVER);
        // if(current_instance.transport == ZMK_TRANSPORT_USB)
        // {
        //     zmk_usb_deinit();
        // }
        current_instance = new_instance;
        // add transport init here!
        lowpower_settings();
        switch (current_instance.transport) {
        case ZMK_TRANSPORT_BLE:
            LOG_DBG("change to ble");
#ifdef CONFIG_ENABLE_WIN_LOCK_INDICATOR
            winlock_led_onoff(0);
#endif
            zmk_ble_init();
            current_instance.ble.profile_index = zmk_ble_active_profile_index();
            break;

#if CONFIG_ZMK_PPT
        case ZMK_TRANSPORT_PPT:
            LOG_DBG("change to 24g");
#ifdef CONFIG_ENABLE_WIN_LOCK_INDICATOR
            winlock_led_onoff(0);
#endif
            zmk_ppt_init();
            break;
#endif
        case ZMK_TRANSPORT_USB:
            LOG_ERR("change to usb");
            zmk_usb_init();
            break;
        default:
            break;
        }
        //
        char endpoint_str[ZMK_ENDPOINT_STR_LEN];
        zmk_endpoint_instance_to_str(current_instance, endpoint_str, sizeof(endpoint_str));
        LOG_INF("Endpoint changed: %s", endpoint_str);

        raise_zmk_endpoint_changed((struct zmk_endpoint_changed){.endpoint = current_instance});
    }
}

static int endpoint_listener(const zmk_event_t *eh) {
    update_current_endpoint();
    return 0;
}
enum zmk_transport get_current_transport(void) { return current_instance.transport; }
ZMK_LISTENER(endpoint_listener, endpoint_listener);
#if IS_ENABLED(CONFIG_ZMK_USB)
ZMK_SUBSCRIPTION(endpoint_listener, zmk_usb_conn_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(endpoint_listener, zmk_ble_active_profile_changed);
#endif
// #if IS_ENABLED(CONFIG_ZMK_PPT)
// ZMK_SUBSCRIPTION(endpoint_listener, zmk_ppt_conn_state_changed);
// #endif
#define ENDPOINTS_INIT_PRIORITY 51 // make sure endpoints init early to check settings!
SYS_INIT(zmk_endpoints_init, APPLICATION,
         ENDPOINTS_INIT_PRIORITY); // CONFIG_APPLICATION_INIT_PRIORITY);
