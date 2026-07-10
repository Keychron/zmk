/* Copyright 2021
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "send_string.h"

#include <ctype.h>
#include <stdlib.h>
#include "keycodes.h"

#include <zmk/behavior_queue.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>
LOG_MODULE_DECLARE(zmk,CONFIG_ZMK_LOG_LEVEL);

#define PROGMEM


// clang-format off

/* Bit-Packed look-up table to convert an ASCII character to whether
 * [Shift] needs to be sent with the keycode.
 */
__attribute__((weak)) const uint8_t ascii_to_shift_lut[16] PROGMEM = {
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),

    KCLUT_ENTRY(0, 1, 1, 1, 1, 1, 1, 0),
    KCLUT_ENTRY(1, 1, 1, 1, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 1, 0, 1, 0, 1, 1),
    KCLUT_ENTRY(1, 1, 1, 1, 1, 1, 1, 1),
    KCLUT_ENTRY(1, 1, 1, 1, 1, 1, 1, 1),
    KCLUT_ENTRY(1, 1, 1, 1, 1, 1, 1, 1),
    KCLUT_ENTRY(1, 1, 1, 0, 0, 0, 1, 1),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 1, 1, 1, 1, 0)
};

/* Bit-Packed look-up table to convert an ASCII character to whether
 * [AltGr] needs to be sent with the keycode.
 */
__attribute__((weak)) const uint8_t ascii_to_altgr_lut[16] PROGMEM = {
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),

    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0)
};

/* Bit-Packed look-up table to convert an ASCII character to whether
 * [Space] needs to be sent after the keycode
 */
__attribute__((weak)) const uint8_t ascii_to_dead_lut[16] PROGMEM = {
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),

    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0),
    KCLUT_ENTRY(0, 0, 0, 0, 0, 0, 0, 0)
};

/* Look-up table to convert an ASCII character to a keycode.
 */
__attribute__((weak)) const uint8_t ascii_to_keycode_lut[128] PROGMEM = {
    // NUL   SOH      STX      ETX      EOT      ENQ      ACK      BEL
    XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,
    // BS    TAB      LF       VT       FF       CR       SO       SI
    KC_BSPC, KC_TAB,  KC_ENT,  XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,
    // DLE   DC1      DC2      DC3      DC4      NAK      SYN      ETB
    XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,
    // CAN   EM       SUB      ESC      FS       GS       RS       US
    XXXXXXX, XXXXXXX, XXXXXXX, KC_ESC,  XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,

    //       !        "        #        $        %        &        '
    KC_SPC,  KC_1,    KC_QUOT, KC_3,    KC_4,    KC_5,    KC_7,    KC_QUOT,
    // (     )        *        +        ,        -        .        /
    KC_9,    KC_0,    KC_8,    KC_EQL,  KC_COMM, KC_MINS, KC_DOT,  KC_SLSH,
    // 0     1        2        3        4        5        6        7
    KC_0,    KC_1,    KC_2,    KC_3,    KC_4,    KC_5,    KC_6,    KC_7,
    // 8     9        :        ;        <        =        >        ?
    KC_8,    KC_9,    KC_SCLN, KC_SCLN, KC_COMM, KC_EQL,  KC_DOT,  KC_SLSH,
    // @     A        B        C        D        E        F        G
    KC_2,    KC_A,    KC_B,    KC_C,    KC_D,    KC_E,    KC_F,    KC_G,
    // H     I        J        K        L        M        N        O
    KC_H,    KC_I,    KC_J,    KC_K,    KC_L,    KC_M,    KC_N,    KC_O,
    // P     Q        R        S        T        U        V        W
    KC_P,    KC_Q,    KC_R,    KC_S,    KC_T,    KC_U,    KC_V,    KC_W,
    // X     Y        Z        [        \        ]        ^        _
    KC_X,    KC_Y,    KC_Z,    KC_LBRC, KC_BSLS, KC_RBRC, KC_6,    KC_MINS,
    // `     a        b        c        d        e        f        g
    KC_GRV,  KC_A,    KC_B,    KC_C,    KC_D,    KC_E,    KC_F,    KC_G,
    // h     i        j        k        l        m        n        o
    KC_H,    KC_I,    KC_J,    KC_K,    KC_L,    KC_M,    KC_N,    KC_O,
    // p     q        r        s        t        u        v        w
    KC_P,    KC_Q,    KC_R,    KC_S,    KC_T,    KC_U,    KC_V,    KC_W,
    // x     y        z        {        |        }        ~        DEL
    KC_X,    KC_Y,    KC_Z,    KC_LBRC, KC_BSLS, KC_RBRC, KC_GRV,  KC_DEL
};

