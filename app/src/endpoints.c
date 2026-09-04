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
#include <zmk/events/endpoint_changed.h>

#include <hal/nrf_power.h>
#include <zephyr/sys/reboot.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include "./launcher/mousekey.h"

extern uint8_t get_hardware_select_transport(void);
extern int zmk_ble_init(const struct device *_arg);
#if CONFIG_ZMK_NRF_24G
extern void zmk_24g_init(void);
extern int zmk_24g_send_report(uint8_t *data, uint8_t len);
#endif
int zmk_hog_send_mouse_report(report_mouse_t *report);
void keyboad_led_set_onoff(uint8_t led_state);
void bt_24g_switch_reset(void);
uint8_t get_mode_status(void);

// #define DEFAULT_TRANSPORT
//     COND_CODE_1(IS_ENABLED(CONFIG_ZMK_BLE), (ZMK_TRANSPORT_BLE), (ZMK_TRANSPORT_USB))

#define DEFAULT_TRANSPORT ZMK_TRANSPORT_USB

static struct zmk_endpoint_instance current_instance = {};
static enum zmk_transport preferred_transport =
    ZMK_TRANSPORT_USB; /* Used if multiple endpoints are ready */

static void update_current_endpoint(void);

void switch_to_usb(void) { zmk_endpoints_select_transport(ZMK_TRANSPORT_USB); }
#if 0
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
#endif

bool zmk_endpoint_instance_eq(struct zmk_endpoint_instance a, struct zmk_endpoint_instance b) {
    if (a.transport != b.transport) {
        return false;
    }

    switch (a.transport) {
    case ZMK_TRANSPORT_USB:
        return true;

    case ZMK_TRANSPORT_BLE:
        return a.ble.profile_index == b.ble.profile_index;
    case ZMK_TRANSPORT_24G:
        return true;
        break;
    default:
        return false;
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
    case ZMK_TRANSPORT_24G:
        return snprintf(str, len, "24G");

    default:
        return snprintf(str, len, "Invalid");
    }
}

#define INSTANCE_INDEX_OFFSET_USB 0
#define INSTANCE_INDEX_OFFSET_BLE ZMK_ENDPOINT_USB_COUNT

int zmk_endpoint_instance_to_index(struct zmk_endpoint_instance endpoint) {
    switch (endpoint.transport) {
    case ZMK_TRANSPORT_USB:
        return INSTANCE_INDEX_OFFSET_USB;

    case ZMK_TRANSPORT_BLE:
        return INSTANCE_INDEX_OFFSET_BLE + endpoint.ble.profile_index;
    case ZMK_TRANSPORT_24G:
        break;
    default:
        break;
    }

    LOG_ERR("Invalid transport %d", endpoint.transport);
    return 0;
}

int zmk_endpoints_select_transport(enum zmk_transport transport) {
    LOG_DBG("Selected endpoint transport %d", transport);

    // if (preferred_transport == transport) {
    //     return 0;
    // }

    // preferred_transport = transport;

    // endpoints_save_preferred();
    if (current_instance.transport == transport) {
        return 0;
    }
    if (transport == ZMK_TRANSPORT_NONE) {
        current_instance.transport = transport;
        return 0;
    }
    preferred_transport = transport;
    update_current_endpoint();

    return 0;
}

int zmk_endpoints_toggle_transport(void) {
    enum zmk_transport new_transport =
        (preferred_transport == ZMK_TRANSPORT_USB) ? ZMK_TRANSPORT_BLE : ZMK_TRANSPORT_USB;
    return zmk_endpoints_select_transport(new_transport);
}

struct zmk_endpoint_instance zmk_endpoints_selected(void) { return current_instance; }
#if CONFIG_ADAPATIVE_NKRO

extern struct zmk_adapative_nkro adapative_nkro;
int zmk_usb_hid_send_report(const uint8_t *report, size_t len);
bool nkro_changed(void);
bool kb_changed(void);

