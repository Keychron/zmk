/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk,CONFIG_ZMK_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/sensor_event.h>

#include <zmk/activity.h>
#include <zmk/endpoints.h>
#include <hal/nrf_power.h>
#include <zmk/leds.h>

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
#include <zmk/usb.h>
#endif
bool all_keys_up(void);
bool is_usb_power_present() {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    return zmk_usb_is_powered();
#else
    return false;
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
}
extern struct k_work_delayable sleep_work;
static enum zmk_activity_state activity_state;

static uint32_t activity_last_uptime;

#define MAX_IDLE_MS CONFIG_ZMK_IDLE_TIMEOUT

#if IS_ENABLED(CONFIG_ZMK_SLEEP)
#define MAX_SLEEP_MS CONFIG_ZMK_IDLE_SLEEP_TIMEOUT
#endif

uint32_t max_sleep_time=MAX_SLEEP_MS;
uint32_t max_idle_time=MAX_IDLE_MS;

int raise_event() {
    return ZMK_EVENT_RAISE(new_zmk_activity_state_changed(
        (struct zmk_activity_state_changed){.state = activity_state}));
}

int set_state(enum zmk_activity_state state) {
    if (activity_state == state)
        return 0;

    activity_state = state;
    return raise_event();
}

enum zmk_activity_state zmk_activity_get_state() { return activity_state; }

int activity_event_listener(const zmk_event_t *eh) {
    activity_last_uptime = k_uptime_get();
    struct zmk_position_state_changed *pos_state;
    
    pos_state = as_zmk_position_state_changed(eh);
    if(pos_state)
    {
        if(pos_state->state)
        {
            void bat_low_check(void);
            bat_low_check();
        }
    }
    return set_state(ZMK_ACTIVITY_ACTIVE);
}

void activity_work_handler(struct k_work *work) {
    int32_t current = k_uptime_get();
    int32_t inactive_time = current - activity_last_uptime;
#if IS_ENABLED(CONFIG_ZMK_SLEEP)
    // LOG_ERR("inactive:%d,max sleep:%d,state:%d,allkeyup:%d",inactive_time,max_sleep_time,activity_state,all_keys_up());
    if ((inactive_time > max_sleep_time || activity_state == ZMK_ACTIVITY_SLEEP)  && all_keys_up()) {
        // Put devices in suspend power mode before sleeping
        LOG_ERR("prepare sleep");
        if (zmk_endpoints_selected().transport != ZMK_TRANSPORT_USB && get_charge_led_state()!=LED_BAT_CHARGING && get_charge_led_state()!= LED_BAT_CHARGE_DONE)
        {
            void leds_turnoff(void);
            leds_turnoff();
            void kscan_gpio_direct_enter_sleep(void);
            kscan_gpio_direct_enter_sleep();
            LOG_DBG("ZMK_ACTIVITY_SLEEP");
            set_state(ZMK_ACTIVITY_SLEEP);
            nrf_power_gpregret2_set(NRF_POWER, 0x02);//REBOOT_SLEEP);
            pm_state_force(0U, &(struct pm_state_info){PM_STATE_SOFT_OFF, 0, 0});
        }
        else
        {
            void zmk_24g_endpoint_disconnect(void);
            void zmk_ble_disconn_active_profile(void);
            if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_BLE)
            {
                if(zmk_ble_is_connected())
                {
                    zmk_ble_disconn_active_profile();
                }
            }
            else if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_24G)
            {
                bool zmk_24g_is_connected(void);
                if(zmk_24g_is_connected())
                {
                    zmk_24g_endpoint_disconnect();
                }
            }
            LOG_ERR("disconnect");
            k_work_reschedule(&sleep_work,K_MSEC(10000));
        }

    } else
#endif /* IS_ENABLED(CONFIG_ZMK_SLEEP) */
        if (inactive_time > max_idle_time) {
            set_state(ZMK_ACTIVITY_IDLE);
        }
}

K_WORK_DEFINE(activity_work, activity_work_handler);

void activity_expiry_function() { k_work_submit(&activity_work); }

K_TIMER_DEFINE(activity_timer, activity_expiry_function, NULL);

int activity_init() {
    activity_last_uptime = k_uptime_get();

    k_timer_start(&activity_timer, K_SECONDS(1), K_SECONDS(1));
    return 0;
}

ZMK_LISTENER(activity, activity_event_listener);
ZMK_SUBSCRIPTION(activity, zmk_position_state_changed);
ZMK_SUBSCRIPTION(activity, zmk_sensor_event);

SYS_INIT(activity_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#include <zephyr/settings/settings.h>
lpm_settings_t lpm_set ={
    .max_idle_time = CONFIG_ZMK_IDLE_TIMEOUT/1000,
    .max_sleep_time = CONFIG_ZMK_IDLE_SLEEP_TIMEOUT/1000
};
void update_lpm_set(uint16_t idle_time,uint16_t sleep_time)
{
    max_idle_time = idle_time *1000;
    max_sleep_time = sleep_time *1000;
}

void get_lpm_set(uint16_t * idle_time,uint16_t * sleep_time)
{
    *idle_time = lpm_set.max_idle_time;
    *sleep_time = lpm_set.max_sleep_time;
}
void set_lpm_set(uint16_t  idle_time,uint16_t  sleep_time)
{
    lpm_set.max_idle_time=idle_time ;
    lpm_set.max_sleep_time=sleep_time ;
    update_lpm_set(lpm_set.max_idle_time,lpm_set.max_sleep_time);
    settings_save_one("via_ee/lpm_set",&lpm_set,sizeof(lpm_set));
    activity_last_uptime = k_uptime_get();
}
