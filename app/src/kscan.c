/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/drivers/kscan.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/matrix_transform.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/matrix.h>

#define ZMK_KSCAN_EVENT_STATE_PRESSED 0
#define ZMK_KSCAN_EVENT_STATE_RELEASED 1
bool dip_switch_update_user(uint8_t index, bool active);
struct zmk_kscan_event {
    uint32_t row;
    uint32_t column;
    uint32_t state;
};
#ifdef CONFIG_EN_FIX_CAPS_ALT_DEL  
extern bool caps_ctrl_swap;
bool fix_caps_alt_del_en=false;
uint8_t caps_alt_del_pressed=0;
bool fix_ctrl_shift_tab_en=false;
uint8_t ctrl_shift_tab_pressed=0;
uint8_t option_cmd_pressed=0;
bool fix_option_cmd_left = false;
#if defined(CONFIG_SHIELD_KEYCHRON_B6_JIS_N)
#define CAPS_POSITION   63
#define WIN_POSITION    98
#define ALT_POSITION    99
#define DEL_POSITION    56
#define CTRL_POSITION   97
#define SHIFT_POSITION  80
#define TAB_POSITION    43
#define LEFT_POSITION   107
#elif defined(CONFIG_SHIELD_KEYCHRON_B6_US_N)
#define CAPS_POSITION   63
#define WIN_POSITION    96
#define ALT_POSITION    97
#define DEL_POSITION    56
#define CTRL_POSITION   95
#define SHIFT_POSITION  79
#define TAB_POSITION    42
#define LEFT_POSITION   103
#elif defined(CONFIG_SHIELD_KEYCHRON_B6_UK_N)
#define CAPS_POSITION   62
#define WIN_POSITION    97
#define ALT_POSITION    98
#define DEL_POSITION    55
#define CTRL_POSITION   96
#define SHIFT_POSITION  79
#define TAB_POSITION    42
#define LEFT_POSITION   104
#elif defined(CONFIG_SHIELD_KEYCHRON_B2_JIS_N)
#define CAPS_POSITION   54
#define WIN_POSITION    88
#define ALT_POSITION    89
#define DEL_POSITION    14
#define CTRL_POSITION   87
#define SHIFT_POSITION  71
#define TAB_POSITION    37
#define LEFT_POSITION   95
#elif defined(CONFIG_SHIELD_KEYCHRON_B2_US_N)
#define CAPS_POSITION   54
#define WIN_POSITION    86
#define ALT_POSITION    87
#define DEL_POSITION    14
#define CTRL_POSITION   85
#define SHIFT_POSITION  70
#define TAB_POSITION    36
#define LEFT_POSITION   91
#elif defined(CONFIG_SHIELD_KEYCHRON_B2_UK_N)
#define CAPS_POSITION   53
#define WIN_POSITION    87
#define ALT_POSITION    88
#define DEL_POSITION    14
#define CTRL_POSITION   86
#define SHIFT_POSITION  70
#define TAB_POSITION    36
#define LEFT_POSITION   92
#elif defined(CONFIG_SHIELD_KEYCHRON_B1_JIS_N)
#define CAPS_POSITION   42
#define WIN_POSITION    70
#define ALT_POSITION    71  
#define DEL_POSITION    13
#define CTRL_POSITION   69
#define SHIFT_POSITION  56
#define TAB_POSITION    29
#define LEFT_POSITION   77
#elif defined(CONFIG_SHIELD_KEYCHRON_B1_UK_N)
#define CAPS_POSITION   41
#define WIN_POSITION    69
#define ALT_POSITION    70
#define DEL_POSITION    13
#define CTRL_POSITION   68
#define SHIFT_POSITION  55
#define TAB_POSITION    28
#define LEFT_POSITION   74
#elif defined(CONFIG_SHIELD_KEYCHRON_B1_US_N)
#define CAPS_POSITION   42
#define WIN_POSITION    68
#define ALT_POSITION    69  
#define DEL_POSITION    13
#define CTRL_POSITION   67
#define SHIFT_POSITION  55
#define TAB_POSITION    28
#define LEFT_POSITION   73
#endif
#endif 
struct zmk_kscan_msg_processor {
    struct k_work work;
} msg_processor;

