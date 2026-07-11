#ifndef __MY_DFU_PRIVATE_H__
#define __MY_DFU_PRIVATE_H__
#include "../version.h"//<app_version.h>
unsigned char *my_dfu_get_prv_data(void);
unsigned char my_dfu_get_prv_len(void);

#if CONFIG_SHIELD_KEYCHRON_B1
#define MY_FWU_STRING_NAME "ZMK_KB_B1"
#define MY_FW_VERSION APP_VERSION_STRING
#define MY_HW_VERSION "B1N_v1.0"
#elif CONFIG_SHIELD_KEYCHRON_B2
#define MY_FWU_STRING_NAME "ZMK_KB_B2"
#define MY_FW_VERSION APP_VERSION_STRING
#define MY_HW_VERSION "B2N_v1.0"
#else
#define MY_FWU_STRING_NAME "ZMK_KB_B6"
#define MY_FW_VERSION APP_VERSION_STRING
#define MY_HW_VERSION "B6N_v1.0"
#endif 

#endif

