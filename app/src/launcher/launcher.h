#pragma once

#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>
#include <zephyr/device.h>
// #include <app_version.h>
#include <zmk/keymap.h>
#include "keycodes.h"

// #define VIA_FIRMWARE_VERSION 0x000102002

#define ZMK_HID_MAIN_VAL_DATA (0x00 << 0)
#define ZMK_HID_MAIN_VAL_CONST (0x01 << 0)

#define ZMK_HID_MAIN_VAL_ARRAY (0x00 << 1)
#define ZMK_HID_MAIN_VAL_VAR (0x01 << 1)

#define ZMK_HID_MAIN_VAL_ABS (0x00 << 2)
#define ZMK_HID_MAIN_VAL_REL (0x01 << 2)

#define ZMK_HID_MAIN_VAL_NO_WRAP (0x00 << 3)
#define ZMK_HID_MAIN_VAL_WRAP (0x01 << 3)

#define ZMK_HID_MAIN_VAL_LIN (0x00 << 4)
#define ZMK_HID_MAIN_VAL_NON_LIN (0x01 << 4)

#define ZMK_HID_MAIN_VAL_PREFERRED (0x00 << 5)
#define ZMK_HID_MAIN_VAL_NO_PREFERRED (0x01 << 5)

#define ZMK_HID_MAIN_VAL_NO_NULL (0x00 << 6)
#define ZMK_HID_MAIN_VAL_NULL (0x01 << 6)

#define ZMK_HID_MAIN_VAL_NON_VOL (0x00 << 7)
#define ZMK_HID_MAIN_VAL_VOL (0x01 << 7)

#define HID_USAGE_PAGE_2(a,b)        \
    HID_ITEM(HID_ITEM_TAG_USAGE_PAGE, HID_ITEM_TYPE_GLOBAL, 2), a,b 


#define RAW_USAGE_PAGE 0xFF60
#define RAW_USAGE_ID 0x61
#define RAW_EPSIZE 32

static const uint8_t zmk_hid_via_report_desc[]= {
    HID_USAGE_PAGE_2((RAW_USAGE_PAGE &0xff),(RAW_USAGE_PAGE>>8) ), // Vendor Defined
    HID_USAGE(RAW_USAGE_ID),         // Vendor Defined
    HID_COLLECTION(0x01),    // Application
        // Data to host
        HID_USAGE( 0x62),     // Vendor Defined
        HID_LOGICAL_MIN8(0x00),
        HID_LOGICAL_MAX16(0xFF,0x00),
        HID_REPORT_COUNT(RAW_EPSIZE),
        HID_REPORT_SIZE(0x08),
        HID_INPUT(ZMK_HID_MAIN_VAL_DATA | ZMK_HID_MAIN_VAL_VAR | ZMK_HID_MAIN_VAL_ABS),

        // Data from host
        HID_USAGE(0x63),     // Vendor Defined
        HID_LOGICAL_MIN8(0x00),
        HID_LOGICAL_MAX16(0xFF,0x00),
        HID_REPORT_COUNT(RAW_EPSIZE),
        HID_REPORT_SIZE(0x08),
        HID_OUTPUT(ZMK_HID_MAIN_VAL_DATA | ZMK_HID_MAIN_VAL_VAR | ZMK_HID_MAIN_VAL_ABS | ZMK_HID_MAIN_VAL_NON_VOL),

    HID_END_COLLECTION
};

enum via_command_id {
    id_get_protocol_version                 = 0x01, // always 0x01
    id_get_keyboard_value                   = 0x02,
    id_set_keyboard_value                   = 0x03,
    id_dynamic_keymap_get_keycode           = 0x04,
    id_dynamic_keymap_set_keycode           = 0x05,
    id_dynamic_keymap_reset                 = 0x06,
    id_custom_set_value                     = 0x07,
    id_custom_get_value                     = 0x08,
    id_custom_save                          = 0x09,
    id_eeprom_reset                         = 0x0A,
    id_bootloader_jump                      = 0x0B,
    id_dynamic_keymap_macro_get_count       = 0x0C,
    id_dynamic_keymap_macro_get_buffer_size = 0x0D,
    id_dynamic_keymap_macro_get_buffer      = 0x0E,
    id_dynamic_keymap_macro_set_buffer      = 0x0F,
    id_dynamic_keymap_macro_reset           = 0x10,
    id_dynamic_keymap_get_layer_count       = 0x11,
    id_dynamic_keymap_get_buffer            = 0x12,
    id_dynamic_keymap_set_buffer            = 0x13,
    id_dynamic_keymap_get_encoder           = 0x14,
    id_dynamic_keymap_set_encoder           = 0x15,

