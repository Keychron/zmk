/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "zmk/keys.h"
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/hid.h>
#include <dt-bindings/zmk/modifiers.h>
#include <zmk/endpoints.h>

static struct zmk_hid_keyboard_report keyboard_report = {
    .report_id = ZMK_HID_REPORT_ID_KEYBOARD, .body = {.modifiers = 0, ._reserved = 0, .keys = {0}}};

static struct zmk_hid_consumer_report consumer_report = {.report_id = ZMK_HID_REPORT_ID_CONSUMER,
                                                         .body = {.keys = {0}}};

// Keep track of how often a modifier was pressed.
// Only release the modifier if the count is 0.
static int explicit_modifier_counts[8] = {0, 0, 0, 0, 0, 0, 0, 0};
static zmk_mod_flags_t explicit_modifiers = 0;
static zmk_mod_flags_t implicit_modifiers = 0;
static zmk_mod_flags_t masked_modifiers = 0;

extern uint8_t macro_running;
bool get_fn_win_lock(void);

#define SET_MODIFIERS(mods)                                                                        \
    {                                                                                              \
        keyboard_report.body.modifiers = (mods & ~masked_modifiers) | implicit_modifiers;          \
        LOG_DBG("Modifiers set to 0x%02X", keyboard_report.body.modifiers);                        \
    }

#define GET_MODIFIERS (keyboard_report.body.modifiers)

#if CONFIG_ADAPATIVE_NKRO
struct zmk_adapative_nkro adapative_nkro;
// struct zmk_hid_keyboard_report nkro_24g_rpt;
static ATOMIC_DEFINE(report_changed, 2);
bool nkro_changed(void)
{
    return atomic_test_and_clear_bit(report_changed,NKRO_RPT);
}
bool kb_changed(void)
{
    return atomic_test_and_clear_bit(report_changed,KB_RPT);
}
#endif 

zmk_mod_flags_t zmk_hid_get_explicit_mods() { return explicit_modifiers; }

int zmk_hid_register_mod(zmk_mod_t modifier) {
    if(get_fn_win_lock() && (modifier == 7 || modifier ==3)) 
    {
        explicit_modifier_counts[modifier]=0;
        LOG_ERR("not send modifier:%d",modifier);
        return 0;
    }
    
    explicit_modifier_counts[modifier]++;
    LOG_DBG("Modifier %d count %d", modifier, explicit_modifier_counts[modifier]);
    WRITE_BIT(explicit_modifiers, modifier, true);
    zmk_mod_flags_t current = GET_MODIFIERS;
    SET_MODIFIERS(explicit_modifiers);
#if CONFIG_ADAPATIVE_NKRO
    int ret = current == GET_MODIFIERS ? 0 : 1;
    if(ret)
        atomic_set_bit(report_changed,KB_RPT);
    return ret;
#else       
    return current == GET_MODIFIERS ? 0 : 1;
#endif     
}

int zmk_hid_unregister_mod(zmk_mod_t modifier) {
    if (explicit_modifier_counts[modifier] <= 0) {
        LOG_ERR("Tried to unregister modifier %d too often", modifier);
        return -EINVAL;
    }
    explicit_modifier_counts[modifier]--;

    if(macro_running)
    {
        explicit_modifier_counts[modifier]=0;
    }

    LOG_DBG("Modifier %d count: %d", modifier, explicit_modifier_counts[modifier]);
    if (explicit_modifier_counts[modifier] == 0) {
        LOG_DBG("Modifier %d released", modifier);
        WRITE_BIT(explicit_modifiers, modifier, false);
    }
    zmk_mod_flags_t current = GET_MODIFIERS;
    SET_MODIFIERS(explicit_modifiers);
#if CONFIG_ADAPATIVE_NKRO
    int ret = current == GET_MODIFIERS ? 0 : 1;
    if(ret)
        atomic_set_bit(report_changed,KB_RPT);
    return ret;
#else       
    return current == GET_MODIFIERS ? 0 : 1;
#endif     
}

bool zmk_hid_mod_is_pressed(zmk_mod_t modifier) {
    zmk_mod_flags_t mod_flag = 1 << modifier;
    return (zmk_hid_get_explicit_mods() & mod_flag) == mod_flag;
}

int zmk_hid_register_mods(zmk_mod_flags_t modifiers) {
    int ret = 0;
    for (zmk_mod_t i = 0; i < 8; i++) {
        if (modifiers & (1 << i)) {
            ret += zmk_hid_register_mod(i);
        }
    }
    return ret;
}

