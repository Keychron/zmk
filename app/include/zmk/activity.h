/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

enum zmk_activity_state { ZMK_ACTIVITY_ACTIVE, ZMK_ACTIVITY_IDLE, ZMK_ACTIVITY_SLEEP };

enum zmk_activity_state zmk_activity_get_state();
int set_state(enum zmk_activity_state state);

typedef struct {
    uint16_t max_idle_time;
    uint16_t max_sleep_time;
} lpm_settings_t;
void update_lpm_set(uint16_t idle_time,uint16_t sleep_time);
void get_lpm_set(uint16_t * idle_time,uint16_t * sleep_time);
void set_lpm_set(uint16_t  idle_time,uint16_t  sleep_time);