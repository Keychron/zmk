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

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);
void bt_24g_switch_reset(void);
void bt_24g_switch_pulldown(uint8_t current);
void zmk_usb_set_protocol_report(void);
void keyboad_led_set_onoff(uint8_t led_state);
uint8_t keyboard_get_led_state(void);
uint32_t usb_sof_count=0;
static enum usb_dc_status_code usb_status = USB_DC_UNKNOWN;

static void raise_usb_status_changed_event(struct k_work *_work) {
    ZMK_EVENT_RAISE(new_zmk_usb_conn_state_changed(
        (struct zmk_usb_conn_state_changed){.conn_state = zmk_usb_get_conn_state()}));
}

K_WORK_DEFINE(usb_status_notifier_work, raise_usb_status_changed_event);

enum usb_dc_status_code zmk_usb_get_status() { return usb_status; }

enum zmk_usb_conn_state zmk_usb_get_conn_state() {
    // LOG_DBG("state: %d", usb_status);
    switch (usb_status) {
    // case USB_DC_SUSPEND:
    //     LOG_DBG("USB_DC_SUSPEND");
    case USB_DC_CONFIGURED:
    case USB_DC_RESUME:
    case USB_DC_CLEAR_HALT:        
        return ZMK_USB_CONN_HID;

    case USB_DC_DISCONNECTED:
    case USB_DC_UNKNOWN:
        return ZMK_USB_CONN_NONE;

    default:
        return ZMK_USB_CONN_POWERED;
    }
}

void usb_status_cb(enum usb_dc_status_code status, const uint8_t *params) {
    static uint8_t led_bak_state=0;
    usb_status = status;
    // if (status == USB_DC_SOF) {
    //     usb_sof_count++;
    //     return;
    // }
    switch(status)
    {
        case USB_DC_RESET:
            // bt_24g_switch_reset();
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
        // if(get_current_transport()==ZMK_TRANSPORT_BLE)
        // {
        //     bt_24g_switch_pulldown(0);
        // }
        // else if(get_current_transport()==ZMK_TRANSPORT_24G)
        // {
        //     bt_24g_switch_pulldown(1);
        // }
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