int zmk_hid_unregister_mods(zmk_mod_flags_t modifiers) {
    int ret = 0;
    for (zmk_mod_t i = 0; i < 8; i++) {
        if (modifiers & (1 << i)) {
            ret += zmk_hid_unregister_mod(i);
        }
    }

    return ret;
}
#if CONFIG_ADAPATIVE_NKRO
static inline int TOGGLE_KEYBOARD_nkro(uint16_t code, uint16_t val,uint8_t nkro)
{
    
    if(nkro)
    {
        if(val) {
            adapative_nkro.nkro_bits_count ++;
            atomic_set_bit(report_changed,NKRO_RPT);
            adapative_nkro.nkro_report.body._reserved =1;
            WRITE_BIT(adapative_nkro.nkro_report.body.keys[code / 8], code % 8, val);
        }
        else {
            if(adapative_nkro.nkro_report.body.keys[code/8] & BIT(code%8)){
                WRITE_BIT(adapative_nkro.nkro_report.body.keys[code / 8], code % 8, val);
                adapative_nkro.nkro_bits_count --;
                atomic_set_bit(report_changed,NKRO_RPT);
                adapative_nkro.nkro_report.body._reserved =1;
                return 1;
            }
        }
        
    }
    else
    {
        for (int idx = 0; idx < CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE; idx++) 
        {                          
            if (keyboard_report.body.keys[idx] != code) {                                             
                continue;                                                                              
            }                                                                                          
            keyboard_report.body.keys[idx] = val; 
            
            
            if (val) {
                adapative_nkro.kb_keys_count++;   
                break;                                                                                 
            }  
            else
            {
                adapative_nkro.kb_keys_count--;
            }                                                                                        
        }
    }
    return 0;
}
// static inline int TOGGLE_KEYBOARD_nkro_24g(uint16_t code, uint16_t val)
// {
//     if(val) {
//         WRITE_BIT(nkro_24g_rpt.body.keys[code / 8], code % 8, val);
//         return 0;
//     }
//     else {
//         if(nkro_24g_rpt.body.keys[code/8] & BIT(code%8)){
//             WRITE_BIT(nkro_24g_rpt.body.keys[code / 8], code % 8, val);
//             return 1;
//         }
//     }
//     return 0;
// }
#endif 
#if IS_ENABLED(CONFIG_ZMK_HID_REPORT_TYPE_NKRO)

#define TOGGLE_KEYBOARD(code, val) WRITE_BIT(keyboard_report.body.keys[code / 8], code % 8, val)

static inline int select_keyboard_usage(zmk_key_t usage) {
    if (usage > ZMK_HID_KEYBOARD_NKRO_MAX_USAGE) {
        return -EINVAL;
    }
    TOGGLE_KEYBOARD(usage, 1);
    return 0;
}

static inline int deselect_keyboard_usage(zmk_key_t usage) {
    if (usage > ZMK_HID_KEYBOARD_NKRO_MAX_USAGE) {
        return -EINVAL;
    }
    TOGGLE_KEYBOARD(usage, 0);
    return 0;
}

static inline bool check_keyboard_usage(zmk_key_t usage) {
    if (usage > ZMK_HID_KEYBOARD_NKRO_MAX_USAGE) {
        return false;
    }
    return keyboard_report.body.keys[usage / 8] & (1 << (usage % 8));
}

#elif IS_ENABLED(CONFIG_ZMK_HID_REPORT_TYPE_HKRO)

#define TOGGLE_KEYBOARD(match, val)                                                                \
    for (int idx = 0; idx < CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE; idx++) {                          \
        if (keyboard_report.body.keys[idx] != match  && keyboard_report.body.keys[idx] != val ) {                                             \
            continue;                                                                              \
        }                                                                                          \
        keyboard_report.body.keys[idx] = val;                                                      \
        if (val) {                                                                                 \
            break;                                                                                 \
        }                                                                                          \
    }

