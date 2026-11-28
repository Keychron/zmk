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

#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)

static zmk_hid_boot_report_t boot_report = {.modifiers = 0, ._reserved = 0, .keys = {0}};
static uint8_t keys_held = 0;

#endif /* IS_ENABLED(CONFIG_ZMK_USB_BOOT) */

#if CONFIG_ADAPATIVE_NKRO
struct zmk_adapative_nkro adapative_nkro;
static ATOMIC_DEFINE(report_changed, 2);
bool nkro_changed(void) { return atomic_test_and_clear_bit(report_changed, NKRO_RPT); }
bool kb_changed(void) { return atomic_test_and_clear_bit(report_changed, KB_RPT); }
#endif

#if IS_ENABLED(CONFIG_ZMK_MOUSE)

static struct zmk_hid_mouse_report mouse_report = {.report_id = ZMK_HID_REPORT_ID_MOUSE,
                                                   .body = {.buttons = 0}};

#endif // IS_ENABLED(CONFIG_ZMK_MOUSE)

extern uint8_t fn_win_lock;

// Keep track of how often a modifier was pressed.
// Only release the modifier if the count is 0.
static int explicit_modifier_counts[8] = {0, 0, 0, 0, 0, 0, 0, 0};
static zmk_mod_flags_t explicit_modifiers = 0;
static zmk_mod_flags_t implicit_modifiers = 0;
static zmk_mod_flags_t masked_modifiers = 0;
static bool zmk_nkro_enable;

#define SET_MODIFIERS(mods)                                                                        \
    {                                                                                              \
        keyboard_report.body.modifiers = (mods & ~masked_modifiers) | implicit_modifiers;          \
        LOG_DBG("Modifiers set to 0x%02X", keyboard_report.body.modifiers);                        \
    }

#define GET_MODIFIERS (keyboard_report.body.modifiers)

zmk_mod_flags_t zmk_hid_get_explicit_mods(void) { return explicit_modifiers; }

