/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/bluetooth/addr.h>

#define ZMK_BLE_PROFILE_NAME_MAX 15

struct zmk_ble_profile {
    char name[ZMK_BLE_PROFILE_NAME_MAX];
    uint8_t bt_id : 4;
    uint8_t is_rpa : 2;
    uint8_t connected:1;
    uint8_t bonded:1;
    uint8_t fn_exchange;
    bt_addr_le_t peer;
};