int transport_send(struct zmk_hid_keyboard_report *report, uint8_t len)
{
    int err = 0;
    switch (current_instance.transport) {
        case ZMK_TRANSPORT_USB: {
            err =zmk_usb_hid_send_report((uint8_t *)report,len);
            if (err) {
                LOG_ERR("FAILED TO SEND OVER USB: %d", err);
            }
        }
        break;
        case ZMK_TRANSPORT_24G:{
            err = zmk_24g_send_report((uint8_t *)report, len);
            if (err) {
                LOG_ERR("FAILED TO SEND OVER PPT: %d", err);
            }
        }
        break;

        case ZMK_TRANSPORT_BLE: {
            // LOG_HEXDUMP_ERR(&report->body,sizeof(report->body),"tx");
            err = zmk_hog_send_keyboard_report(&report->body);
            if (err) {
                LOG_ERR("FAILED TO SEND OVER HOG: %d", err);
            }
        }
        break;
        default:
        break;
    }
    return err;
}
static int send_keyboard_report(void) {
    extern struct zmk_adapative_nkro adapative_nkro;
    // extern struct zmk_hid_keyboard_report nkro_24g_rpt;
    int ret=0;
    if(zmk_endpoints_selected().transport != ZMK_TRANSPORT_24G)
    {
        if(kb_changed())
        {
            struct zmk_hid_keyboard_report *report = zmk_hid_get_keyboard_report();
            report->report_id = ZMK_HID_REPORT_ID_KEYBOARD;
            report->body._reserved =0;
            uint8_t len =  CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE+3;
            ret =transport_send(report,len);
        }
        if(nkro_changed())
        {
            adapative_nkro.nkro_report.report_id =ZMK_HID_REPORT_ID_KEYBOARD_NKRO;
            adapative_nkro.nkro_report.body.modifiers=0;
            adapative_nkro.nkro_report.body._reserved=1;
            struct zmk_hid_keyboard_report *report =&adapative_nkro.nkro_report;
            uint8_t len = sizeof(adapative_nkro.nkro_report);
            ret =transport_send(report,len);
        }
    }
    else 
    {
        uint8_t nk_count=0;
        for(int n=0;n<sizeof(adapative_nkro.nkro_report.body.keys);n++)
        {
            if(adapative_nkro.nkro_report.body.keys[n])
            {
                nk_count=1;
                break;
            }
        }

        if (nkro_changed()|| nk_count)
        {
             struct zmk_hid_keyboard_report kb_rpt={0};
             struct zmk_hid_keyboard_report *report = zmk_hid_get_keyboard_report();

             for(int i=0;i<CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE;i++)
                if(report->body.keys[i])
                    WRITE_BIT(kb_rpt.body.keys[report->body.keys[i]/8], report->body.keys[i] % 8, 1);
            // LOG_HEXDUMP_ERR(adapative_nkro.nkro_report.body.keys,sizeof(kb_rpt.body.keys),"nk0");

            for(int n=0;n<sizeof(kb_rpt.body.keys);n++)
            {

                kb_rpt.body.keys[n] |= adapative_nkro.nkro_report.body.keys[n];
            }
            
            // LOG_HEXDUMP_ERR(kb_rpt.body.keys,sizeof(kb_rpt.body.keys),"kb");

            kb_rpt.report_id =ZMK_HID_REPORT_ID_KEYBOARD_NKRO;
            kb_rpt.body.modifiers=report->body.modifiers;;
            kb_rpt.body._reserved=0;
            struct zmk_hid_keyboard_report *nk_report =&kb_rpt;
            uint8_t len = sizeof(kb_rpt);
            ret =transport_send(nk_report,len);
        }
        else 
        {
            struct zmk_hid_keyboard_report *report = zmk_hid_get_keyboard_report();
            report->report_id = ZMK_HID_REPORT_ID_KEYBOARD;
            report->body._reserved =0;
            uint8_t len =  CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE+3;
            ret =transport_send(report,len);

        }
        // struct zmk_hid_keyboard_report *report = zmk_hid_get_keyboard_report();
        // nkro_24g_rpt.body.modifiers=report->body.modifiers;
        // nkro_24g_rpt.report_id =ZMK_HID_REPORT_ID_KEYBOARD_NKRO;        
        // nkro_24g_rpt.body._reserved=0;
        // // LOG_HEXDUMP_ERR(&nkro_24g_rpt,sizeof(nkro_24g_rpt),"24g");
        // ret =transport_send(&nkro_24g_rpt,sizeof(nkro_24g_rpt));
    }
    return ret;
}
#else
static int send_keyboard_report(void) {
    struct zmk_hid_keyboard_report *keyboard_report = zmk_hid_get_keyboard_report();

    switch (current_instance.transport) {
#if IS_ENABLED(CONFIG_ZMK_USB)
    case ZMK_TRANSPORT_USB: {
        int err = zmk_usb_hid_send_report((uint8_t *)keyboard_report, sizeof(*keyboard_report));
        if (err) {
            LOG_ERR("FAILED TO SEND OVER USB: %d", err);
        }
        return err;
    }
#endif /* IS_ENABLED(CONFIG_ZMK_USB) */

#if IS_ENABLED(CONFIG_ZMK_BLE)
    case ZMK_TRANSPORT_BLE: {
        int err = zmk_hog_send_keyboard_report(&keyboard_report->body);
        if (err) {
            LOG_ERR("FAILED TO SEND OVER HOG: %d", err);
        }
        return err;
    }
#endif /* IS_ENABLED(CONFIG_ZMK_BLE) */
#if CONFIG_ZMK_NRF_24G
    case ZMK_TRANSPORT_24G: {

        int err = zmk_24g_send_report((uint8_t *)keyboard_report, sizeof(*keyboard_report));
        if (err) {
            LOG_ERR("FAILED TO SEND OVER HOG: %d", err);
        }
        return err;
    } break;
#endif
    default:
        break;
    }

    LOG_ERR("Unsupported endpoint transport %d", current_instance.transport);
    return -ENOTSUP;
}
#endif 
static int send_consumer_report(void) {
    struct zmk_hid_consumer_report *consumer_report = zmk_hid_get_consumer_report();

    switch (current_instance.transport) {
#if IS_ENABLED(CONFIG_ZMK_USB)
    case ZMK_TRANSPORT_USB: {
        int err = zmk_usb_hid_send_report((uint8_t *)consumer_report, sizeof(*consumer_report));
        if (err) {
            LOG_ERR("FAILED TO SEND OVER USB: %d", err);
        }
        return err;
    }
#endif /* IS_ENABLED(CONFIG_ZMK_USB) */

#if IS_ENABLED(CONFIG_ZMK_BLE)
    case ZMK_TRANSPORT_BLE: {
        int err = zmk_hog_send_consumer_report(&consumer_report->body);
        if (err) {
            LOG_ERR("FAILED TO SEND OVER HOG: %d", err);
        }
        return err;
    }
#endif /* IS_ENABLED(CONFIG_ZMK_BLE) */
#if CONFIG_ZMK_NRF_24G
    case ZMK_TRANSPORT_24G: {
        int err = zmk_24g_send_report((uint8_t *)consumer_report, sizeof(*consumer_report));
        if (err) {
            LOG_ERR("FAILED TO SEND OVER HOG: %d", err);
        }
        return err;
    } break;
#endif
    default:
        break;
    }

    LOG_ERR("Unsupported endpoint transport %d", current_instance.transport);
    return -ENOTSUP;
}

