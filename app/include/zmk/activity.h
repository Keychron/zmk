/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

enum zmk_activity_state { ZMK_ACTIVITY_ACTIVE, ZMK_ACTIVITY_IDLE, ZMK_ACTIVITY_SLEEP };

<<<<<<< HEAD
enum zmk_activity_state zmk_activity_get_state();
int set_state(enum zmk_activity_state state);
=======
enum zmk_activity_state zmk_activity_get_state(void);
>>>>>>> ead91ca094c6cd778574f73b9ae011ddab611c66