    kc_get_protocol_version                 = 0xa0,
    kc_get_firmware_version                 = 0xa1, //ascii ver
    kc_get_support_feature                  = 0xa2,
    kc_get_default_layer                    = 0xa3,
    kc_set_key_debounce                     = 0xb0,
    kc_get_key_debounce                     = 0xb1,

    id_unhandled                            = 0xFF,
};

enum via_keyboard_value_id {
    id_uptime              = 0x01,
    id_layout_options      = 0x02,
    id_switch_matrix_state = 0x03,
    id_firmware_version    = 0x04,
    id_device_indication   = 0x05,
};

enum via_channel_id {
    id_custom_channel         = 0,
    id_qmk_backlight_channel  = 1,
    id_qmk_rgblight_channel   = 2,
    id_qmk_rgb_matrix_channel = 3,
    id_qmk_audio_channel      = 4,
    id_qmk_led_matrix_channel = 5,
};

enum via_qmk_backlight_value {
    id_qmk_backlight_brightness = 1,
    id_qmk_backlight_effect     = 2,
};

enum via_qmk_rgblight_value {
    id_qmk_rgblight_brightness   = 1,
    id_qmk_rgblight_effect       = 2,
    id_qmk_rgblight_effect_speed = 3,
    id_qmk_rgblight_color        = 4,
};

enum via_qmk_rgb_matrix_value {
    id_qmk_rgb_matrix_brightness   = 1,
    id_qmk_rgb_matrix_effect       = 2,
    id_qmk_rgb_matrix_effect_speed = 3,
    id_qmk_rgb_matrix_color        = 4,
};

enum via_qmk_led_matrix_value {
    id_qmk_led_matrix_brightness   = 1,
    id_qmk_led_matrix_effect       = 2,
    id_qmk_led_matrix_effect_speed = 3,
};

enum via_qmk_audio_value {
    id_qmk_audio_enable        = 1,
    id_qmk_audio_clicky_enable = 2,
};

typedef uint32_t matrix_row_t ;

#if CONFIG_ZMK_LAUNCHER
#define VIA_ENABLE
#endif

enum{
    BT_HST1 = QK_KB_0,
    BT_HST2 ,
    BT_HST3 ,
    KC_BOOT ,
    PAIR_24G ,
    KC_SCRN_LOCK,
    UC_CMD_CMA,//Command-Comma (,): Open preferences for the front app.
    UC_LOPT,//Left Option
    UC_ROPT,//Right Option
    UC_LCMD,//Left Command
    UC_RCMD,//Right Command
    UC_CTRL_LEFT,//ctrl+ <-  ,switch desktops
    UC_CTRL_RIGHT,//ctrl+ ->  ,switch desktops
    UC_EMOJI_MAC,//Command+Control+space open emoji in mac
    UC_TASK_VIEW,//LG(TAB) Open Task View
    UC_SWITCH_DESKTOP_LEFT,//LG(LC(LEFT)) switch desktops
    UC_SWITCH_DESKTOP_RIGHT,//LG(LC(RIGHT)) switch desktops
    UC_FILE_EXPLORER,//LG(E) ,Open File Explorer.
    UC_LOCK,//LG(L),Lock your PC or switch accounts.
    UC_SETTINGS,//LG(I),Open Settings.
    UC_EMOJI_WIN,//LG(DOT),open emoji in win
    UC_PRNS_WIN,//LG(LS(S))
    UC_PRNS_MAC,//LG(LS(N4))
    UC_KEYCHRON,//0x770100
};

#if CONFIG_SHIELD_KEYCHRON_B6_UK
#define QMK_BUILDDATE "2024-03-02" 
#elif CONFIG_SHIELD_KEYCHRON_B6_US
#define QMK_BUILDDATE "2024-03-03"
#elif CONFIG_SHIELD_KEYCHRON_B6_JIS
#define QMK_BUILDDATE "2024-03-04" 
#elif CONFIG_SHIELD_KEYCHRON_B1_US
#define QMK_BUILDDATE "2024-04-05" 
#elif CONFIG_SHIELD_KEYCHRON_B1_UK
#define QMK_BUILDDATE "2024-04-06" 
#elif CONFIG_SHIELD_KEYCHRON_B1_JIS
#define QMK_BUILDDATE "2024-04-07" 
#elif CONFIG_SHIELD_KEYCHRON_B2_JIS
#define QMK_BUILDDATE "2024-05-08" 
#elif CONFIG_SHIELD_KEYCHRON_B2_US
#define QMK_BUILDDATE "2024-05-09" 
#elif CONFIG_SHIELD_KEYCHRON_B2_UK
#define QMK_BUILDDATE "2024-05-10"
#elif CONFIG_SHIELD_KEYCHRON_B2_KR
#define QMK_BUILDDATE "2024-08-21" 
#else
#define QMK_BUILDDATE "2024-05-27" 
#endif 

