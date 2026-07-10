
#include "launcher.h"
#include "keycodes.h"
#include <zmk/behavior.h>
#include <zmk/keymap.h>
#include <zmk/matrix_transform.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <dt-bindings/zmk/keys.h>
#include "dynamic_keymap.h"


LOG_MODULE_DECLARE(zmk,4);// CONFIG_ZMK_LOG_LEVEL);

uint16_t gen_via_keymaps[DYNAMIC_KEYMAP_LAYER_COUNT][MATRIX_ROWS][MATRIX_COLS];
void update_zmk_keymap(uint8_t layer,uint8_t pos,struct zmk_behavior_binding* binding);
struct zmk_behavior_binding * get_zmk_keymap(uint8_t layer ,uint8_t pos);

static inline uint16_t KEYCODE2SYSTEM_R(uint16_t key) {
    switch (key) {
        case SYSTEM_POWER_DOWN:
            return KC_SYSTEM_POWER;
        case SYSTEM_SLEEP_0:
            return KC_SYSTEM_SLEEP;
        case SYSTEM_WAKE_UP_0:
            return KC_SYSTEM_WAKE;
        default:
            return 0;
    }
}
static inline uint16_t KEYCODE2CONSUMER_R(uint16_t key) {
    switch (key) {
        case AUDIO_MUTE:
            return KC_AUDIO_MUTE;
        case AUDIO_VOL_UP:
            return KC_AUDIO_VOL_UP;
        case AUDIO_VOL_DOWN:
            return KC_AUDIO_VOL_DOWN;
        case TRANSPORT_NEXT_TRACK:
            return KC_MEDIA_NEXT_TRACK;
        case TRANSPORT_PREV_TRACK:
            return  KC_MEDIA_PREV_TRACK;
        case TRANSPORT_FAST_FORWARD:
            return  KC_MEDIA_FAST_FORWARD;
        case TRANSPORT_REWIND:
            return  KC_MEDIA_REWIND;
        case TRANSPORT_STOP:
            return  KC_MEDIA_STOP;
        case TRANSPORT_STOP_EJECT:
            return  KC_MEDIA_EJECT;
        case TRANSPORT_PLAY_PAUSE:
            return  KC_MEDIA_PLAY_PAUSE;
        case AL_CC_CONFIG:
            return  KC_MEDIA_SELECT;
        case AL_EMAIL:
            return KC_MAIL;
        case AL_CALCULATOR:
            return  KC_CALCULATOR;
        case AL_LOCAL_BROWSER:
            return  KC_MY_COMPUTER;
        case AL_CONTROL_PANEL:
            return  KC_CONTROL_PANEL;
        case AL_ASSISTANT:
            return  KC_ASSISTANT;
        case AC_SEARCH:
            return  KC_WWW_SEARCH;
        case AC_HOME:
            return  KC_WWW_HOME;
        case AC_BACK:
            return  KC_WWW_BACK;
        case AC_FORWARD:
            return  KC_WWW_FORWARD;
        case AC_STOP:
            return  KC_WWW_STOP;
        case AC_REFRESH:
            return  KC_WWW_REFRESH;
        case BRIGHTNESS_UP:
            return  KC_BRIGHTNESS_UP;
        case BRIGHTNESS_DOWN:
            return  KC_BRIGHTNESS_DOWN;
        case AC_BOOKMARKS:
            return  KC_WWW_FAVORITES;
        case AC_DESKTOP_SHOW_ALL_WINDOWS:
            return  KC_MISSION_CONTROL;
        case AC_SOFT_KEY_LEFT:
            return  KC_LAUNCHPAD;
        default:
            return 0;
    }
}
/* keycode to system usage */
static inline uint16_t KEYCODE2SYSTEM(uint8_t key) {
    switch (key) {
        case KC_SYSTEM_POWER:
            return SYSTEM_POWER_DOWN;
        case KC_SYSTEM_SLEEP:
            return SYSTEM_SLEEP_0;
        case KC_SYSTEM_WAKE:
            return SYSTEM_WAKE_UP_0;
        default:
            return 0;
    }
}