int zmk_hid_register_mod(zmk_mod_t modifier) {
#ifdef CONFIG_ENABLE_WIN_LOCK
    if (fn_win_lock && (modifier == 7 || modifier == 3)) {
        explicit_modifier_counts[modifier] = 0;
        LOG_ERR("not send modifier:%d", modifier);
        return 0;
    }
#endif
    explicit_modifier_counts[modifier]++;
    LOG_DBG("Modifier %d count %d", modifier, explicit_modifier_counts[modifier]);
    WRITE_BIT(explicit_modifiers, modifier, true);
    zmk_mod_flags_t current = GET_MODIFIERS;
    SET_MODIFIERS(explicit_modifiers);
#if CONFIG_ADAPATIVE_NKRO
    int ret = current == GET_MODIFIERS ? 0 : 1;
    if (ret)
        atomic_set_bit(report_changed, KB_RPT);
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
#if CONFIG_ZMK_LAUNCHER
    extern uint8_t macro_running;
    if (macro_running) {
        explicit_modifier_counts[modifier] = 0;
        LOG_WRN("clear modifier:%d", modifier);
    }
#endif
    LOG_DBG("Modifier %d count: %d", modifier, explicit_modifier_counts[modifier]);
    if (explicit_modifier_counts[modifier] == 0) {
        LOG_DBG("Modifier %d released", modifier);
        WRITE_BIT(explicit_modifiers, modifier, false);
    }
    zmk_mod_flags_t current = GET_MODIFIERS;
    SET_MODIFIERS(explicit_modifiers);
#if CONFIG_ADAPATIVE_NKRO
    int ret = current == GET_MODIFIERS ? 0 : 1;
    if (ret)
        atomic_set_bit(report_changed, KB_RPT);
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

#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)

static zmk_hid_boot_report_t *boot_report_rollover(uint8_t modifiers) {
    boot_report.modifiers = modifiers;
    for (int i = 0; i < HID_BOOT_KEY_LEN; i++) {
        boot_report.keys[i] = HID_ERROR_ROLLOVER;
    }
    return &boot_report;
}

#endif /* IS_ENABLED(CONFIG_ZMK_USB_BOOT) */
void launcher_update_nkro(uint8_t nkro);
bool all_key_up(void);
static uint8_t try_toggle_count;
static uint8_t toggle_nkro_up;
void toggle_nkro_cb(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(toggle_nkro, toggle_nkro_cb);
void toggle_nkro_cb(struct k_work *work) {
    if (!all_key_up()) {
        LOG_ERR("try toggle count:%d", try_toggle_count);
        if (try_toggle_count++ < 200) {
            k_work_reschedule(&toggle_nkro, K_MSEC(20));
        } else if (!toggle_nkro_up) {
            k_work_reschedule(&toggle_nkro, K_MSEC(20));
        }
        return;
    }
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(HID_USAGE_KEY);

    zmk_nkro_enable = zmk_nkro_enable ? 0 : 1;
    keyboard_report.report_id =
        zmk_nkro_enable ? ZMK_HID_REPORT_ID_KEYBOARD_NKRO : ZMK_HID_REPORT_ID_KEYBOARD;
    keyboard_report.body._reserved = zmk_nkro_enable;
    LOG_ERR("nkro:%d", zmk_nkro_enable);
    launcher_update_nkro(zmk_nkro_enable);
}
void zmk_toggle_nkro_up(void) {
    toggle_nkro_up = 1;
    try_toggle_count = 0;
}
void zmk_toggle_nkro(void) {
#if !(CONFIG_ADAPATIVE_NKRO)
    toggle_nkro_up = 0;
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(HID_USAGE_KEY);
    // delay some time to change nkro!
    try_toggle_count = 0;
    k_work_reschedule(&toggle_nkro, K_MSEC(100));
#if CONFIG_ENABLE_NKRO_LED_FLASH
    void led_recover(uint8_t stop_rgb);
    led_recover(0);
#endif
#endif
}
bool zmk_get_nkro_status(void) { return zmk_nkro_enable; }
void zmk_set_nkro_status(bool enable) {
    zmk_nkro_enable = enable;
    keyboard_report.report_id =
        zmk_nkro_enable ? ZMK_HID_REPORT_ID_KEYBOARD_NKRO : ZMK_HID_REPORT_ID_KEYBOARD;
    keyboard_report.body._reserved = zmk_nkro_enable;
}
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
zmk_hid_boot_report_t *zmk_hid_get_boot_report(void) {
    if (keys_held > HID_BOOT_KEY_LEN) {
        return boot_report_rollover(keyboard_report.body.modifiers);
    }
    boot_report.modifiers = keyboard_report.body.modifiers;
    /* With adaptive NKRO keyboard_report always holds the 6-key array, NKRO or not. */
    if (IS_ENABLED(CONFIG_ADAPATIVE_NKRO) || !zmk_nkro_enable) {
#if CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE != HID_BOOT_KEY_LEN
        // Form a boot report from a report of different size.
        int out = 0;
        for (int i = 0; i < CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE; i++) {
            uint8_t key = keyboard_report.body.keys[i];
            if (key) {
                boot_report.keys[out++] = key;
                if (out == keys_held) {
                    break;
                }
            }
        }

        while (out < HID_BOOT_KEY_LEN) {
            boot_report.keys[out++] = 0;
        }

        return &boot_report;
#else
        return &keyboard_report.body;
#endif
    } else {
        memset(&boot_report.keys, 0, HID_BOOT_KEY_LEN);
        int ix = 0;
        uint8_t base_code = 0;
        for (int i = 0; i < (ZMK_HID_KEYBOARD_NKRO_MAX_USAGE + 1) / 8; ++i) {
            if (ix == keys_held) {
                break;
            }
            if (!keyboard_report.body.keys[i]) {
                continue;
            }
            base_code = i * 8;
            for (int j = 0; j < 8; ++j) {
                if (keyboard_report.body.keys[i] & BIT(j)) {
                    boot_report.keys[ix++] = base_code + j;
                }
            }
        }
        return &boot_report;
    }
}
#endif
#if CONFIG_ADAPATIVE_NKRO
static inline int TOGGLE_KEYBOARD_nkro(uint16_t code, uint16_t val, uint8_t nkro) {

    if (nkro) {
        if (val) {
            adapative_nkro.nkro_bits_count++;
            atomic_set_bit(report_changed, NKRO_RPT);
            adapative_nkro.nkro_report.body._reserved = 1;
            WRITE_BIT(adapative_nkro.nkro_report.body.keys[code / 8], code % 8, val);
        } else {
            if (adapative_nkro.nkro_report.body.keys[code / 8] & BIT(code % 8)) {
                WRITE_BIT(adapative_nkro.nkro_report.body.keys[code / 8], code % 8, val);
                adapative_nkro.nkro_bits_count--;
                atomic_set_bit(report_changed, NKRO_RPT);
                adapative_nkro.nkro_report.body._reserved = 1;
                return 1;
            }
        }

    } else {
        for (int idx = 0; idx < CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE; idx++) {
            if (keyboard_report.body.keys[idx] != code) {
                continue;
            }
            keyboard_report.body.keys[idx] = val;

            if (val) {
                adapative_nkro.kb_keys_count++;
                break;
            } else {
                adapative_nkro.kb_keys_count--;
            }
        }
    }
    return 0;
}
#endif
static inline int TOGGLE_KEYBOARD(uint16_t code, uint16_t val) {
    if (zmk_nkro_enable)
        WRITE_BIT(keyboard_report.body.keys[code / 8], code % 8, val);
    else {
        for (int idx = 0; idx < CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE; idx++) {
            if (keyboard_report.body.keys[idx] != code) {
                continue;
            }
            keyboard_report.body.keys[idx] = val;
            if (val) {
                break;
            }
        }
    }
    return 0;
}
static inline int TOGGLE_CONSUMER(uint16_t match, uint16_t val) {
#if IS_ENABLED(CONFIG_ZMK_HID_CONSUMER_REPORT_USAGES_BASIC)
    if (val > 0xff)
        return -ENOTSUP;
#endif
    for (int idx = 0; idx < CONFIG_ZMK_HID_CONSUMER_REPORT_SIZE; idx++) {
        if (consumer_report.body.keys[idx] != match) {
            continue;
        }
        consumer_report.body.keys[idx] = val;
        if (val) {
            break;
        }
    }
    return 0;
}
static inline int select_keyboard_usage(zmk_key_t usage) {
#if CONFIG_ADAPATIVE_NKRO
    if (adapative_nkro.kb_keys_count == CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE) {
        if (usage > ZMK_HID_KEYBOARD_NKRO_MAX_USAGE) {
            return -EINVAL;
        }
        TOGGLE_KEYBOARD_nkro(usage, 1, 1);
    } else {
        TOGGLE_KEYBOARD_nkro(0U, usage, 0);

        atomic_set_bit(report_changed, KB_RPT);
    }
#else
    if (zmk_nkro_enable) {

        if (usage > ZMK_HID_KEYBOARD_NKRO_MAX_USAGE) {
            return -EINVAL;
        }
        TOGGLE_KEYBOARD(usage, 1);
    } else {
        TOGGLE_KEYBOARD(0U, usage);
    }
#endif
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    ++keys_held;
#endif
    return 0;
}

static inline int deselect_keyboard_usage(zmk_key_t usage) {
#if CONFIG_ADAPATIVE_NKRO
    if (adapative_nkro.nkro_bits_count) {
        if (usage > ZMK_HID_KEYBOARD_NKRO_MAX_USAGE) {
            return -EINVAL;
        }
        if (TOGGLE_KEYBOARD_nkro(usage, 0, 1))
            return 0;
    }
    TOGGLE_KEYBOARD_nkro(usage, 0U, 0);

    atomic_set_bit(report_changed, KB_RPT);
    LOG_ERR("kb keys:%d,%d", adapative_nkro.kb_keys_count, adapative_nkro.nkro_bits_count);
#else
    if (zmk_nkro_enable) {

        if (usage > ZMK_HID_KEYBOARD_NKRO_MAX_USAGE) {
            return -EINVAL;
        }
        TOGGLE_KEYBOARD(usage, 0);
    } else {
        TOGGLE_KEYBOARD(usage, 0U);
    }
#endif
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    --keys_held;
#endif
    return 0;
}

static inline bool check_keyboard_usage(zmk_key_t usage) {
#if CONFIG_ADAPATIVE_NKRO
    if (adapative_nkro.nkro_bits_count) {
        if (usage > ZMK_HID_KEYBOARD_NKRO_MAX_USAGE) {
            return false;
        }
        return adapative_nkro.nkro_report.body.keys[usage / 8] & (1 << (usage % 8));
    } else {
        for (int idx = 0; idx < CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE; idx++) {
            if (keyboard_report.body.keys[idx] == usage) {
                return true;
            }
        }
        return false;
    }
#else
    if (zmk_nkro_enable) {
        if (usage > ZMK_HID_KEYBOARD_NKRO_MAX_USAGE) {
            return false;
        }
        return keyboard_report.body.keys[usage / 8] & (1 << (usage % 8));
    } else {
        for (int idx = 0; idx < CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE; idx++) {
            if (keyboard_report.body.keys[idx] == usage) {
                return true;
            }
        }
        return false;
    }
#endif
}
int zmk_hid_implicit_modifiers_press(zmk_mod_flags_t new_implicit_modifiers) {
    implicit_modifiers = new_implicit_modifiers;
    zmk_mod_flags_t current = GET_MODIFIERS;
    SET_MODIFIERS(explicit_modifiers);
    return current == GET_MODIFIERS ? 0 : 1;
}

int zmk_hid_implicit_modifiers_release(void) {
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

int zmk_hid_masked_modifiers_clear(void) {
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

void zmk_hid_keyboard_clear(void) {
    memset(&keyboard_report.body, 0, sizeof(keyboard_report.body));
    keyboard_report.body._reserved = zmk_nkro_enable ? 1 : 0;
#if CONFIG_ADAPATIVE_NKRO
    memset(&adapative_nkro.nkro_report.body, 0, sizeof(adapative_nkro.nkro_report.body));

    if (adapative_nkro.nkro_bits_count) {
        atomic_set_bit(report_changed, NKRO_RPT);
        adapative_nkro.nkro_bits_count = 0;
    }
    if (adapative_nkro.kb_keys_count) {
        atomic_set_bit(report_changed, KB_RPT);
        adapative_nkro.kb_keys_count = 0;
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

void zmk_hid_consumer_clear(void) {
    memset(&consumer_report.body, 0, sizeof(consumer_report.body));
}

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

#if IS_ENABLED(CONFIG_ZMK_MOUSE)

// Keep track of how often a button was pressed.
// Only release the button if the count is 0.
static int explicit_button_counts[5] = {0, 0, 0, 0, 0};
static zmk_mod_flags_t explicit_buttons = 0;

#define SET_MOUSE_BUTTONS(btns)                                                                    \
    {                                                                                              \
        mouse_report.body.buttons = btns;                                                          \
        LOG_DBG("Mouse buttons set to 0x%02X", mouse_report.body.buttons);                         \
    }

int zmk_hid_mouse_button_press(zmk_mouse_button_t button) {
    if (button >= ZMK_HID_MOUSE_NUM_BUTTONS) {
        return -EINVAL;
    }

    explicit_button_counts[button]++;
    LOG_DBG("Button %d count %d", button, explicit_button_counts[button]);
    WRITE_BIT(explicit_buttons, button, true);
    SET_MOUSE_BUTTONS(explicit_buttons);
    return 0;
}

int zmk_hid_mouse_button_release(zmk_mouse_button_t button) {
    if (button >= ZMK_HID_MOUSE_NUM_BUTTONS) {
        return -EINVAL;
    }

    if (explicit_button_counts[button] <= 0) {
        LOG_ERR("Tried to release button %d too often", button);
        return -EINVAL;
    }
    explicit_button_counts[button]--;
    LOG_DBG("Button %d count: %d", button, explicit_button_counts[button]);
    if (explicit_button_counts[button] == 0) {
        LOG_DBG("Button %d released", button);
        WRITE_BIT(explicit_buttons, button, false);
    }
    SET_MOUSE_BUTTONS(explicit_buttons);
    return 0;
}

int zmk_hid_mouse_buttons_press(zmk_mouse_button_flags_t buttons) {
    for (zmk_mouse_button_t i = 0; i < ZMK_HID_MOUSE_NUM_BUTTONS; i++) {
        if (buttons & BIT(i)) {
            zmk_hid_mouse_button_press(i);
        }
    }
    return 0;
}

int zmk_hid_mouse_buttons_release(zmk_mouse_button_flags_t buttons) {
    for (zmk_mouse_button_t i = 0; i < ZMK_HID_MOUSE_NUM_BUTTONS; i++) {
        if (buttons & BIT(i)) {
            zmk_hid_mouse_button_release(i);
        }
    }
    return 0;
}
void zmk_hid_mouse_clear(void) { memset(&mouse_report.body, 0, sizeof(mouse_report.body)); }

#endif // IS_ENABLED(CONFIG_ZMK_MOUSE)

struct zmk_hid_keyboard_report *zmk_hid_get_keyboard_report(void) {
    return &keyboard_report;
}

struct zmk_hid_consumer_report *zmk_hid_get_consumer_report(void) {
    return &consumer_report;
}

#if IS_ENABLED(CONFIG_ZMK_MOUSE)

struct zmk_hid_mouse_report *zmk_hid_get_mouse_report(void) {
    return &mouse_report;
}

void zmk_hid_set_mouse_report(uint8_t *payload) {
    memcpy(&mouse_report.body, payload, sizeof(mouse_report.body));
}

#endif // IS_ENABLED(CONFIG_ZMK_MOUSE)