#define EECONFIG_SIZE 0
#define MATRIX_COLS DT_PROP_LEN(DT_NODELABEL(kscan0), col_gpios)
#define MATRIX_ROWS DT_PROP_LEN(DT_NODELABEL(kscan0), row_gpios)
#define DYNAMIC_KEYMAP_LAYER_COUNT ZMK_KEYMAP_LAYERS_LEN
#define TOTAL_EEPROM_BYTE_COUNT 2560//4000
#define KEYMAP_LEN (MATRIX_COLS*MATRIX_ROWS*2)
// Keyboard level code can change where VIA stores the magic.
// The magic is the build date YYMMDD encoded as BCD in 3 bytes,
// thus installing firmware built on a different date to the one
// already installed can be detected and the EEPROM data is reset.
// The only reason this is important is in case EEPROM usage changes
// and the EEPROM was not explicitly reset by bootmagic lite.
#ifndef VIA_EEPROM_MAGIC_ADDR
#    define VIA_EEPROM_MAGIC_ADDR (EECONFIG_SIZE)
#endif

#define VIA_EEPROM_LAYOUT_OPTIONS_ADDR (VIA_EEPROM_MAGIC_ADDR + 3)

// Changing the layout options size after release will invalidate EEPROM,
// but this is something that should be set correctly on initial implementation.
// 1 byte is enough for most uses (i.e. 8 binary states, or 6 binary + 1 ternary/quaternary )
#ifndef VIA_EEPROM_LAYOUT_OPTIONS_SIZE
#    define VIA_EEPROM_LAYOUT_OPTIONS_SIZE 1
#endif

// Allow override of the layout options default value.
// This requires advanced knowledge of how VIA stores layout options
// and is only really useful for setting a boolean layout option
// state to true by default.
#ifndef VIA_EEPROM_LAYOUT_OPTIONS_DEFAULT
#    define VIA_EEPROM_LAYOUT_OPTIONS_DEFAULT 0x00000000
#endif

// The end of the EEPROM memory used by VIA
// By default, dynamic keymaps will start at this if there is no
// custom config
#define VIA_EEPROM_CUSTOM_CONFIG_ADDR (VIA_EEPROM_LAYOUT_OPTIONS_ADDR + VIA_EEPROM_LAYOUT_OPTIONS_SIZE)

#ifndef VIA_EEPROM_CUSTOM_CONFIG_SIZE
#    define VIA_EEPROM_CUSTOM_CONFIG_SIZE 0
#endif

#define VIA_EEPROM_CONFIG_END (VIA_EEPROM_CUSTOM_CONFIG_ADDR + VIA_EEPROM_CUSTOM_CONFIG_SIZE)

#ifdef VIA_ENABLE
#    define DYNAMIC_KEYMAP_EEPROM_START (VIA_EEPROM_CONFIG_END)
#else
#    include "eeconfig.h"
#    define DYNAMIC_KEYMAP_EEPROM_START (EECONFIG_SIZE)
#endif

#ifdef ENCODER_ENABLE
#    include "encoder.h"
#else
#    define NUM_ENCODERS 0
#endif

#ifndef DYNAMIC_KEYMAP_LAYER_COUNT
#    define DYNAMIC_KEYMAP_LAYER_COUNT 4
#endif

#ifndef DYNAMIC_KEYMAP_MACRO_COUNT
#    define DYNAMIC_KEYMAP_MACRO_COUNT 16
#endif

#ifndef TOTAL_EEPROM_BYTE_COUNT
#    error Unknown total EEPROM size. Cannot derive maximum for dynamic keymaps.
#endif

#ifndef DYNAMIC_KEYMAP_EEPROM_MAX_ADDR
#    define DYNAMIC_KEYMAP_EEPROM_MAX_ADDR (TOTAL_EEPROM_BYTE_COUNT - 1)
#endif

