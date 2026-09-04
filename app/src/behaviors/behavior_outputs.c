/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_outputs

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <drivers/behavior.h>

#include <dt-bindings/zmk/outputs.h>

#include <zmk/behavior.h>
#include <zmk/endpoints.h>
#include <zmk/leds.h>
#include <hal/nrf_power.h>
#include <zephyr/sys/reboot.h>
#include <zmk/usb.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, 4); // CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static uint8_t bat_charge_state;
static uint8_t bat_charge_done;
static uint8_t hardware_select_transport;
static uint8_t fn_win_lock;
extern void do_f1_f13_fn_exchange(void);
void bt_24g_switch_pulldown(uint8_t current);
void bt_24g_switch_reset(void);
uint8_t skip_switch_change(void);
bool keyboard_os_is_mac(void);
void clear_bat_shutdown(void);
void reset_led_power_on(void);
uint8_t get_mode_status(void);
void save_fn_win_lock(uint8_t winlock);

static uint8_t reboot_status;
enum {
    BIT_SLEEP=0x02,
    BIT_USB=0x04,
    BIT_OFF=0x10,
};
void reboot_status_set(uint8_t bits)
{
    reboot_status =nrf_power_gpregret2_get(NRF_POWER);
    reboot_status |= bits;
    nrf_power_gpregret2_set(NRF_POWER,reboot_status);
}
void reboot_status_clear(uint8_t bits)
{
    reboot_status =nrf_power_gpregret2_get(NRF_POWER);
    reboot_status &= ~bits;
    nrf_power_gpregret2_set(NRF_POWER,reboot_status);
}