// clang-format on

// Note: we bit-pack in "reverse" order to optimize loading
#define PGM_LOADBIT(mem, pos) ((mem[(pos) / 8] >> ((pos) % 8)) & 0x01)

#ifndef TAP_CODE_DELAY
#    define TAP_CODE_DELAY 10
#endif
#ifndef TAP_HOLD_CAPS_DELAY
#    define TAP_HOLD_CAPS_DELAY 80
#endif
void via_keycode_to_binding(uint8_t keycode,struct zmk_behavior_binding * binding);
int zmk_macro_queue_add(uint8_t code,uint8_t type ,uint16_t delay);


struct q_item {
    uint8_t code;
    uint8_t type;
    uint16_t delay;
};
K_MSGQ_DEFINE(zmk_macro_queue_msgq, sizeof(struct q_item), 256, 4);
void send_string_with_delay(const char *string, uint8_t interval) {

    while (1) {
        char ascii_code = *string;
        if (!ascii_code) break;
        if (ascii_code == SS_QMK_PREFIX) {
            ascii_code = *(++string);
            if (ascii_code == SS_TAP_CODE) {
                // tap
                uint8_t keycode = *(++string);
                zmk_macro_queue_add(keycode,SS_TAP_CODE,0);
                
            } else if (ascii_code == SS_DOWN_CODE) {
                // down
                uint8_t keycode = *(++string);
                // register_code(keycode);
                zmk_macro_queue_add(keycode,SS_DOWN_CODE,0);
                LOG_DBG("press,code:%x",keycode);

            } else if (ascii_code == SS_UP_CODE) {
                // up
                uint8_t keycode = *(++string);
                // unregister_code(keycode);
                zmk_macro_queue_add(keycode,SS_UP_CODE,0);
                LOG_DBG("release,code:%x",keycode);

            } else if (ascii_code == SS_DELAY_CODE) {
                // delay
                int     ms      = 0;
                uint8_t keycode = *(++string);
                while (isdigit(keycode)) {
                    ms *= 10;
                    ms += keycode - '0';
                    keycode = *(++string);
                }
                LOG_DBG("delay:%d",ms);
                zmk_macro_queue_add(0,SS_DELAY_CODE,ms);
            }
        } else {
            send_char(ascii_code);
           
        }
        ++string;

    }
}
void send_string_end(void)
{
    struct q_item item;
    
    uint16_t num = k_msgq_num_used_get(&zmk_macro_queue_msgq);
    LOG_DBG("queue num:%d",num);
    const int ret = k_msgq_get(&zmk_macro_queue_msgq, &item, K_NO_WAIT);
    if (ret < 0) {
        return ;
    }
    LOG_DBG("item type:%d,code:%x,delay:%d",item.type,item.code,item.delay);
    struct zmk_behavior_binding keycode_binding;
    via_keycode_to_binding(item.code,&keycode_binding);

    switch(item.type)
    {

        case SS_TAP_CODE:
        {
            uint16_t delay = (item.code == KC_CAPS_LOCK) ? TAP_HOLD_CAPS_DELAY : TAP_CODE_DELAY;
            
            zmk_behavior_queue_add(0,keycode_binding,true,delay);
            zmk_behavior_queue_add(0,keycode_binding,false,TAP_CODE_DELAY);
        }
            break;
        case SS_DOWN_CODE:                
                zmk_behavior_queue_add(0,keycode_binding,true,TAP_CODE_DELAY);
            break;

        case SS_UP_CODE:
            zmk_behavior_queue_add(0,keycode_binding,false,TAP_CODE_DELAY);
            break;
    }
}
void send_char(char ascii_code) {


    uint8_t keycode    = ascii_to_keycode_lut[(uint8_t)ascii_code];
    bool    is_shifted = PGM_LOADBIT(ascii_to_shift_lut, (uint8_t)ascii_code);
    bool    is_altgred = PGM_LOADBIT(ascii_to_altgr_lut, (uint8_t)ascii_code);
    bool    is_dead    = PGM_LOADBIT(ascii_to_dead_lut, (uint8_t)ascii_code);

    if (is_shifted) {
        // register_code(KC_LEFT_SHIFT);
        zmk_macro_queue_add(KC_LEFT_SHIFT,SS_DOWN_CODE,0);
    }
    if (is_altgred) {
        // register_code(KC_RIGHT_ALT);

        zmk_macro_queue_add(KC_RIGHT_ALT,SS_DOWN_CODE,0);
    }
    // tap_code(keycode);
     zmk_macro_queue_add(keycode,SS_TAP_CODE,0);


    if (is_altgred) {
        // unregister_code(KC_RIGHT_ALT);
        zmk_macro_queue_add(KC_RIGHT_ALT,SS_UP_CODE,0);
    }
    if (is_shifted) {
        // unregister_code(KC_LEFT_SHIFT);
        zmk_macro_queue_add(KC_LEFT_SHIFT,SS_UP_CODE,0);
    }
    if (is_dead) {
        // tap_code(KC_SPACE);
        zmk_macro_queue_add(KC_SPACE,SS_TAP_CODE,0);

    }
}



