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
#include <zmk/event_manager.h>
#include <zmk/endpoints.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

void keyboad_led_set_onoff(uint8_t led_state);
uint32_t get_usb_sof_count(void);

static const struct device *hid_dev;

static K_SEM_DEFINE(hid_sem, 1, 1);
static uint8_t zmk_usb_protocol=HID_PROTOCOL_REPORT;
void zmk_usb_set_protocol_report(void)
{
    zmk_usb_protocol=HID_PROTOCOL_REPORT;
}
void zmk_usb_protocol_change(const struct device *dev, uint8_t protocol)
{
    zmk_usb_protocol =protocol;
    LOG_DBG("usb protocol:%d",protocol);
}
static void out_ready_cb(const struct device *dev)
{
    uint8_t rev_buf[32]={0};
    uint32_t rev_bytes =0;
    hid_int_ep_read(dev,rev_buf,sizeof(rev_buf),&rev_bytes);
    LOG_HEXDUMP_DBG(rev_buf,rev_bytes,"rx");
    if(rev_buf[0]==ZMK_HID_REPORT_ID_KEYBOARD)
    {
        if(get_current_transport()==ZMK_TRANSPORT_USB)
            keyboad_led_set_onoff(rev_buf[1]);
    }
}

static void in_ready_cb(const struct device *dev) { k_sem_give(&hid_sem); }
static int zmk_usb_hid_set_report(const struct device *dev, struct usb_setup_packet *setup,
                                  int32_t *len, uint8_t **data) {
    LOG_HEXDUMP_DBG(*data, *len, "set report");
    uint8_t *report=*data;
    if(report[0]==ZMK_HID_REPORT_ID_KEYBOARD)
    {
        keyboad_led_set_onoff(report[1]);
    }
    return 0;
}
static const struct hid_ops ops = {
    .int_in_ready = in_ready_cb,
    .int_out_ready = out_ready_cb,
    .set_report = zmk_usb_hid_set_report,
    .protocol_change =zmk_usb_protocol_change
};

int zmk_usb_hid_send_report(const uint8_t *report, size_t len) {
    switch (zmk_usb_get_status()) {
   
    case USB_DC_ERROR:
    case USB_DC_RESET:
    case USB_DC_DISCONNECTED:
    case USB_DC_UNKNOWN:
        return -ENODEV;
    case USB_DC_SUSPEND:
         usb_wakeup_request();

         for(int i=0;i<50;i++)
         {
            k_msleep(100);
            if(!usb_get_remote_wakeup_status()|| get_usb_sof_count()>100)
            {
                LOG_ERR("i:%d,sof:%d",i,get_usb_sof_count());
                break;
            }
         }
    default:
        int sem_err=k_sem_take(&hid_sem, K_MSEC(100));
        if(sem_err)
        {
            LOG_ERR("sem err:%d",sem_err);
        }
        LOG_HEXDUMP_DBG(report,len,"usb");
        int err;
        if(zmk_usb_protocol == HID_PROTOCOL_REPORT)
        {
#if CONFIG_ADAPATIVE_NKRO            
            if(report[0]==ZMK_HID_REPORT_ID_KEYBOARD_NKRO)
            {
                uint8_t buffer[(ZMK_HID_KEYBOARD_NKRO_MAX_USAGE + 1) / 8+2];
                buffer[0]=report[0];
                buffer[1]=report[1];
                memcpy(buffer+2,report+3,(ZMK_HID_KEYBOARD_NKRO_MAX_USAGE + 1) / 8);
                // LOG_HEXDUMP_ERR(buffer,len-1,"nkro");
                err =hid_int_ep_write(hid_dev, buffer, len-1, NULL);
            }
            else
#endif     
             err = hid_int_ep_write(hid_dev, report, len, NULL);
        }
        else
        {
        
            err = hid_int_ep_write(hid_dev, &report[1], 8, NULL);
        }

        if (err) {
            LOG_ERR("err:%d",err);
            k_sem_give(&hid_sem);
        }

        return err;
    }
}

static int zmk_usb_hid_init(const struct device *_arg) {
    hid_dev = device_get_binding("HID_0");
    if (hid_dev == NULL) {
        LOG_ERR("Unable to locate HID device");
        return -EINVAL;
    }

    usb_hid_register_device(hid_dev, zmk_hid_report_desc, sizeof(zmk_hid_report_desc), &ops);
    usb_hid_init(hid_dev);
    usb_hid_set_proto_code(hid_dev,1);//set boot keyboard!
    return 0;
}

SYS_INIT(zmk_usb_hid_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
