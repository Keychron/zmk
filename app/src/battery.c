/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/bluetooth/services/bas.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, 4);//CONFIG_ZMK_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <zmk/battery.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/workqueue.h>
#include <zmk/leds.h>
#include <zmk/activity.h>
#include <zmk/usb.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/device.h>
#define BAT_VOLTAGE_LOW 3320
#define BAT_VOLTAGE_SHUTDOWN (3045)
void zmk_battery_check(void);
void check_voltage_when_boot(void);
bool is_usb_power_present(void);
static uint8_t last_state_of_charge = 100;
static bool bat_shutdown; 
static const struct device *adc = DEVICE_DT_GET(DT_NODELABEL(adc));
static uint16_t voltage;

uint8_t zmk_battery_state_of_charge() { return last_state_of_charge; }

#if DT_HAS_CHOSEN(zmk_battery)
static const struct device *const battery = DEVICE_DT_GET(DT_CHOSEN(zmk_battery));
#else
#warning                                                                                           \
    "Using a node labeled BATTERY for the battery sensor is deprecated. Set a zmk,battery chosen node instead. (Ignore this if you don't have a battery sensor.)"
static const struct device *battery;
#endif

extern struct bt_conn *destination_connection();
extern int bt_bas_set_battery_level_fix(struct bt_conn *conn,uint8_t level);
extern void bt_conn_unref(struct bt_conn *conn);

static int zmk_battery_update(const struct device *battery) {
    struct sensor_value state_of_charge;
    struct sensor_value sensor_voltage;

    pm_device_action_run(adc,PM_DEVICE_ACTION_RESUME);
    int rc = sensor_sample_fetch_chan(battery, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE);

    if (rc != 0) {
        LOG_DBG("Failed to fetch battery values: %d", rc);
        return rc;
    }

    rc = sensor_channel_get(battery, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE, &state_of_charge);

    if (rc != 0) {
        LOG_DBG("Failed to get battery state of charge: %d", rc);
        return rc;
    }
    rc = sensor_channel_get(battery, SENSOR_CHAN_GAUGE_VOLTAGE, &sensor_voltage);
    if (rc != 0) {
        LOG_DBG("Failed to fetch battery values: %d", rc);
        return rc;
    }
    voltage = sensor_voltage.val1*1000+ sensor_voltage.val2/1000;
    if(voltage >BAT_VOLTAGE_LOW)
    {
        bat_shutdown=false;
    }
    zmk_battery_check();

    if(state_of_charge.val1==0)
    {
        if(voltage >BAT_VOLTAGE_LOW)
        {
            state_of_charge.val1=2;
        }
        else if(voltage >BAT_VOLTAGE_SHUTDOWN)
        {
            state_of_charge.val1 =1;
        }
    }
    
    LOG_DBG("bat,last per:%d,cur:%d",last_state_of_charge,state_of_charge.val1);

    if (last_state_of_charge != state_of_charge.val1  && ((state_of_charge.val1 < last_state_of_charge) ||is_usb_power_present())) {
        last_state_of_charge = state_of_charge.val1;

        LOG_DBG("Setting BAS GATT battery level to %d.", last_state_of_charge);

        // rc = bt_bas_set_battery_level(last_state_of_charge);
        struct bt_conn *conn=destination_connection();
        rc = bt_bas_set_battery_level_fix(conn,last_state_of_charge);
        bt_conn_unref(conn);

        if (rc != 0) {
            LOG_WRN("Failed to set BAS GATT battery level (err %d)", rc);
            return rc;
        }

        rc = ZMK_EVENT_RAISE(new_zmk_battery_state_changed(
            (struct zmk_battery_state_changed){.state_of_charge = last_state_of_charge}));
    }
    pm_device_action_run(adc,PM_DEVICE_ACTION_SUSPEND);
    return rc;
}

static void zmk_battery_work(struct k_work *work) {
    int rc = zmk_battery_update(battery);

    if (rc != 0) {
        LOG_DBG("Failed to update battery value: %d.", rc);
    }
}