uint8_t get_hardware_select_transport(void) { return hardware_select_transport; }
bool get_fn_win_lock(void) { return !keyboard_os_is_mac() && fn_win_lock; }
void set_fn_win_lock(uint8_t lock) { fn_win_lock = lock; }
static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    switch (binding->param1) {
    case OUT_TOG:
        return zmk_endpoints_toggle_transport();
    case OUT_USB:
        return zmk_endpoints_select_transport(ZMK_TRANSPORT_USB);
    case OUT_BLE: {
        LOG_ERR("out ble");
        // reset_led_power_on();
        hardware_select_transport = ZMK_TRANSPORT_BLE;
        // nrf_power_gpregret_set(NRF_POWER,REBOOT_ENDPOINT_BLE);
        // if(zmk_usb_is_powered()) return 0;
        uint8_t type = nrf_power_gpregret_get(NRF_POWER);
        LOG_DBG("reboot type:%02x", type);
        if (type == REBOOT_ENDPOINT_BLE || type ==0) {
            // bt_24g_switch_pulldown(0);
            nrf_power_gpregret_set(NRF_POWER, REBOOT_ENDPOINT_BLE);
            zmk_endpoints_select_transport(ZMK_TRANSPORT_BLE);

        } else {
            nrf_power_gpregret_set(NRF_POWER, REBOOT_ENDPOINT_BLE);
            // nrf_power_gpregret2_set(NRF_POWER, REBOOT_SLEEP);
            LOG_ERR("set:%x", REBOOT_ENDPOINT_BLE);
            LOG_DBG("REBOOT");
            reset_led_power_on();
            k_msleep(200);
            sys_reboot(REBOOT_ENDPOINT_BLE);
        }
        return 0;
    }
    case OUT_24G: {
        LOG_ERR("out 24g");
        // reset_led_power_on();
        hardware_select_transport = ZMK_TRANSPORT_24G;
        // nrf_power_gpregret_set(NRF_POWER,REBOOT_ENDPOINT_24G);
        // if(zmk_usb_is_powered()) return 0;
        uint8_t type = nrf_power_gpregret_get(NRF_POWER);

        LOG_DBG("reboot type:%02x", type);
        if (type == REBOOT_ENDPOINT_24G || type ==0) {
            nrf_power_gpregret_set(NRF_POWER, REBOOT_ENDPOINT_24G);
            // bt_24g_switch_pulldown(1);
            zmk_endpoints_select_transport(ZMK_TRANSPORT_24G);

        } else {
            nrf_power_gpregret_set(NRF_POWER, REBOOT_ENDPOINT_24G);
            // nrf_power_gpregret2_set(NRF_POWER, REBOOT_SLEEP);
            LOG_ERR("set:%x", REBOOT_ENDPOINT_24G);
            LOG_DBG("REBOOT");
            reset_led_power_on();
            k_msleep(200);
            sys_reboot(REBOOT_ENDPOINT_24G);
        }
        return 0;
    }
    case OUT_CHG:
        LOG_ERR("charging");
        led_charge_set_state(LED_BAT_CHARGING);
        bat_charge_state = 1;
        // bt_24g_switch_reset();
        clear_bat_shutdown();
        reboot_status_set(BIT_USB);
        return 0;
        ;
    case OUT_CHGD:
        LOG_ERR("charge done");
        led_charge_set_state(LED_BAT_CHARGE_DONE);
        reboot_status_set(BIT_USB);
        bat_charge_done = 1;
        // bt_24g_switch_reset();
        return 0;
    case OUT_BAT:
        led_bat_display();
        return 0;
    case OUT_FN:
        do_f1_f13_fn_exchange();
        return 0;
    case OUT_RECOVER:
        void zmk_factory_recover(void);
        zmk_factory_recover();
        return 0;
    case OUT_FN_WIN: {
        fn_win_lock = ~fn_win_lock;
        LOG_ERR("fn win lock:%d", fn_win_lock);
        save_fn_win_lock(fn_win_lock);
    }
        return 0;
    default:
        LOG_ERR("Unknown output command: %d", binding->param1);
    }

    return -ENOTSUP;
}
void zmk_24g_endpoint_disconnect(void);
void zmk_ble_endpoint_disconnect(void);

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {

    switch (binding->param1) {
    case OUT_TOG:
    case OUT_USB:
        break;
    case OUT_BLE:
        LOG_ERR("switch off:%d", binding->param1);
        reboot_status_clear(BIT_SLEEP);
        if((reboot_status&BIT_USB)==0)
            reboot_status_set(BIT_OFF);
        zmk_ble_endpoint_disconnect();
        if (zmk_usb_is_powered() && get_mode_status() == 0x03) {
            hardware_select_transport = ZMK_TRANSPORT_USB;
            reset_led_power_on();
            zmk_endpoints_select_transport(ZMK_TRANSPORT_USB);
        } else {
            LOG_ERR("select transport none");
            zmk_endpoints_select_transport(ZMK_TRANSPORT_NONE);
        }
        return 0;
    case OUT_24G:
        LOG_ERR("switch off:%d", binding->param1);
        reboot_status_clear(BIT_SLEEP);
        if((reboot_status&BIT_USB)==0)
            reboot_status_set(BIT_OFF);
        // if(skip_switch_change())
        // {
        //     LOG_ERR("skip");
        //     return 0;
        // }
        zmk_24g_endpoint_disconnect();
        if (zmk_usb_is_powered() && get_mode_status() == 0x03) {
            hardware_select_transport = ZMK_TRANSPORT_USB;
            reset_led_power_on();
            zmk_endpoints_select_transport(ZMK_TRANSPORT_USB);
        } else {
            LOG_ERR("select transport none");
            zmk_endpoints_select_transport(ZMK_TRANSPORT_NONE);
        }
        return 0;
        // hardware_select_transport=ZMK_TRANSPORT_USB;
        // return zmk_endpoints_select_transport(ZMK_TRANSPORT_USB);
    case OUT_CHG:
        LOG_ERR("not charging");
        led_charge_set_state(LED_BAT_NONE);
        reboot_status_clear(BIT_USB);
        bat_charge_state = 0;
        // if(bat_charge_done==0)
        // {
        //     LOG_ERR("reset pulldown");
        //     if(get_current_transport()==ZMK_TRANSPORT_BLE)
        //     {
        //         bt_24g_switch_pulldown(0);
        //     }
        //     else if(get_current_transport()==ZMK_TRANSPORT_24G)
        //     {
        //         bt_24g_switch_pulldown(1);
        //     }
        // }
        return 0;
    case OUT_CHGD:
        LOG_ERR("not charge done");
        if (bat_charge_state)
        {
            led_charge_set_state(LED_BAT_CHARGING);
            reboot_status_set(BIT_USB);  
        }
        else {
            led_charge_set_state(LED_BAT_NONE);
            reboot_status_clear(BIT_USB);
        }
        bat_charge_done = 0;
        // if(bat_charge_state==0)
        // {
        //     LOG_ERR("reset pulldown");
        //     if(get_current_transport()==ZMK_TRANSPORT_BLE)
        //     {
        //         bt_24g_switch_pulldown(0);
        //     }
        //     else if(get_current_transport()==ZMK_TRANSPORT_24G)
        //     {
        //         bt_24g_switch_pulldown(1);
        //     }
        // }
        return 0;
    case OUT_BAT:
        led_bat_display_off();
        return 0;
    case OUT_FN_WIN:
        return 0;

    default:
        LOG_ERR("Unknown output command: %d", binding->param1);
    }

    return -ENOTSUP;
}
static int behavior_out_init(const struct device *dev) { return 0; }

static const struct behavior_driver_api behavior_outputs_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
};

DEVICE_DT_INST_DEFINE(0, behavior_out_init, NULL, NULL, NULL, APPLICATION,
                      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_outputs_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