#if DYNAMIC_KEYMAP_EEPROM_MAX_ADDR > (TOTAL_EEPROM_BYTE_COUNT - 1)
#    pragma message STR(DYNAMIC_KEYMAP_EEPROM_MAX_ADDR) " > " STR((TOTAL_EEPROM_BYTE_COUNT - 1))
#    error DYNAMIC_KEYMAP_EEPROM_MAX_ADDR is configured to use more space than what is available for the selected EEPROM driver
#endif

// Due to usage of uint16_t check for max 65535
#if DYNAMIC_KEYMAP_EEPROM_MAX_ADDR > 65535
#    pragma message STR(DYNAMIC_KEYMAP_EEPROM_MAX_ADDR) " > 65535"
#    error DYNAMIC_KEYMAP_EEPROM_MAX_ADDR must be less than 65536
#endif

// If DYNAMIC_KEYMAP_EEPROM_ADDR not explicitly defined in config.h,
#ifndef DYNAMIC_KEYMAP_EEPROM_ADDR
#    define DYNAMIC_KEYMAP_EEPROM_ADDR DYNAMIC_KEYMAP_EEPROM_START
#endif

// Dynamic encoders starts after dynamic keymaps
#ifndef DYNAMIC_KEYMAP_ENCODER_EEPROM_ADDR
#    define DYNAMIC_KEYMAP_ENCODER_EEPROM_ADDR (DYNAMIC_KEYMAP_EEPROM_ADDR + (DYNAMIC_KEYMAP_LAYER_COUNT * MATRIX_ROWS * MATRIX_COLS * 2))
#endif

// Dynamic macro starts after dynamic encoders, but only when using ENCODER_MAP
#ifdef ENCODER_MAP_ENABLE
#    ifndef DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR
#        define DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR (DYNAMIC_KEYMAP_ENCODER_EEPROM_ADDR + (DYNAMIC_KEYMAP_LAYER_COUNT * NUM_ENCODERS * 2 * 2))
#    endif // DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR
#else      // ENCODER_MAP_ENABLE
#    ifndef DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR
#        define DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR (DYNAMIC_KEYMAP_ENCODER_EEPROM_ADDR)
#    endif // DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR
#endif     // ENCODER_MAP_ENABLE

// Sanity check that dynamic keymaps fit in available EEPROM
// If there's not 100 bytes available for macros, then something is wrong.
// The keyboard should override DYNAMIC_KEYMAP_LAYER_COUNT to reduce it,
// or DYNAMIC_KEYMAP_EEPROM_MAX_ADDR to increase it, *only if* the microcontroller has
// more than the default.
_Static_assert((DYNAMIC_KEYMAP_EEPROM_MAX_ADDR) - (DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR) >= 100, "Dynamic keymaps are configured to use more EEPROM than is available.");

// Dynamic macros are stored after the keymaps and use what is available
// up to and including DYNAMIC_KEYMAP_EEPROM_MAX_ADDR.
#ifndef DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE
#    define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE (DYNAMIC_KEYMAP_EEPROM_MAX_ADDR - DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + 1)
#endif

#ifndef DYNAMIC_KEYMAP_MACRO_DELAY
#    define DYNAMIC_KEYMAP_MACRO_DELAY TAP_CODE_DELAY
#endif
//keympas[][]

#define NUM_KEYMAP_LAYERS_RAW DYNAMIC_KEYMAP_LAYER_COUNT //((uint8_t)(sizeof(keymaps) / ((MATRIX_ROWS) * (MATRIX_COLS) * sizeof(uint16_t))))
//
// This is changed only when the command IDs change,
// so VIA Configurator can detect compatible firmware.
#define VIA_PROTOCOL_VERSION 0x000C

// This is a version number for the firmware for the keyboard.
// It can be used to ensure the VIA keyboard definition and the firmware
// have the same version, especially if there are changes to custom values.
// Define this in config.h to override and bump this number.
// This is *not* required if the keyboard is only using basic functionality
// and not using custom values for lighting, rotary encoders, etc.
#ifndef VIA_FIRMWARE_VERSION
#    define VIA_FIRMWARE_VERSION 0x00000000
#endif

#ifndef TAP_CODE_DELAY
#    define TAP_CODE_DELAY 10
#endif

// Can be called in an overriding via_init_kb() to test if keyboard level code usage of
// EEPROM is invalid and use/save defaults.
bool via_eeprom_is_valid(void);

// Sets VIA/keyboard level usage of EEPROM to valid/invalid
// Keyboard level code (eg. via_init_kb()) should not call this
void via_eeprom_set_valid(bool valid);

// Called by QMK core to initialize dynamic keymaps etc.
void eeconfig_init_via(void);
void via_init(void);