K_WORK_DEFINE(battery_work, zmk_battery_work);

static void zmk_battery_timer(struct k_timer *timer) {
    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &battery_work);
}

K_TIMER_DEFINE(battery_timer, zmk_battery_timer, NULL);

static int zmk_battery_init(const struct device *_arg) {
#if !DT_HAS_CHOSEN(zmk_battery)
    battery = device_get_binding("BATTERY");

    if (battery == NULL) {
        return -ENODEV;
    }

    LOG_WRN("Finding battery device labeled BATTERY is deprecated. Use zmk,battery chosen node.");
#endif

    if (!device_is_ready(battery)) {
        LOG_ERR("Battery device \"%s\" is not ready", battery->name);
        return -ENODEV;
    }

    k_timer_start(&battery_timer, K_SECONDS(1), K_SECONDS(CONFIG_ZMK_BATTERY_REPORT_INTERVAL));

    check_voltage_when_boot();
    pm_device_action_run(adc,PM_DEVICE_ACTION_SUSPEND);
    return 0;
}

SYS_INIT(zmk_battery_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);


void zmk_battery_check(void)
{
    static uint8_t check_count=0;   
    LOG_DBG("voltage:%d,count:%d,state:%d",voltage,check_count,get_charge_led_state());
    if(zmk_usb_is_powered()) return;
    if(voltage <BAT_VOLTAGE_LOW)
    {
        
        // led_charge_set_state(LED_BAT_LOW);
        if(voltage <BAT_VOLTAGE_SHUTDOWN)
        {
            check_count ++;
            if(check_count >2)
            {
                set_state(ZMK_ACTIVITY_SLEEP);
            }
        }
        else
        {
            check_count=0;
        }
    }
    else
    {
        if(get_charge_led_state()==LED_BAT_LOW)
            led_charge_set_state(LED_BAT_NONE);
    }

}

void check_voltage_when_boot(void)
{
    struct sensor_value sensor_voltage;
    uint16_t voltage=0;
    uint16_t max=0;
    uint16_t min=0xffff;
    uint32_t sum=0;
    int i;
    bat_shutdown =false;
    for(i=0;i<10;i++)
    {
        int rc = sensor_sample_fetch_chan(battery, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE);
        if (rc != 0) {
            LOG_DBG("Failed to fetch battery values: %d", rc);
            return ;
        }
        rc = sensor_channel_get(battery, SENSOR_CHAN_GAUGE_VOLTAGE, &sensor_voltage);
        if (rc != 0) {
            LOG_DBG("Failed to fetch battery values: %d", rc);
            return ;
        }
        voltage = sensor_voltage.val1*1000+ sensor_voltage.val2/1000;
        sum +=voltage;
        if(voltage>=max)
        {
            max = voltage ;
        }
        if(voltage<min)
        {
            min = voltage;
        }
        k_msleep(2);
        LOG_DBG("voltage:%d,max%d,min:%d",voltage,max,min);
    }

    sum -= max+min;
    sum /=8;
    LOG_DBG("ave voltage:%d",sum);
    if(sum <= BAT_VOLTAGE_SHUTDOWN && sum>2000)
    {
        
        LOG_ERR("bat low to shutdown");
        bat_shutdown =true;
    }

}
bool bat_is_shutdown(void)
{
    return bat_shutdown;
}

void clear_bat_shutdown(void)
{
    bat_shutdown =false;
}

void bat_low_check(void)
{
    static int64_t last_time=0;
    int64_t uptime= k_uptime_get();
    int64_t delta_time= uptime -last_time;
    if((delta_time>30*1000) ||(last_time==0))
    {
         if((voltage <BAT_VOLTAGE_LOW) && (voltage>BAT_VOLTAGE_SHUTDOWN) && !zmk_usb_is_powered())
        {
            led_charge_set_state(LED_BAT_LOW);
            last_time = uptime;
        }
    }

}