/* keycode to consumer usage */
static inline uint16_t KEYCODE2CONSUMER(uint8_t key) {
    switch (key) {
        case KC_AUDIO_MUTE:
            return AUDIO_MUTE;
        case KC_AUDIO_VOL_UP:
            return AUDIO_VOL_UP;
        case KC_AUDIO_VOL_DOWN:
            return AUDIO_VOL_DOWN;
        case KC_MEDIA_NEXT_TRACK:
            return TRANSPORT_NEXT_TRACK;
        case KC_MEDIA_PREV_TRACK:
            return TRANSPORT_PREV_TRACK;
        case KC_MEDIA_FAST_FORWARD:
            return TRANSPORT_FAST_FORWARD;
        case KC_MEDIA_REWIND:
            return TRANSPORT_REWIND;
        case KC_MEDIA_STOP:
            return TRANSPORT_STOP;
        case KC_MEDIA_EJECT:
            return TRANSPORT_STOP_EJECT;
        case KC_MEDIA_PLAY_PAUSE:
            return TRANSPORT_PLAY_PAUSE;
        case KC_MEDIA_SELECT:
            return AL_CC_CONFIG;
        case KC_MAIL:
            return AL_EMAIL;
        case KC_CALCULATOR:
            return AL_CALCULATOR;
        case KC_MY_COMPUTER:
            return AL_LOCAL_BROWSER;
        case KC_CONTROL_PANEL:
            return AL_CONTROL_PANEL;
        case KC_ASSISTANT:
            return AL_ASSISTANT;
        case KC_WWW_SEARCH:
            return AC_SEARCH;
        case KC_WWW_HOME:
            return AC_HOME;
        case KC_WWW_BACK:
            return AC_BACK;
        case KC_WWW_FORWARD:
            return AC_FORWARD;
        case KC_WWW_STOP:
            return AC_STOP;
        case KC_WWW_REFRESH:
            return AC_REFRESH;
        case KC_BRIGHTNESS_UP:
            return BRIGHTNESS_UP;
        case KC_BRIGHTNESS_DOWN:
            return BRIGHTNESS_DOWN;
        case KC_WWW_FAVORITES:
            return AC_BOOKMARKS;
        case KC_MISSION_CONTROL:
            return AC_DESKTOP_SHOW_ALL_WINDOWS;
        case KC_LAUNCHPAD:
            return AC_SOFT_KEY_LEFT;
        default:
            return 0;
    }
}
uint8_t mod_to_zmk(uint8_t mod)
{
    if(mod &0x10)
    {
        mod  &= 0x0f;
        mod <<= 4;
        return mod;
    }
    else
    {
        return mod ;
    }
}
uint8_t mod_to_zmk_keycode(uint8_t mod_5bits)
{
    uint8_t mod=mod_to_zmk(mod_5bits);
    for(int i=0;i<8;i++)
    {
        if(mod &(1<<i))
        {
            return LCTRL +i;
        }
    }
    return 0;
}


uint8_t mod_to_via(uint8_t mod)
{
    uint8_t left_mode = mod &0x0f;
    uint8_t right_mode = mod>>4;
    if(left_mode) 
    {
        mod = left_mode;
    }
    else if(right_mode)
    {
        mod =0x10 | right_mode;
    }
    return mod;
}
uint8_t mod_kc_to_via(uint8_t kc)
{
    uint8_t mod =1<< (kc-LCTRL);
    return mod_to_via(mod);
}
#define QK_MODS_GET_MODS(kc) (((kc) >> 8) & 0x1F)
#define QK_MODS_GET_BASIC_KEYCODE(kc) ((kc)&0xFF)

void via_keycode_to_binding(uint8_t keycode,struct zmk_behavior_binding * binding)
{
        switch (keycode) {
        case BASIC_KEYCODE_RANGE:
        case MODIFIER_KEYCODE_RANGE:        
            binding->behavior_dev="KEY_PRESS";//"&kp";    
            binding->param1 =keycode;
            break;

        case SYSTEM_KEYCODE_RANGE:
            binding->behavior_dev="KEY_PRESS";//"&kp";
            binding->param1 =KEYCODE2SYSTEM(keycode)|(HID_USAGE_GD<<16);
            break;
        case CONSUMER_KEYCODE_RANGE:
            binding->behavior_dev="KEY_PRESS";//"&kp";
            binding->param1 =KEYCODE2CONSUMER(keycode)|(HID_USAGE_CONSUMER<<16);
            break;
        }
}

