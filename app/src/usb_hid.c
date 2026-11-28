/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/init.h>

#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>

#include <zmk/usb.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
#include <zmk/hid_indicators.h>
#endif // IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
#include <zmk/event_manager.h>
#include <zmk/endpoints.h>

LOG_MODULE_DECLARE(zmk, 4); // CONFIG_ZMK_LOG_LEVEL);

static const struct device *hid_dev;

static K_SEM_DEFINE(hid_sem, 1, 1);

static void in_ready_cb(const struct device *dev) { k_sem_give(&hid_sem); }

#define HID_GET_REPORT_TYPE_MASK 0xff00
#define HID_GET_REPORT_ID_MASK 0x00ff

#define HID_REPORT_TYPE_INPUT 0x100
#define HID_REPORT_TYPE_OUTPUT 0x200
#define HID_REPORT_TYPE_FEATURE 0x300

#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
static uint8_t hid_protocol = HID_PROTOCOL_REPORT;
static uint8_t usb_out;

static void set_proto_cb(const struct device *dev, uint8_t protocol) {
    hid_protocol = protocol;
    LOG_ERR("-->set protocol:%d", hid_protocol);
}

void zmk_usb_hid_set_protocol(uint8_t protocol) { hid_protocol = protocol; }
#endif /* IS_ENABLED(CONFIG_ZMK_USB_BOOT) */

bool zmk_usb_hid_is_boot_protocol(void) {
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    return hid_protocol == HID_PROTOCOL_BOOT;
#else
    return false;
#endif
}

uint8_t *get_keyboard_report(size_t *len) {
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    if (hid_protocol != HID_PROTOCOL_REPORT) {
        zmk_hid_boot_report_t *boot_report = zmk_hid_get_boot_report();
        /* A boot keyboard report is exactly 8 bytes (modifiers, reserved, 6 keys). The
         * report body type is larger than that because its key array is sized for NKRO. */
        *len = 2 + HID_BOOT_KEY_LEN;
        return (uint8_t *)boot_report;
    }
#endif
    struct zmk_hid_keyboard_report *report = zmk_hid_get_keyboard_report();
    if (zmk_get_nkro_status())
        *len = sizeof(*report);
    else
        *len = CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE + 3;
    return (uint8_t *)report;
}

static int get_report_cb(const struct device *dev, struct usb_setup_packet *setup, int32_t *len,
                         uint8_t **data) {

    /*
     * 7.2.1 of the HID v1.11 spec is unclear about handling requests for reports that do not exist
     * For requested reports that aren't input reports, return -ENOTSUP like the Zephyr subsys does
     */
    if ((setup->wValue & HID_GET_REPORT_TYPE_MASK) != HID_REPORT_TYPE_INPUT) {
        LOG_ERR("Unsupported report type %d requested", (setup->wValue & HID_GET_REPORT_TYPE_MASK)
                                                            << 8);
        return -ENOTSUP;
    }

    switch (setup->wValue & HID_GET_REPORT_ID_MASK) {
    case ZMK_HID_REPORT_ID_KEYBOARD: {
        *data = get_keyboard_report(len);
        break;
    }
    case ZMK_HID_REPORT_ID_CONSUMER: {
        struct zmk_hid_consumer_report *report = zmk_hid_get_consumer_report();
        *data = (uint8_t *)report;
        *len = sizeof(*report);
        break;
    }
    default:
        LOG_ERR("Invalid report ID %d requested", setup->wValue & HID_GET_REPORT_ID_MASK);
        return -EINVAL;
    }

    return 0;
}
static void out_ready_cb(const struct device *dev) {
    uint8_t rev_buf[32] = {0};
    uint32_t rev_bytes = 0;
    hid_int_ep_read(dev, rev_buf, sizeof(rev_buf), &rev_bytes);
    LOG_HEXDUMP_DBG(rev_buf, rev_bytes, "rx");
    if (rev_buf[0] == ZMK_HID_REPORT_ID_LEDS) {
        usb_out = 1;
        if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_USB) {
            // struct zmk_hid_led_report *report = (struct zmk_hid_led_report *)rev_buf;
            struct zmk_hid_led_report report;
            report.body.leds = rev_buf[1];
            struct zmk_endpoint_instance endpoint = {
                .transport = ZMK_TRANSPORT_USB,
            };
            zmk_hid_indicators_process_report(&report.body, endpoint);
        }
    } else if (hid_protocol != HID_PROTOCOL_REPORT) {
        if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_USB && (rev_bytes == 1)) {
            struct zmk_hid_led_report report;
            report.body.leds = rev_buf[0];
            struct zmk_endpoint_instance endpoint = {
                .transport = ZMK_TRANSPORT_USB,
            };
            zmk_hid_indicators_process_report(&report.body, endpoint);
        }
    }
}
static int set_report_cb(const struct device *dev, struct usb_setup_packet *setup, int32_t *len,
                         uint8_t **data) {
    if ((setup->wValue & HID_GET_REPORT_TYPE_MASK) != HID_REPORT_TYPE_OUTPUT) {
        LOG_ERR("Unsupported report type %d requested",
                (setup->wValue & HID_GET_REPORT_TYPE_MASK) >> 8);
        return -ENOTSUP;
    }
    LOG_DBG("rpt id:%d", setup->wValue & HID_GET_REPORT_ID_MASK);
    switch (setup->wValue & HID_GET_REPORT_ID_MASK) {
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
    case ZMK_HID_REPORT_ID_LEDS:
        if (*len != sizeof(struct zmk_hid_led_report)) {
            LOG_ERR("LED set report is malformed: length=%d", *len);
            return -EINVAL;
        } else {
            // struct zmk_hid_led_report *report = (struct zmk_hid_led_report *)*data;
            struct zmk_hid_led_report report;
            uint8_t *p_data = *data;
            report.body.leds = p_data[1];
            struct zmk_endpoint_instance endpoint = {
                .transport = ZMK_TRANSPORT_USB,
            };
            zmk_hid_indicators_process_report(&report.body, endpoint);
        }
        break;
#endif // IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
    default:
        // LOG_HEXDUMP_ERR(*data,*len,"set report");
        if (hid_protocol != HID_PROTOCOL_REPORT) {
            if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_USB && (*len == 1)) {
                struct zmk_hid_led_report report;
                uint8_t *p_data = *data;
                report.body.leds = p_data[0];
                struct zmk_endpoint_instance endpoint = {
                    .transport = ZMK_TRANSPORT_USB,
                };
                zmk_hid_indicators_process_report(&report.body, endpoint);
            }
        }
        LOG_ERR("Invalid report ID %d requested", setup->wValue & HID_GET_REPORT_ID_MASK);
        return -EINVAL;
    }

    return 0;
}