int zmk_endpoints_send_report(uint16_t usage_page) {

    LOG_DBG("usage page 0x%02X", usage_page);
    switch (usage_page) {
    case HID_USAGE_KEY:
        return send_keyboard_report();

    case HID_USAGE_CONSUMER:
        return send_consumer_report();
    case HID_USAGE_GEN_BUTTON:
        mousekey_send();
        return 0;
    }

    LOG_ERR("Unsupported usage page %d", usage_page);
    return -ENOTSUP;
}

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
#if 0
static bool is_usb_ready(void) {
#if IS_ENABLED(CONFIG_ZMK_USB)
    return zmk_usb_is_hid_ready();
#else
    return false;
#endif
}

static bool is_ble_ready(void) {
#if IS_ENABLED(CONFIG_ZMK_BLE)
    return zmk_ble_is_connected();//zmk_ble_active_profile_is_connected();
#else
    return false;
#endif
}

static enum zmk_transport get_selected_transport(void) {
    if (is_ble_ready()) {
        if (is_usb_ready()) {
            LOG_DBG("Both endpoint transports are ready. Using %d", preferred_transport);
            return preferred_transport;
        }

        LOG_DBG("Only BLE is ready.");
        return ZMK_TRANSPORT_BLE;
    }

    if (is_usb_ready()) {
        LOG_DBG("Only USB is ready.");
        return ZMK_TRANSPORT_USB;
    }

    LOG_DBG("No endpoint transports are ready.");
    return current_instance.transport;// DEFAULT_TRANSPORT;
}
#endif
uint8_t get_current_transport(void) { return current_instance.transport; }
#if 0
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
#endif

static int zmk_endpoints_init(const struct device *_arg) {
#if 0 // IS_ENABLED(CONFIG_SETTINGS)
    settings_subsys_init();

    int err = settings_register(&endpoints_handler);
    if (err) {
        LOG_ERR("Failed to register the endpoints settings handler (err %d)", err);
        return err;
    }

    k_work_init_delayable(&endpoints_save_work, endpoints_save_preferred_work);

    // settings_load_subtree("endpoints");
#endif

    uint32_t reason_reset_pin = nrf_power_resetreas_get(NRF_POWER) & POWER_RESETREAS_RESETPIN_Msk;
    if (reason_reset_pin) {
        LOG_DBG("reason_reset_pin");
        nrf_power_resetreas_clear(NRF_POWER, POWER_RESETREAS_RESETPIN_Msk);
    }

    current_instance.transport = ZMK_TRANSPORT_NONE; // ZMK_TRANSPORT_USB;
    LOG_ERR("zmk_endpoints_init");
    return 0;
}