static inline int select_keyboard_usage(zmk_key_t usage) {
#if CONFIG_ADAPATIVE_NKRO
    // if(zmk_endpoints_selected().transport == ZMK_TRANSPORT_24G)
    //     return TOGGLE_KEYBOARD_nkro_24g(usage,1);

    if(adapative_nkro.kb_keys_count == CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE)
    {
        if (usage > ZMK_HID_KEYBOARD_NKRO_MAX_USAGE) {
            return -EINVAL;
        }
        TOGGLE_KEYBOARD_nkro(usage, 1,1);
    }
    else
    {
        TOGGLE_KEYBOARD_nkro(0U, usage,0);
        
        atomic_set_bit(report_changed,KB_RPT);
    }
    LOG_ERR("kb keys:%d,%d",adapative_nkro.kb_keys_count,adapative_nkro.nkro_bits_count);
#else    
    TOGGLE_KEYBOARD(0U, usage);
#endif     
    return 0;
}

static inline int deselect_keyboard_usage(zmk_key_t usage) {
#if CONFIG_ADAPATIVE_NKRO
    // if(zmk_endpoints_selected().transport == ZMK_TRANSPORT_24G)
    //     return TOGGLE_KEYBOARD_nkro_24g(usage,0);

    if(adapative_nkro.nkro_bits_count)
    {
        if (usage > ZMK_HID_KEYBOARD_NKRO_MAX_USAGE) {
            return -EINVAL;
        }
        if(TOGGLE_KEYBOARD_nkro(usage, 0,1)) return 0;

    }

    TOGGLE_KEYBOARD_nkro(usage, 0U,0);
    atomic_set_bit(report_changed,KB_RPT);
    LOG_ERR("kb keys:%d,%d",adapative_nkro.kb_keys_count,adapative_nkro.nkro_bits_count);
#else
    TOGGLE_KEYBOARD(usage, 0U);
#endif 
    return 0;
}

static inline int check_keyboard_usage(zmk_key_t usage) {
#if CONFIG_ADAPATIVE_NKRO
    // if(zmk_endpoints_selected().transport == ZMK_TRANSPORT_24G)
    // {
    //     if (usage > ZMK_HID_KEYBOARD_NKRO_MAX_USAGE) {
    //         return false;
    //     }
    //      return nkro_24g_rpt.body.keys[usage/8] & (1<< (usage%8));
    // }

    if(adapative_nkro.nkro_bits_count)
    {
         if (usage > ZMK_HID_KEYBOARD_NKRO_MAX_USAGE) {
            return false;
        }
        return adapative_nkro.nkro_report.body.keys[usage/8] & (1<< (usage%8));
    }
    else
    {
        for (int idx = 0; idx < CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE; idx++) {
            if (keyboard_report.body.keys[idx] == usage) {
                return true;
            }
        }
        return false;
    }
#else       
    for (int idx = 0; idx < CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE; idx++) {
        if (keyboard_report.body.keys[idx] == usage) {
            return true;
        }
    }
    return false;
#endif 
}

#else
#error "A proper HID report type must be selected"
#endif

#define TOGGLE_CONSUMER(match, val)                                                                \
    COND_CODE_1(IS_ENABLED(CONFIG_ZMK_HID_CONSUMER_REPORT_USAGES_BASIC),                           \
                (if (val > 0xFF) { return -ENOTSUP; }), ())                                        \
    for (int idx = 0; idx < CONFIG_ZMK_HID_CONSUMER_REPORT_SIZE; idx++) {                          \
        if (consumer_report.body.keys[idx] != match) {                                             \
            continue;                                                                              \
        }                                                                                          \
        consumer_report.body.keys[idx] = val;                                                      \
        if (val) {                                                                                 \
            break;                                                                                 \
        }                                                                                          \
    }

int zmk_hid_implicit_modifiers_press(zmk_mod_flags_t new_implicit_modifiers) {
    implicit_modifiers = new_implicit_modifiers;
    zmk_mod_flags_t current = GET_MODIFIERS;
    SET_MODIFIERS(explicit_modifiers);
    return current == GET_MODIFIERS ? 0 : 1;
}

int zmk_hid_implicit_modifiers_release() {
    implicit_modifiers = 0;
    zmk_mod_flags_t current = GET_MODIFIERS;
    SET_MODIFIERS(explicit_modifiers);
    return current == GET_MODIFIERS ? 0 : 1;
}

int zmk_hid_masked_modifiers_set(zmk_mod_flags_t new_masked_modifiers) {
    masked_modifiers = new_masked_modifiers;
    zmk_mod_flags_t current = GET_MODIFIERS;
    SET_MODIFIERS(explicit_modifiers);
    return current == GET_MODIFIERS ? 0 : 1;
}