static const struct hid_ops ops = {
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    .protocol_change = set_proto_cb,
#endif
    .int_in_ready = in_ready_cb,
    .int_out_ready = out_ready_cb,
    .get_report = get_report_cb,
    .set_report = set_report_cb,
};
void toggle_debug_pin(void);
int zmk_usb_hid_send_report(const uint8_t *report, size_t len) {
    switch (zmk_usb_get_status()) {

    case USB_DC_ERROR:
    case USB_DC_RESET:
    case USB_DC_DISCONNECTED:
    case USB_DC_UNKNOWN:
        return -ENODEV;
    case USB_DC_SUSPEND: {
        extern uint8_t usb_configured;
        if (!usb_configured)
            return -1;
        int ret = usb_wakeup_request();
        LOG_ERR("usb_wakeup_request:%d ", ret);
        usb_out = 0;
        for (int i = 0; i < 30; i++) {
            k_msleep(100);
            if (!usb_get_remote_wakeup_status() || usb_out) {
                usb_out = 0;
                LOG_ERR("i:%d", i);
                break;
            }
        }
    }
    default:
        // LOG_DBG("usb send");
        toggle_debug_pin();
        k_sem_take(&hid_sem, K_MSEC(30));
        int err = 0;
        // LOG_HEXDUMP_ERR(report,len,"usb");
        // cut reserve byte in nkro report;
        /* Boot protocol reports have no report ID: byte 0 is the modifier mask, and
         * 0x04 there means Left Alt, not the NKRO report ID. */
        if (!zmk_usb_hid_is_boot_protocol() && report[0] == ZMK_HID_REPORT_ID_KEYBOARD_NKRO) {
            uint8_t buffer[(ZMK_HID_KEYBOARD_NKRO_MAX_USAGE + 1) / 8 + 2];
            buffer[0] = report[0];
            buffer[1] = report[1];
            memcpy(buffer + 2, report + 3, (ZMK_HID_KEYBOARD_NKRO_MAX_USAGE + 1) / 8);
            // LOG_HEXDUMP_ERR(buffer,len-1,"nkro");
            err = hid_int_ep_write(hid_dev, buffer, len - 1, NULL);
        } else
            err = hid_int_ep_write(hid_dev, report, len, NULL);

        toggle_debug_pin();
        LOG_DBG("sent");
        // LOG_HEXDUMP_WRN(report,len,"data");
        if (err) {
            k_sem_give(&hid_sem);
        }

        return err;
    }
}

int zmk_usb_hid_send_keyboard_report(void) {
    size_t len;
    uint8_t *report = get_keyboard_report(&len);
    return zmk_usb_hid_send_report(report, len);
}

int zmk_usb_hid_send_consumer_report(void) {
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    if (hid_protocol == HID_PROTOCOL_BOOT) {
        return -ENOTSUP;
    }
#endif /* IS_ENABLED(CONFIG_ZMK_USB_BOOT) */

    struct zmk_hid_consumer_report *report = zmk_hid_get_consumer_report();
    return zmk_usb_hid_send_report((uint8_t *)report, sizeof(*report));
}

#if IS_ENABLED(CONFIG_ZMK_MOUSE)
int zmk_usb_hid_send_mouse_report() {
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    if (hid_protocol == HID_PROTOCOL_BOOT) {
        return -ENOTSUP;
    }
#endif /* IS_ENABLED(CONFIG_ZMK_USB_BOOT) */

    struct zmk_hid_mouse_report *report = zmk_hid_get_mouse_report();
    return zmk_usb_hid_send_report((uint8_t *)report, sizeof(*report));
}
#endif // IS_ENABLED(CONFIG_ZMK_MOUSE)

static int zmk_usb_hid_init(void) {
    hid_dev = device_get_binding("HID_0");
    if (hid_dev == NULL) {
        LOG_ERR("Unable to locate HID device");
        return -EINVAL;
    }

    usb_hid_register_device(hid_dev, zmk_hid_report_desc, sizeof(zmk_hid_report_desc), &ops);

#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    usb_hid_set_proto_code(hid_dev, HID_BOOT_IFACE_CODE_KEYBOARD);
#endif /* IS_ENABLED(CONFIG_ZMK_USB_BOOT) */

    usb_hid_init(hid_dev);

    return 0;
}

SYS_INIT(zmk_usb_hid_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