static void disconnect_current_endpoint() {
    zmk_hid_keyboard_clear();
    zmk_hid_consumer_clear();

    zmk_endpoints_send_report(HID_USAGE_KEY);
    zmk_endpoints_send_report(HID_USAGE_CONSUMER);

    keyboad_led_set_onoff(0);
    void zmk_24g_endpoint_disconnect(void);
    void zmk_ble_endpoint_disconnect(void);
    zmk_24g_endpoint_disconnect();
    zmk_ble_endpoint_disconnect();
}
void led_power_on(void);
void add_kscan_boot_msgq(void);

static void update_current_endpoint(void) {
    LOG_DBG("curr:%d,new:%d", current_instance.transport, preferred_transport);
    struct zmk_endpoint_instance new_instance = {
        .transport = preferred_transport}; // get_selected_instance();

    if (!zmk_endpoint_instance_eq(new_instance, current_instance)) {
        // Cancel all current keypresses so keys don't stay held on the old endpoint.
        if (current_instance.transport != ZMK_TRANSPORT_NONE)
            disconnect_current_endpoint();
        current_instance = new_instance;
        // led_power_on();

        switch (current_instance.transport) {
        case ZMK_TRANSPORT_BLE:
            LOG_DBG("change to ble");
            zmk_ble_init(NULL);
            add_kscan_boot_msgq();
            break;
#if CONFIG_ZMK_NRF_24G
        case ZMK_TRANSPORT_24G:
            LOG_DBG("change to 24g");
            add_kscan_boot_msgq();
            zmk_24g_init();
            break;
#endif
        case ZMK_TRANSPORT_USB:
            LOG_ERR("usb_dc_reset");
            usb_dc_reset();
            break;
        default:
            break;
        }

        char endpoint_str[ZMK_ENDPOINT_STR_LEN];
        zmk_endpoint_instance_to_str(current_instance, endpoint_str, sizeof(endpoint_str));
        LOG_INF("Endpoint changed: %s", endpoint_str);

        ZMK_EVENT_RAISE(
            new_zmk_endpoint_changed((struct zmk_endpoint_changed){.endpoint = current_instance}));
    }
}

static int endpoint_listener(const zmk_event_t *eh) {

    const struct zmk_usb_conn_state_changed *ev = as_zmk_usb_conn_state_changed(eh);
    if (ev) {
        LOG_DBG("usb state:%d", ev->conn_state);
        if (ev->conn_state == ZMK_USB_CONN_HID) {
            if (current_instance.transport == ZMK_TRANSPORT_USB) {
                LOG_DBG("usb status:%d", zmk_usb_get_status());

                if (zmk_usb_get_status() == USB_DC_SUSPEND) {

                    keyboad_led_set_onoff(0);
                } else if (zmk_usb_get_status() == USB_DC_CONFIGURED) {
                    add_kscan_boot_msgq();
                }
            }
            if (get_hardware_select_transport() == 0 && get_mode_status() == 3) {
                preferred_transport = ZMK_TRANSPORT_USB;
                update_current_endpoint();
            }
        } else if (ev->conn_state == ZMK_USB_CONN_NONE) {
            preferred_transport = get_hardware_select_transport();
            update_current_endpoint();
        }
    }

    return 0;
}

int host_mouse_send(report_mouse_t *rp) {

    switch (current_instance.transport) {
#if IS_ENABLED(CONFIG_ZMK_USB)
    case ZMK_TRANSPORT_USB: {
        int err = zmk_usb_hid_send_report((uint8_t *)rp, sizeof(report_mouse_t));
        if (err) {
            LOG_ERR("FAILED TO SEND OVER USB: %d", err);
        }
        return err;
    }
#endif /* IS_ENABLED(CONFIG_ZMK_USB) */

#if IS_ENABLED(CONFIG_ZMK_BLE)
    case ZMK_TRANSPORT_BLE: {
        int err = zmk_hog_send_mouse_report(rp);
        if (err) {
            LOG_ERR("FAILED TO SEND OVER HOG: %d", err);
        }
        return err;
    }
#endif /* IS_ENABLED(CONFIG_ZMK_BLE) */
#if CONFIG_ZMK_NRF_24G
    case ZMK_TRANSPORT_24G: {

        int err = zmk_24g_send_report((uint8_t *)rp, sizeof(report_mouse_t));
        if (err) {
            LOG_ERR("FAILED TO SEND OVER HOG: %d", err);
        }
        return err;
    } break;
#endif
    default:
        break;
    }

    LOG_ERR("Unsupported endpoint transport %d", current_instance.transport);
    return -ENOTSUP;
}

ZMK_LISTENER(endpoint_listener, endpoint_listener);
#if IS_ENABLED(CONFIG_ZMK_USB)
ZMK_SUBSCRIPTION(endpoint_listener, zmk_usb_conn_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(endpoint_listener, zmk_ble_active_profile_changed);
#endif

SYS_INIT(zmk_endpoints_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