int zmk_macro_queue_add(uint8_t code,uint8_t type ,uint16_t delay) {

    struct q_item item = {.code = code, .type = type, .delay = delay};

    int ret = k_msgq_put(&zmk_macro_queue_msgq, &item, K_NO_WAIT);
    if (ret < 0) {
        LOG_ERR("macro queue full");
        return ret;
    }
    LOG_DBG("macro queue num:%d",k_msgq_num_used_get(&zmk_macro_queue_msgq));
    if(k_msgq_num_used_get(&zmk_macro_queue_msgq)>=2)
    {
        struct q_item item1={.delay =0};
        struct q_item item2={.delay =0};
        ret =k_msgq_get(&zmk_macro_queue_msgq,&item1,K_NO_WAIT);
        if(ret <0)
        {
            return ret;
        }
        LOG_DBG("item1 type:%d,char:%c,delay:%d",item1.type,item1.code,item1.delay);
        ret =k_msgq_peek(&zmk_macro_queue_msgq,&item2);
        if(ret <0 )
        {
            return ret;
        }
        LOG_DBG("item2 type:%d,char:%c,delay:%d",item2.type,item2.code,item2.delay);
        if(item2.type ==SS_DELAY_CODE)
        {
            k_msgq_get(&zmk_macro_queue_msgq,&item2,K_NO_WAIT);
        }
        
        struct zmk_behavior_binding keycode_binding;
        via_keycode_to_binding(item1.code,&keycode_binding);
        switch(item1.type)
        {
            case SS_TAP_CODE:
            {
                uint16_t delay = (item1.code == KC_CAPS_LOCK) ? TAP_HOLD_CAPS_DELAY : TAP_CODE_DELAY;
                
                zmk_behavior_queue_add(0,keycode_binding,true,delay);
                zmk_behavior_queue_add(0,keycode_binding,false,item2.delay ? item2.delay: TAP_CODE_DELAY);

            }
                break;

            case SS_DOWN_CODE:                
                zmk_behavior_queue_add(0,keycode_binding,true,item2.delay ? item2.delay: TAP_CODE_DELAY);

                break;
            case SS_UP_CODE:
                zmk_behavior_queue_add(0,keycode_binding,false,item2.delay ? item2.delay: TAP_CODE_DELAY);
                break;
            
        }

        
    }
    return 0;
}


