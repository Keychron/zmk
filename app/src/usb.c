/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/init.h>

#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>

#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/event_manager.h>
#include <zmk/events/usb_conn_state_changed.h>
#include "version.h"
#include <zephyr/sys/byteorder.h>
#include <zmk/endpoints.h>

#include <zmk/usb_hid.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);
void bt_24g_switch_reset(void);
void bt_24g_switch_pulldown(uint8_t current);
void zmk_usb_set_protocol_report(void);
void keyboad_led_set_onoff(uint8_t led_state);
uint8_t keyboard_get_led_state(void);
uint32_t usb_sof_count=0;
static enum usb_dc_status_code usb_status = USB_DC_UNKNOWN;
static bool is_configured;

static void raise_usb_status_changed_event(struct k_work *_work) {
    raise_zmk_usb_conn_state_changed(
        (struct zmk_usb_conn_state_changed){.conn_state = zmk_usb_get_conn_state()});
}

K_WORK_DEFINE(usb_status_notifier_work, raise_usb_status_changed_event);

enum usb_dc_status_code zmk_usb_get_status(void) { return usb_status; }

<<<<<<< HEAD
enum zmk_usb_conn_state zmk_usb_get_conn_state() {
    // LOG_DBG("state: %d", usb_status);
=======
enum zmk_usb_conn_state zmk_usb_get_conn_state(void) {
    LOG_DBG("state: %d", usb_status);
>>>>>>> ead91ca094c6cd778574f73b9ae011ddab611c66
    switch (usb_status) {
    // case USB_DC_SUSPEND:
    //     LOG_DBG("USB_DC_SUSPEND");
    case USB_DC_CONFIGURED:
    case USB_DC_RESUME:
<<<<<<< HEAD
    case USB_DC_CLEAR_HALT:        
=======
    case USB_DC_CLEAR_HALT:
    case USB_DC_SOF:
>>>>>>> ead91ca094c6cd778574f73b9ae011ddab611c66
        return ZMK_USB_CONN_HID;

    case USB_DC_DISCONNECTED:
    case USB_DC_UNKNOWN:
        return ZMK_USB_CONN_NONE;

    default:
        return ZMK_USB_CONN_POWERED;
    }
}

bool zmk_usb_is_hid_ready(void) {
    return zmk_usb_get_conn_state() == ZMK_USB_CONN_HID && is_configured;
}

void usb_status_cb(enum usb_dc_status_code status, const uint8_t *params) {
<<<<<<< HEAD
    static uint8_t led_bak_state=0;
    usb_status = status;

    switch(status)
    {
        case USB_DC_RESET:

            zmk_usb_set_protocol_report();
            led_bak_state =0;
            usb_sof_count=0;
            break;
        case USB_DC_RESUME:

            if(get_current_transport()==ZMK_TRANSPORT_USB)
            {
                keyboad_led_set_onoff(led_bak_state);
            }
            usb_sof_count =0;
        break;
    case USB_DC_SUSPEND:
        led_bak_state= keyboard_get_led_state();
        if(get_current_transport()==ZMK_TRANSPORT_USB)
        {
            keyboad_led_set_onoff(0);
        }
        break;
    case USB_DC_DISCONNECTED:

        break;
    default:
        break;
    }
    LOG_ERR("status:%d,sof:%d",status,usb_sof_count);
    k_work_submit(&usb_status_notifier_work);
};
extern struct usb_desc_header __usb_descriptor_start[];
static int zmk_usb_init(const struct device *_arg) {

    struct usb_device_descriptor *device_descriptor=(struct usb_device_descriptor *)__usb_descriptor_start;
    if(device_descriptor)
    {
        LOG_ERR("usb bcd ver:%x",device_descriptor->bcdDevice);
        device_descriptor->bcdDevice = sys_cpu_to_le16(USB_BCD_VER);
        LOG_ERR("new usb bcd ver:%x,ver:%s",device_descriptor->bcdDevice,APP_VERSION_STRING);
    }

=======
    // Start-of-frame events are too frequent and noisy to notify, and they're
    // not used within ZMK
    if (status == USB_DC_SOF) {
        return;
    }

#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    if (status == USB_DC_RESET) {
        zmk_usb_hid_set_protocol(HID_PROTOCOL_REPORT);
    }
#endif
    usb_status = status;
    if (zmk_usb_get_conn_state() == ZMK_USB_CONN_HID) {
        is_configured |= usb_status == USB_DC_CONFIGURED;
    } else {
        is_configured = false;
    }
    k_work_submit(&usb_status_notifier_work);
};

static int zmk_usb_init(void) {
>>>>>>> ead91ca094c6cd778574f73b9ae011ddab611c66
    int usb_enable_ret;

    usb_enable_ret = usb_enable(usb_status_cb);

    if (usb_enable_ret != 0) {
        LOG_ERR("Unable to enable USB");
        return -EINVAL;
    }

    return 0;
}

SYS_INIT(zmk_usb_init, APPLICATION, CONFIG_ZMK_USB_INIT_PRIORITY);

uint32_t get_usb_sof_count(void)
{
    return usb_sof_count;
}