// Used by VIA to store and retrieve the layout options.
uint32_t via_get_layout_options(void);
void     via_set_layout_options(uint32_t value);
void     via_set_layout_options_kb(uint32_t value);

// Used by VIA to tell a device to flash LEDs (or do something else) when that
// device becomes the active device being configured, on startup or switching
// between devices.
void via_set_device_indication(uint8_t value);

// Called by QMK core to process VIA-specific keycodes.
// bool process_record_via(uint16_t keycode, keyrecord_t *record);

// These are made external so that keyboard level custom value handlers can use them.
#if defined(BACKLIGHT_ENABLE)
void via_qmk_backlight_command(uint8_t *data, uint8_t length);
void via_qmk_backlight_set_value(uint8_t *data);
void via_qmk_backlight_get_value(uint8_t *data);
void via_qmk_backlight_save(void);
#endif

#if defined(RGBLIGHT_ENABLE)
void via_qmk_rgblight_command(uint8_t *data, uint8_t length);
void via_qmk_rgblight_set_value(uint8_t *data);
void via_qmk_rgblight_get_value(uint8_t *data);
void via_qmk_rgblight_save(void);
#endif

#if defined(RGB_MATRIX_ENABLE)
void via_qmk_rgb_matrix_command(uint8_t *data, uint8_t length);
void via_qmk_rgb_matrix_set_value(uint8_t *data);
void via_qmk_rgb_matrix_get_value(uint8_t *data);
void via_qmk_rgb_matrix_save(void);
#endif

#if defined(LED_MATRIX_ENABLE)
void via_qmk_led_matrix_command(uint8_t *data, uint8_t length);
void via_qmk_led_matrix_set_value(uint8_t *data);
void via_qmk_led_matrix_get_value(uint8_t *data);
void via_qmk_led_matrix_save(void);
#endif

#if defined(AUDIO_ENABLE)
void via_qmk_audio_command(uint8_t *data, uint8_t length);
void via_qmk_audio_set_value(uint8_t *data);
void via_qmk_audio_get_value(uint8_t *data);
void via_qmk_audio_save(void);
#endif

void raw_hid_receive(uint8_t *data, uint8_t length);

union VIA_EE_DEVICE {
    uint8_t raw[TOTAL_EEPROM_BYTE_COUNT];
    struct {
        uint8_t magic[3];
        uint8_t layout_options[VIA_EEPROM_LAYOUT_OPTIONS_SIZE];
    #if VIA_EEPROM_CUSTOM_CONFIG_SIZE    
        uint8_t custom[VIA_EEPROM_CUSTOM_CONFIG_SIZE];
    #endif
        uint8_t keymaps[(DYNAMIC_KEYMAP_LAYER_COUNT * KEYMAP_LEN)];
    #ifdef ENCODER_MAP_ENABLE
        uint8_t encoder[DYNAMIC_KEYMAP_LAYER_COUNT * NUM_ENCODERS * 2 * 2];
    #endif     
        uint8_t macros[DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE];
        uint8_t changed_keys[DYNAMIC_KEYMAP_LAYER_COUNT][20];
    } __packed;
};

typedef union VIA_EE_DEVICE _via_ee_device;
extern _via_ee_device via_ee_device;

struct user_debounce{
    uint8_t scan_period_ms;
    uint8_t debounce_press_ms;
    uint8_t debounce_release_ms;
};

void via_ee_read_all(void);
void via_ee_read_macros(void);
void via_ee_read_keymaps(void);
void via_ee_read_magic(void);
void via_ee_read_debounce(void);
void via_ee_update_debounce(void);
void via_ee_update_magic(void);
void via_ee_update_layout(void);
void via_ee_update_custom(void);
void via_ee_update_keymaps(void);
void via_ee_update_keymap(uint8_t i);
void via_ee_update_encoder(void);
void via_ee_update_macros(void);
void via_ee_delete_macros(void);
void via_ee_read_changed_keys(void);
void via_ee_update_changed_keys(uint8_t layer);
void via_set_changed_key(uint8_t layer,uint8_t row,uint8_t col);
void via_ee_reset_changed_keys(void);
void via_ee_reset_keymaps(void);
void set_zmk_keymap(uint8_t layer,uint8_t row ,uint8_t column ,uint16_t keycode) ;
void generate_via_keymaps(void);
void via_save_debounce(void);

static inline uint8_t eeprom_read_byte(uint16_t addr)
{
    return via_ee_device.raw[addr];
}
static inline void eeprom_update_byte(uint16_t addr, uint8_t value)
{
    via_ee_device.raw[addr]=value;
}