void set_zmk_keymap(uint8_t layer,uint8_t row ,uint8_t column ,uint16_t keycode) {

    int32_t position = zmk_matrix_transform_row_column_to_position(row, column);
    if(position<0) return;
    struct zmk_behavior_binding binding;
    memset(&binding,0,sizeof(binding));

    switch (keycode) {
        case BASIC_KEYCODE_RANGE:
        case MODIFIER_KEYCODE_RANGE:        
            binding.behavior_dev="KEY_PRESS";//"&kp";    
            binding.param1 =keycode;
            break;

        case SYSTEM_KEYCODE_RANGE:
            binding.behavior_dev="KEY_PRESS";//"&kp";
            binding.param1 =KEYCODE2SYSTEM(keycode)|(HID_USAGE_GD<<16);
            break;
        case CONSUMER_KEYCODE_RANGE:
            binding.behavior_dev="KEY_PRESS";//"&kp";
            binding.param1 =KEYCODE2CONSUMER(keycode)|(HID_USAGE_CONSUMER<<16);
            break;

        case MOUSE_KEYCODE_RANGE:
            binding.behavior_dev="KEY_PRESS";//"&kp";
            binding.param1 = keycode|(HID_USAGE_GEN_BUTTON<<16);
            break;
        case KC_NO:
            binding.behavior_dev="NONE";//"&none";
            break;
        case KC_TRANSPARENT:
            binding.behavior_dev="TRANS";//"&trans";
            break;
        case QK_MODS ... QK_MODS_MAX:
            binding.behavior_dev="KEY_PRESS";//"&kp";
            binding.param1 =(mod_to_zmk(QK_MODS_GET_MODS(keycode))<<24) | (HID_USAGE_KEY<<16)|QK_MODS_GET_BASIC_KEYCODE(keycode);

            break;
        case QK_LAYER_TAP ... QK_LAYER_TAP_MAX:
            binding.behavior_dev="LAYER_TAP";
            binding.param1= (keycode>>8) &0x0f;
            binding.param2= keycode&0xff;
            break;

        case QK_TO ... QK_TO_MAX:;
            // Layer set "GOTO"
            binding.behavior_dev="TO_LAYER";//"&to";
            binding.param1= keycode &0x1f;
            break;
        case QK_MOMENTARY ... QK_MOMENTARY_MAX:;
            // Momentary action_layer
            binding.behavior_dev="MO";// "&mo";
            binding.param1= keycode &0x1f;
            break;
        // case QK_DEF_LAYER ... QK_DEF_LAYER_MAX:;
        //     // Set default action_layer
        //     action_layer = QK_DEF_LAYER_GET_LAYER(keycode);
        //     action.code  = ACTION_DEFAULT_LAYER_SET(action_layer);
        //     break;
        case QK_TOGGLE_LAYER ... QK_TOGGLE_LAYER_MAX:;
            // Set toggle
            binding.behavior_dev="TOGGLE_LAYER";//"&tog";
            binding.param1= keycode &0x1f;
            break;

        case QK_ONE_SHOT_LAYER ... QK_ONE_SHOT_LAYER_MAX:;
            // OSL(action_layer) - One-shot action_layer
            binding.behavior_dev="STICKY_LAYER";//"&sl";
            binding.param1= keycode &0x1f;
            break;
        case QK_LAYER_TAP_TOGGLE ... QK_LAYER_TAP_TOGGLE_MAX:
            binding.behavior_dev="TAPD_LAYER";
            // void update_td_layer(uint32_t pos,uint8_t layer);
            // update_td_layer(position,keycode&0x1f);
            binding.param1 =keycode&0x1f;
            break;

        case QK_ONE_SHOT_MOD ... QK_ONE_SHOT_MOD_MAX:;
            // OSM(mod) - One-shot mod
            binding.behavior_dev="STICKY_KEY";//"&sk";
            binding.param1= keycode &0x1f;
            break;

        // case QK_LAYER_TAP_TOGGLE ... QK_LAYER_TAP_TOGGLE_MAX:
        //     binding.behavior_dev="LAYER_TAP";//"&lt";
        //     binding.param1= (keycode>>8) &0x0f;
        //     binding.param2= keycode &0xff;
        //     break;
        // case QK_LAYER_MOD ... QK_LAYER_MOD_MAX:
        //     mod          = mod_config(QK_LAYER_MOD_GET_MODS(keycode));
        //     action_layer = QK_LAYER_MOD_GET_LAYER(keycode);
        //     action.code  = ACTION_LAYER_MODS(action_layer, (mod & 0x10) ? mod << 4 : mod);
        //     break;

        case QK_MOD_TAP ... QK_MOD_TAP_MAX:
            binding.behavior_dev="MOD_TAP";//"&mt";
            binding.param1 =mod_to_zmk_keycode(QK_MODS_GET_MODS(keycode));//fix mod
            binding.param2 =QK_MODS_GET_BASIC_KEYCODE(keycode);  
            break;
        case QK_MACRO ... QK_MACRO_MAX:
            binding.behavior_dev="ZM_MA";
            binding.param1 =0x770000 |(keycode&0xff);
            break;
        case QK_GRAVE_ESCAPE:
            binding.behavior_dev="GRAVE_ESCAPE";
            break;
        case QK_SPACE_CADET_LEFT_CTRL_PARENTHESIS_OPEN:
            binding.behavior_dev="MOD_TAP";
            binding.param1 =LCTRL;
            binding.param2 =LS(N9);
            break;
        case QK_SPACE_CADET_RIGHT_CTRL_PARENTHESIS_CLOSE:
            binding.behavior_dev="MOD_TAP";
            binding.param1 =RCTRL;
            binding.param2 =LS(N0);
            break;    
        case QK_SPACE_CADET_LEFT_SHIFT_PARENTHESIS_OPEN:
            binding.behavior_dev="MOD_TAP";
            binding.param1 =LSHFT;
            binding.param2 =LS(N9);
            break;
        case QK_SPACE_CADET_RIGHT_SHIFT_PARENTHESIS_CLOSE:
            binding.behavior_dev="MOD_TAP";
            binding.param1 =RSHFT;
            binding.param2 =LS(N0);
            break;  
        case QK_SPACE_CADET_LEFT_ALT_PARENTHESIS_OPEN:
            binding.behavior_dev="MOD_TAP";
            binding.param1 =LALT;
            binding.param2 =LS(N9);
            break;
        case QK_SPACE_CADET_RIGHT_ALT_PARENTHESIS_CLOSE:
            binding.behavior_dev="MOD_TAP";
            binding.param1 =RALT;
            binding.param2 =LS(N0);
            break;  
        case QK_SPACE_CADET_RIGHT_SHIFT_ENTER:
            binding.behavior_dev="MOD_TAP";
            binding.param1 =RSHFT;
            binding.param2 =RET;
            break;

        case QK_KB ... QK_KB_MAX:
            switch(keycode)
            {
            case BT_HST1:
                binding.behavior_dev="bt_pair0";
                break;
            case BT_HST2:
                binding.behavior_dev="bt_pair1";
                break;
            case BT_HST3:
                binding.behavior_dev="bt_pair2";
                break;
            case PAIR_24G:
                binding.behavior_dev="bt_pair3";
                break;
            case KC_BOOT:
                binding.behavior_dev="lp_bootloader";
                break;
            case KC_SCRN_LOCK:
                binding.behavior_dev="KEY_PRESS";//"&kp";
                binding.param1 =C_AL_LOCK;
                break;
            case UC_CMD_CMA:
                binding.behavior_dev="user_custom";//"&kp";
                binding.param1 =LG(COMMA);
                break;
            case UC_LOPT:
                binding.behavior_dev="user_custom";//"&kp";
                binding.param1 =LALT;
                break;
            case UC_ROPT:
                binding.behavior_dev="user_custom";//"&kp";
                binding.param1 =RALT;
                break;
            case UC_LCMD:
                binding.behavior_dev="user_custom";//"&kp";
                binding.param1 =LCMD;
                break;
            case UC_RCMD:
                binding.behavior_dev="user_custom";//"&kp";
                binding.param1 =RCMD;
                break;
            case UC_CTRL_LEFT:
                binding.behavior_dev="user_custom";//"&kp";
                binding.param1 =LC(LEFT);
                break;
            case UC_CTRL_RIGHT:
                binding.behavior_dev="user_custom";//"&kp";
                binding.param1 =LC(RIGHT);
                break;
            case UC_EMOJI_MAC:
                binding.behavior_dev="user_custom";//"&kp";
                binding.param1 =LC(LG(SPACE));
                break;    
            case UC_TASK_VIEW:
                binding.behavior_dev="user_custom";//"&kp";
                binding.param1 =LG(TAB);
                break; 
            case UC_SWITCH_DESKTOP_LEFT:
                binding.behavior_dev="user_custom";//"&kp";
                binding.param1 =LG(LC(LEFT));
                break; 
            case UC_SWITCH_DESKTOP_RIGHT:
                binding.behavior_dev="user_custom";//"&kp";
                binding.param1 =LG(LC(RIGHT));
                break; 
            case UC_FILE_EXPLORER:
                binding.behavior_dev="user_custom";//"&kp";
                binding.param1 =LG(E);
                break; 
            case UC_LOCK:
                binding.behavior_dev="user_custom";//"&kp";
                binding.param1 =LG(L);
                break; 
            case UC_SETTINGS:
                binding.behavior_dev="user_custom";//"&kp";
                binding.param1 =LG(I);
                break;
            case UC_EMOJI_WIN:
                binding.behavior_dev="user_custom";//"&kp";
                binding.param1 =LG(DOT);
                break;
            case UC_PRNS_WIN:
                binding.behavior_dev="user_custom";//"&kp";
                binding.param1 =LG(LS(S));
                break;
            case UC_PRNS_MAC:
                binding.behavior_dev="user_custom";//"&kp";
                binding.param1 =LG(LS(N4));
                break;
            case UC_KEYCHRON:
                binding.behavior_dev="ZM_MA";//"&kp";
                binding.param1 =0x770100;
                break;
            }
            break;
        default:
            binding.behavior_dev="NONE";//"&none";
            break;
    }
    // LOG_DBG("layer:%d,row:%d,col:%d,keycode:%d to binding:%s,p1:%x,p2:%x",layer,row,column,keycode,binding.behavior_dev,binding.param1,binding.param2);
    update_zmk_keymap(layer,position,&binding);
}
void generate_via_keymaps(void)
{
    for (int layer = 0; layer < DYNAMIC_KEYMAP_LAYER_COUNT; layer++) {
        for (int row = 0; row < MATRIX_ROWS; row++) {
            for (int column = 0; column < MATRIX_COLS; column++) {
                 int32_t position = zmk_matrix_transform_row_column_to_position(row, column);
                 if(position>=0)
                 {
                    struct zmk_behavior_binding * binding =get_zmk_keymap(layer,position);
                    char * behavior_dev=binding->behavior_dev;
                    if(memcmp(behavior_dev,"KEY_PRESS",9)==0)//&kp
                    {
                        uint16_t keycode= binding->param1 &0xffff;
                        uint8_t page = binding->param1 >>16 &0xff;
                        uint8_t mod = binding->param1>>24;
                        
                        switch(page)
                        {
                            case HID_USAGE_GD:
                                gen_via_keymaps[layer][row][column]=KEYCODE2SYSTEM_R(keycode);
                                break;
                            case HID_USAGE_KEY:
                                gen_via_keymaps[layer][row][column]=keycode;
                                if(mod) gen_via_keymaps[layer][row][column]|=mod_to_via(mod) <<8;
                                break;
                            case HID_USAGE_CONSUMER:
                                if(keycode == HID_USAGE_CONSUMER_AL_TERMINAL_LOCK_SCREENSAVER)
                                {
                                    gen_via_keymaps[layer][row][column]=KC_SCRN_LOCK;  //custom
                                }
                                else
                                    gen_via_keymaps[layer][row][column]=KEYCODE2CONSUMER_R(keycode);
                                break;
                            case HID_USAGE_GEN_BUTTON:
                                gen_via_keymaps[layer][row][column] =keycode;
                                break;
                        }

                    } else if(memcmp(behavior_dev,"MO",2)==0) {     //&mo               
                        uint16_t keycode= binding->param1 &0xffff;
                        gen_via_keymaps[layer][row][column]=QK_MOMENTARY | (keycode &0x1f);

                    } else if(memcmp(behavior_dev,"TO_LAYER",8)==0) {//&to
                        uint16_t keycode= binding->param1 &0xffff;
                        gen_via_keymaps[layer][row][column]=QK_TO | (keycode &0x1f);

                    } else if(memcmp(behavior_dev,"TOGGLE_LAYER",12)==0) {//&tog
                        uint16_t keycode= binding->param1 &0xffff;
                        gen_via_keymaps[layer][row][column]=QK_TOGGLE_LAYER | (keycode &0x1f);

                    } else if(memcmp(behavior_dev,"TAPD_LAYER",10)==0) {//&tog
                        uint16_t keycode= binding->param1 &0x1f;
                        gen_via_keymaps[layer][row][column]=QK_LAYER_TAP_TOGGLE | (keycode );

                    } else if(memcmp(behavior_dev,"LAYER_TAP",9)==0) {//&lt

                        uint8_t layer=binding->param1&0x0f;
                        uint8_t keycode =binding->param2&0xff;
                        gen_via_keymaps[layer][row][column]=QK_LAYER_TAP | (layer<<8)| (keycode);

                    } else if(memcmp(behavior_dev,"MOD_TAP",7)==0) {//&mt
                        
                        uint8_t mod=binding->param1;
                        uint32_t keycode =binding->param2;
                        LOG_DBG("mod_tap,mod:%d,keycode:%x",mod,keycode);
                        if(mod ==RSHFT && keycode == RET )
                        {
                            gen_via_keymaps[layer][row][column] =SC_SENT;
                        }
                        else if(keycode >0xffff)
                        {
                            switch(keycode)
                            {
                            case LS(N9):
                                if(mod ==LSHFT)
                                {
                                    gen_via_keymaps[layer][row][column]=SC_LSPO;
                                }else if (mod ==LCTRL)
                                {
                                    gen_via_keymaps[layer][row][column]=SC_LCPO;
                                }else if (mod ==LALT)
                                {
                                    gen_via_keymaps[layer][row][column]=SC_LAPO;
                                }
                                break;
                            case LS(N0):
                                if(mod ==RSHFT)
                                {
                                    gen_via_keymaps[layer][row][column]=SC_RSPC;
                                }else if (mod ==RCTRL)
                                {
                                    gen_via_keymaps[layer][row][column]=SC_RCPC;
                                }else if (mod ==RALT)
                                {
                                    gen_via_keymaps[layer][row][column]=SC_RAPC;
                                }
                                break;

                            }
                        }
                        else
                            gen_via_keymaps[layer][row][column]=QK_MOD_TAP | (mod_kc_to_via(mod)<<8) | (keycode &0xff);

                    } else if(memcmp(behavior_dev,"STICKY_LAYER",12)==0) {//&sl

                        uint8_t layer =binding->param1&0x1f;
                        gen_via_keymaps[layer][row][column]=QK_ONE_SHOT_LAYER | (layer);

                    } else if(memcmp(behavior_dev,"STICKY_KEY",9)==0) {//&sk

                        uint8_t mod =binding->param1&0x1f;
                        gen_via_keymaps[layer][row][column]=QK_ONE_SHOT_MOD | (mod);

                    } else if(memcmp(behavior_dev,"TRANS",5)==0) {//&trans
                        
                        gen_via_keymaps[layer][row][column]=KC_TRANSPARENT;

                    } else if(memcmp(behavior_dev,"NONE",4)==0) {//&none

                        gen_via_keymaps[layer][row][column]=KC_NO;

                    } else if(memcmp(behavior_dev,"ZM_MA",5)==0){

                        if(binding->param1 ==0x770100)
                        {
                            gen_via_keymaps[layer][row][column]=UC_KEYCHRON;
                        }
                        else
                        gen_via_keymaps[layer][row][column]=QK_MACRO | (binding->param1 &0xff);

                    } else if(memcmp(behavior_dev,"bt_pair0",8)==0){

                        gen_via_keymaps[layer][row][column] =BT_HST1;
                    } else if(memcmp(behavior_dev,"bt_pair1",8)==0){

                        gen_via_keymaps[layer][row][column] =BT_HST2;

                    } else if(memcmp(behavior_dev,"bt_pair2",8)==0){

                        gen_via_keymaps[layer][row][column] =BT_HST3;

                    } else if(memcmp(behavior_dev,"bt_pair3",8)==0){

                        gen_via_keymaps[layer][row][column] =PAIR_24G;

                    } else if(memcmp(behavior_dev,"lp_bootloader",13)==0){

                        gen_via_keymaps[layer][row][column] =KC_BOOT;

                    } else if(memcmp(behavior_dev,"GRAVE_ESCAPE",12)==0){
                        
                        gen_via_keymaps[layer][row][column] =QK_GRAVE_ESCAPE;

                    } else if(memcmp(behavior_dev,"user_custom",11)==0) {

                        switch(binding->param1)
                        {
                        case LG(COMMA):
                                 gen_via_keymaps[layer][row][column] =UC_CMD_CMA;
                            break;
                        case LALT:
                                 gen_via_keymaps[layer][row][column] =UC_LOPT;
                            break;
                        case RALT:
                                 gen_via_keymaps[layer][row][column] =UC_ROPT;
                            break;
                        case LCMD:
                                 gen_via_keymaps[layer][row][column] =UC_LCMD;
                            break;
                        case RCMD:
                                 gen_via_keymaps[layer][row][column] =UC_RCMD;
                            break;
                        case LC(LEFT):
                                 gen_via_keymaps[layer][row][column] =UC_CTRL_LEFT;
                            break;
                        case LC(RIGHT):
                                 gen_via_keymaps[layer][row][column] =UC_CTRL_RIGHT;
                            break;
                        case LC(LG(SPACE)):
                                 gen_via_keymaps[layer][row][column] =UC_EMOJI_MAC;
                            break;
                        case LG(TAB):
                                 gen_via_keymaps[layer][row][column] =UC_TASK_VIEW;
                            break;
                        case LG(LC(LEFT)):
                                 gen_via_keymaps[layer][row][column] =UC_SWITCH_DESKTOP_LEFT;
                            break;
                        case LG(LC(RIGHT)):
                                 gen_via_keymaps[layer][row][column] =UC_SWITCH_DESKTOP_RIGHT;
                            break;
                        case LG(E):
                                 gen_via_keymaps[layer][row][column] =UC_FILE_EXPLORER;
                            break;
                        case LG(L):
                                 gen_via_keymaps[layer][row][column] =UC_LOCK;
                            break;
                        case LG(I):
                                 gen_via_keymaps[layer][row][column] =UC_SETTINGS;
                            break;
                        case LG(DOT):
                                 gen_via_keymaps[layer][row][column] =UC_EMOJI_WIN;
                            break;
                        case LG(LS(N4)):
                            gen_via_keymaps[layer][row][column] =UC_PRNS_MAC;
                            break;
                        case LG(LS(S)):
                            gen_via_keymaps[layer][row][column] =UC_PRNS_WIN;
                            break;

                        }

                    }

                    else {
                        gen_via_keymaps[layer][row][column]=KC_NO;
                    }
                    dynamic_keymap_set_keycode_no_update(layer, row, column, gen_via_keymaps[layer][row][column]);
                    //if(memcmp(behavior_dev,"TRANS",5)&&memcmp(behavior_dev,"KEY_PRESS",9) || layer ==3)
                    // if(memcmp(behavior_dev,"TRANS",5) && layer ==3)
                    // LOG_DBG("layer:%d,row:%d,col:%d,pos:%d,binding:%s,p1:%x,p2:%x,via keycode:%x",layer,row,column,position, binding->behavior_dev, binding->param1,binding->param2, gen_via_keymaps[layer][row][column]);
                 }
            }
        }
    }
}