int zmk_hid_masked_modifiers_clear() {
    masked_modifiers = 0;
    zmk_mod_flags_t current = GET_MODIFIERS;
    SET_MODIFIERS(explicit_modifiers);
    return current == GET_MODIFIERS ? 0 : 1;
}

int zmk_hid_keyboard_press(zmk_key_t code) {
    if (code >= HID_USAGE_KEY_KEYBOARD_LEFTCONTROL && code <= HID_USAGE_KEY_KEYBOARD_RIGHT_GUI) {
        return zmk_hid_register_mod(code - HID_USAGE_KEY_KEYBOARD_LEFTCONTROL);
    }
    select_keyboard_usage(code);
    return 0;
};

int zmk_hid_keyboard_release(zmk_key_t code) {
    if (code >= HID_USAGE_KEY_KEYBOARD_LEFTCONTROL && code <= HID_USAGE_KEY_KEYBOARD_RIGHT_GUI) {
        return zmk_hid_unregister_mod(code - HID_USAGE_KEY_KEYBOARD_LEFTCONTROL);
    }
    deselect_keyboard_usage(code);
    return 0;
};

bool zmk_hid_keyboard_is_pressed(zmk_key_t code) {
    if (code >= HID_USAGE_KEY_KEYBOARD_LEFTCONTROL && code <= HID_USAGE_KEY_KEYBOARD_RIGHT_GUI) {
        return zmk_hid_mod_is_pressed(code - HID_USAGE_KEY_KEYBOARD_LEFTCONTROL);
    }
    return check_keyboard_usage(code);
}

void zmk_hid_keyboard_clear() { 
    memset(&keyboard_report.body, 0, sizeof(keyboard_report.body));
#if CONFIG_ADAPATIVE_NKRO
    memset(&adapative_nkro.nkro_report.body,0,sizeof(adapative_nkro.nkro_report.body));
    // memset(&nkro_24g_rpt.body,0,sizeof(nkro_24g_rpt.body));
    if(adapative_nkro.nkro_bits_count)
    {
        atomic_set_bit(report_changed,NKRO_RPT);
        adapative_nkro.nkro_bits_count=0;
    }
    if(adapative_nkro.kb_keys_count)
    {
        atomic_set_bit(report_changed,KB_RPT);
        adapative_nkro.kb_keys_count=0;
    }
#endif     
}

int zmk_hid_consumer_press(zmk_key_t code) {
    TOGGLE_CONSUMER(0U, code);
    return 0;
};

int zmk_hid_consumer_release(zmk_key_t code) {
    TOGGLE_CONSUMER(code, 0U);
    return 0;
};

void zmk_hid_consumer_clear() { memset(&consumer_report.body, 0, sizeof(consumer_report.body)); }

bool zmk_hid_consumer_is_pressed(zmk_key_t key) {
    for (int idx = 0; idx < CONFIG_ZMK_HID_CONSUMER_REPORT_SIZE; idx++) {
        if (consumer_report.body.keys[idx] == key) {
            return true;
        }
    }
    return false;
}

int zmk_hid_press(uint32_t usage) {
    switch (ZMK_HID_USAGE_PAGE(usage)) {
    case HID_USAGE_KEY:
        return zmk_hid_keyboard_press(ZMK_HID_USAGE_ID(usage));
    case HID_USAGE_CONSUMER:
        return zmk_hid_consumer_press(ZMK_HID_USAGE_ID(usage));
    }
    return -EINVAL;
}

int zmk_hid_release(uint32_t usage) {
    switch (ZMK_HID_USAGE_PAGE(usage)) {
    case HID_USAGE_KEY:
        return zmk_hid_keyboard_release(ZMK_HID_USAGE_ID(usage));
    case HID_USAGE_CONSUMER:
        return zmk_hid_consumer_release(ZMK_HID_USAGE_ID(usage));
    }
    return -EINVAL;
}

bool zmk_hid_is_pressed(uint32_t usage) {
    switch (ZMK_HID_USAGE_PAGE(usage)) {
    case HID_USAGE_KEY:
        return zmk_hid_keyboard_is_pressed(ZMK_HID_USAGE_ID(usage));
    case HID_USAGE_CONSUMER:
        return zmk_hid_consumer_is_pressed(ZMK_HID_USAGE_ID(usage));
    }
    return false;
}

struct zmk_hid_keyboard_report *zmk_hid_get_keyboard_report() {
    return &keyboard_report;
}

struct zmk_hid_consumer_report *zmk_hid_get_consumer_report() {
    return &consumer_report;
}
