/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/settings/settings.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/matrix.h>
#include <zmk/kscan.h>
#include <zmk/display.h>
#include <drivers/ext_power.h>
#include <zephyr/kernel.h>
#include <zmk/activity.h>
#include <zmk/usb.h>
#include <zmk/endpoints.h>

void f1_f13_fn_exchange_start_check(void);
bool bat_is_shutdown(void);
void clear_bat_shutdown(void);
int zmk_ble_init(const struct device *_arg);
void zmk_24g_init(void);

void main(void) {
    LOG_INF("Welcome to ZMK!\n");

    if (zmk_kscan_init(DEVICE_DT_GET(ZMK_MATRIX_NODE_ID)) != 0) {
        return;
    }
    uint8_t exit_shutdown = 0;
    uint8_t bat_shutdown = 0;

    while (1) {
        if (bat_is_shutdown()) {
            bat_shutdown = 1;
            if (zmk_usb_is_powered()) {
                exit_shutdown = 1;
            }
        } else {
            exit_shutdown = 1;
        }
        if (exit_shutdown) {
            exit_shutdown = 0;
            LOG_ERR("Exit shutdown!");
            if (bat_shutdown) {
                LOG_ERR("bat low ,Exit shutdown!");
                bat_shutdown = 0;
                clear_bat_shutdown();
                if (get_current_transport() == ZMK_TRANSPORT_BLE) {
                    zmk_ble_init(NULL);
                } else if (get_current_transport() == ZMK_TRANSPORT_24G) {
                    zmk_24g_init();
                }
            }
            set_state(ZMK_ACTIVITY_ACTIVE);
            break;
        }

        LOG_ERR("shutdown now");
        k_msleep(100);
        set_state(ZMK_ACTIVITY_SLEEP);
    }

#ifdef CONFIG_ZMK_DISPLAY
    zmk_display_init();
#endif /* CONFIG_ZMK_DISPLAY */

    k_msleep(20);
    // f1_f13_fn_exchange_start_check();
}
#include <zephyr/drivers/kscan.h>
void kscan_read(void)
{
    for(int i=0;i<2;i++)
    kscan_enable_callback(DEVICE_DT_GET(DT_NODELABEL(kscan0)));
}
static int zmk_keyboard_fast_init(const struct device *_arg) {

    if (zmk_kscan_init(DEVICE_DT_GET(DT_NODELABEL(kscan0))) != 0) {
        return -1;
    }
    LOG_ERR("zmk_keyboard_fast_init");
    return 0;
}
SYS_INIT(zmk_keyboard_fast_init, APPLICATION, 41);