K_MSGQ_DEFINE(zmk_kscan_msgq, sizeof(struct zmk_kscan_event), CONFIG_ZMK_KSCAN_EVENT_QUEUE_SIZE, 4);

K_MSGQ_DEFINE(zmk_kscan_boot_msgq, sizeof(struct zmk_kscan_event), 40, 4);

static uint32_t matrix_rows[ZMK_MATRIX_ROWS];
static bool keyboard_ready;
void esc_boot_check(struct zmk_kscan_event *ev);
static void zmk_kscan_callback(const struct device *dev, uint32_t row, uint32_t column,
                               bool pressed) {
    struct zmk_kscan_event ev = {
        .row = row,
        .column = column,
        .state = (pressed ? ZMK_KSCAN_EVENT_STATE_PRESSED : ZMK_KSCAN_EVENT_STATE_RELEASED)};
    // report mac/win dip status.
    if (row == 8 && column == 0) {
        dip_switch_update_user(0, pressed ? 1 : 0);
    }
    if (!keyboard_ready) {
        if (row < 8) {
            esc_boot_check(&ev);
            k_msgq_put(&zmk_kscan_boot_msgq, &ev, K_NO_WAIT);

        } else {
            k_msgq_put(&zmk_kscan_msgq, &ev, K_NO_WAIT);
            k_work_submit(&msg_processor.work);
        }
    } 
    else 
    {
        k_msgq_put(&zmk_kscan_msgq, &ev, K_NO_WAIT);
        k_work_submit(&msg_processor.work);
    }
}

void zmk_kscan_process_msgq(struct k_work *item) {
    struct zmk_kscan_event ev;

    while (k_msgq_get(&zmk_kscan_msgq, &ev, K_NO_WAIT) == 0) {
        bool pressed = (ev.state == ZMK_KSCAN_EVENT_STATE_PRESSED);
        int32_t position = zmk_matrix_transform_row_column_to_position(ev.row, ev.column);

        if (position < 0) {
            LOG_WRN("Not found in transform: row: %d, col: %d, pressed: %s", ev.row, ev.column,
                    (pressed ? "true" : "false"));
            continue;
        }
#ifdef CONFIG_EN_FIX_CAPS_ALT_DEL  
        if(caps_ctrl_swap)
        {
            if(position == CAPS_POSITION)
            {
                if(pressed)
                {
                    caps_alt_del_pressed |= 0x01;
                }
                else
                {
                    
                    caps_alt_del_pressed &= ~0x01;
                }
            }
            else if(position == ALT_POSITION)
            {
                if(pressed)
                {
                    caps_alt_del_pressed |= 0x02;
                }
                else
                {
                    caps_alt_del_pressed &= ~0x02;
                }
            }
            // else if(position == DEL_POSITION)
            // {
            //     if(pressed)
            //     {
            //         caps_alt_del_pressed |= 0x04;
            //     }
            //     else
            //     {
            //         caps_alt_del_pressed &= ~0x04;
            //     }
            // }   
            if(caps_alt_del_pressed ==0x03)//|| caps_alt_del_pressed ==0x05 || caps_alt_del_pressed ==0x06)
            {
                fix_caps_alt_del_en =true;
                LOG_ERR("fix_caps_alt_del_en");
            }
            else 
            {
                fix_caps_alt_del_en =false;
            }
            
        }

        {
            LOG_ERR("pos:%d,pressed:%d,en:%d",position,pressed,fix_ctrl_shift_tab_en);
            if(position == CTRL_POSITION)
            {
                if(pressed)
                {
                    ctrl_shift_tab_pressed |= 0x01;
                }
                else
                {
                    
                    ctrl_shift_tab_pressed &= ~0x01;
                }
            }
            else if(position == SHIFT_POSITION)
            {
                if(pressed)
                {
                    ctrl_shift_tab_pressed |= 0x02;
                }
                else
                {
                    ctrl_shift_tab_pressed &= ~0x02;
                }
            }

            if(ctrl_shift_tab_pressed ==0x03)//|| ctrl_shift_tab_pressed ==0x05 || ctrl_shift_tab_pressed ==0x06)
            {
                fix_ctrl_shift_tab_en =true;
                LOG_ERR("fix_ctrl_shift_tab_en");
            }
            else 
            {
                fix_ctrl_shift_tab_en =false;
            }
        }
#if 1
        if (position == WIN_POSITION)
        {
            if(pressed)
            {
                option_cmd_pressed |= 0x01;
            }
            else
            {
                option_cmd_pressed &= ~0x01;
            }
        }
        else if(position == ALT_POSITION)
        {
            if(pressed)
            {
                option_cmd_pressed |= 0x02;
            }
            else
            {
                option_cmd_pressed &= ~0x02;
            }   
            
        }
        if(option_cmd_pressed ==0x03)
        {
            fix_option_cmd_left = true;
            LOG_ERR("fix_option_cmd_left_en");
        }
        else
        {
            fix_option_cmd_left = false;
        }   
#endif         
#endif
        if (pressed)
            matrix_rows[ev.row] |= 1 << ev.column;
        else
            matrix_rows[ev.row] &= ~(1 << ev.column);

        LOG_WRN("Row: %d, col: %d, position: %d, pressed: %s", ev.row, ev.column, position,
                (pressed ? "true" : "false"));
        ZMK_EVENT_RAISE(new_zmk_position_state_changed(
            (struct zmk_position_state_changed){.source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
                                                .state = pressed,
                                                .position = position,
                                                .timestamp = k_uptime_get()}));
    }
}

int zmk_kscan_init(const struct device *dev) {
    if (dev == NULL) {
        LOG_ERR("Failed to get the KSCAN device");
        return -EINVAL;
    }

    k_work_init(&msg_processor.work, zmk_kscan_process_msgq);

    kscan_config(dev, zmk_kscan_callback);
    kscan_enable_callback(dev);

    return 0;
}

uint32_t matrix_get_row(uint8_t row) { return matrix_rows[row]; }

void delay_boot_submit_cb(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(delay_boot_submit, delay_boot_submit_cb);

void add_kscan_boot_msgq(void) {
    if (k_msgq_num_used_get(&zmk_kscan_boot_msgq)) {
        LOG_ERR("-->add_kscan_boot_msgq:%d", k_msgq_num_used_get(&zmk_kscan_boot_msgq));
        struct zmk_kscan_event ev;
        while (k_msgq_get(&zmk_kscan_boot_msgq, &ev, K_NO_WAIT) == 0) {
            k_msgq_put(&zmk_kscan_msgq, &ev, K_NO_WAIT);
        }
    }
    keyboard_ready = true;
    k_work_reschedule(&delay_boot_submit, K_MSEC(50));
}
void delay_boot_submit_cb(struct k_work *work) { k_work_submit(&msg_processor.work); }

#include <zephyr/sys/reboot.h>
#include <hal/nrf_power.h>
#include <zephyr/logging/log_ctrl.h>

#define ESC_POSITION 0
void esc_boot_check(struct zmk_kscan_event *ev) {
    uint32_t boot_time = k_uptime_get_32();
    LOG_ERR("boot time:%d", boot_time);
    if (zmk_matrix_transform_row_column_to_position(ev->row, ev->column) == ESC_POSITION &&
        (boot_time < 150)) {
        LOG_ERR("esc_press_when_poweron");
        uint8_t type = nrf_power_gpregret_get(NRF_POWER);
        LOG_ERR("reboot type:%02x", type);
        LOG_PANIC();
        if (type == 0) {
            sys_reboot(0x4e); // only serial!
        }
    }
}