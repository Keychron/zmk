
#include "../version.h"
#include "launcher.h"

#include <zephyr/device.h>
#include <zephyr/init.h>

#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>

#include <zmk/usb.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/event_manager.h>
#include "keycodes.h"
#include "dynamic_keymap.h"
#include <zephyr/settings/settings.h>
#include <zmk/endpoints.h>
#include <zephyr/sys/reboot.h>
#if (CONFIG_SHIELD_KEYCHRON_B6_JIS||CONFIG_SHIELD_KEYCHRON_B1_JIS ||CONFIG_SHIELD_KEYCHRON_B2_JIS)
//for jis layout!
#include "sendstring_japanese.h"
#endif 

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

enum {
    VIA_PATH_USB,
    VIA_PATH_BLE,
    VIA_PATH_24G,
};
static uint8_t via_path;

void via_set_path(uint8_t path)
{
    via_path = path;
}
void user_set_debounce(uint32_t scan_period_ms,uint32_t debounce_press_ms,uint32_t debounce_release_ms);

uint8_t zmk_keymap_highest_layer_active();

int zmk_24g_send_via_report(uint8_t *payload,uint8_t payload_len);
static uint8_t report_os_sw_state;
void factory_test_rx(uint8_t *data, uint8_t length);
K_MSGQ_DEFINE(via_usb_msgq, 32,16, 4);
void via_usb_worker(struct k_work *work);
K_WORK_DEFINE(via_usb_work, via_usb_worker);

static const struct device *hid_via_dev;

static K_SEM_DEFINE(hid_via_sem, 1, 1);

void via_usb_worker(struct k_work *work)
{
    uint8_t usb_data[32];
    while (k_msgq_get(&via_usb_msgq, usb_data, K_NO_WAIT) == 0) {
        raw_hid_receive(usb_data,sizeof(usb_data));
    }
}

static void in_ready_cb(const struct device *dev) { k_sem_give(&hid_via_sem); }

static void out_ready_cb(const struct device *dev)
{
	uint8_t rev_buf[32]={0};
	uint32_t rev_bytes =0;
	hid_int_ep_read(dev,rev_buf,sizeof(rev_buf),&rev_bytes);
	LOG_HEXDUMP_DBG(rev_buf,8,"rx");
    // raw_hid_receive(rev_buf,rev_bytes);
    uint16_t hdr = (rev_buf[0]<<8) | rev_buf[1];
    if(hdr == 0xaa55 || hdr== 0xaa56)
    {
        void my_scdfu_data_handle(unsigned char *pdata,unsigned char rxlen);
        return my_scdfu_data_handle(&rev_buf[0],rev_bytes);
    }
    via_path = VIA_PATH_USB;
    int err = k_msgq_put(&via_usb_msgq, rev_buf, K_MSEC(100));
    if (err) {
        switch (err) {
        case -EAGAIN: {
            LOG_WRN("queue full");
            uint8_t buf[32];
            k_msgq_get(&via_usb_msgq, buf, K_NO_WAIT);
            k_msgq_put(&via_usb_msgq, rev_buf, K_MSEC(100));
        }
        default:
            LOG_WRN("Failed to queue via data (%d)", err);
        }
    }

    err = k_work_submit(&via_usb_work);
}

static const struct hid_ops ops = {
    .int_in_ready = in_ready_cb,
    .int_out_ready = out_ready_cb,
};

int zmk_usb_hid_via_send(const uint8_t *report, size_t len) {
    switch (zmk_usb_get_status()) {
   
    case USB_DC_ERROR:
    case USB_DC_RESET:
    case USB_DC_DISCONNECTED:
    case USB_DC_UNKNOWN:
        return -ENODEV;
    case USB_DC_SUSPEND:
         usb_wakeup_request();
         k_msleep(20);
    default:
        k_sem_take(&hid_via_sem, K_MSEC(10));
        LOG_HEXDUMP_DBG(report,8,"usb");
        int err = hid_int_ep_write(hid_via_dev, report, len, NULL);

        if (err!=0) {
            LOG_ERR("write err:%d",err);
            // k_sem_give(&hid_via_sem);
        }

        return err;
    }
}

static int zmk_usb_hid_via_init(const struct device *_arg) {
    hid_via_dev = device_get_binding("HID_1");
    if (hid_via_dev == NULL) {
        LOG_ERR("Unable to locate HID device");
        return -EINVAL;
    }

    usb_hid_register_device(hid_via_dev, zmk_hid_via_report_desc, sizeof(zmk_hid_via_report_desc), &ops);
    usb_hid_init(hid_via_dev);
    via_init();
    return 0;
}

SYS_INIT(zmk_usb_hid_via_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

_via_ee_device via_ee_device;

static struct user_debounce debouce;

struct direct_immediate_value {
    size_t len;
    void *dest;
    uint8_t fetched;
};
static int direct_loader_immediate_value(const char *name, size_t len,
                     settings_read_cb read_cb, void *cb_arg,
                     void *param)
{
    const char *next;
    size_t name_len;
    int rc;
    struct direct_immediate_value *one_value =
                    (struct direct_immediate_value *)param;

    name_len = settings_name_next(name, &next);

    if (name_len == 0) {
        if (len == one_value->len) {
            rc = read_cb(cb_arg, one_value->dest, len);
            if (rc >= 0) {
                one_value->fetched = 1;
                LOG_DBG("immediate load: OK.\n");
                return 0;
            }

            LOG_DBG("err:%d", rc);
            return rc;
        }
        return -EINVAL;
    }

    /* other keys aren't served by the callback
     * Return success in order to skip them
     * and keep storage processing.
     */
    return 0;
}

int load_immediate_value(const char *name, void *dest, size_t len)
{
    int rc;
    struct direct_immediate_value dov;

    dov.fetched = 0;
    dov.len = len;
    dov.dest = dest;

    rc = settings_load_subtree_direct(name, direct_loader_immediate_value,
                      (void *)&dov);
    if (rc == 0) {
        if (!dov.fetched) {
            rc = -ENOENT;
        }
    }

    return rc;
}
void via_ee_delete(void)
{
    LOG_DBG(".");
    settings_delete("via_ee/magic");
    settings_delete("via_ee/layout");
    settings_delete("via_ee/custom");
    // settings_delete("via_ee/keymaps");
    settings_delete("via_ee/encoder");
    settings_delete("via_ee/macros/len");
    settings_delete("via_ee/macros/datas");
    settings_delete("via_ee/debounce");
    // settings_delete("via_ee/changed");
    char setting_name[20]={0};
    for(int i=0;i<DYNAMIC_KEYMAP_LAYER_COUNT;i++)
    {
        memset(setting_name,0,sizeof(setting_name));
        sprintf(setting_name,"via_ee/keymaps/%d",i);
        settings_delete(setting_name);
        // memset(setting_name,0,sizeof(setting_name));
        // sprintf(setting_name,"via_ee/changed/%d",i);
        // settings_delete(setting_name);
    }
}
void via_ee_read_all(void)
{
    int rc;
    rc = load_immediate_value("via_ee/magic", &via_ee_device.magic[0], sizeof(via_ee_device.magic));
    if (rc == -ENOENT) {
       
        LOG_DBG("via_ee/magic:%02x%02x%02x (default)\n",via_ee_device.magic[0],via_ee_device.magic[1],via_ee_device.magic[2] );
    } else if (rc == 0) {
        LOG_DBG("via_ee/magic:%02x%02x%02x\n",via_ee_device.magic[0],via_ee_device.magic[1],via_ee_device.magic[2] );
    }

    rc = load_immediate_value("via_ee/layout", &via_ee_device.layout_options[0], sizeof(via_ee_device.layout_options));
    if (rc == -ENOENT) {

        LOG_DBG("via_ee/layout:%02x (default)\n",via_ee_device.layout_options[0]);
    } else if (rc == 0) {

        LOG_DBG("via_ee/layout:%02x \n",via_ee_device.layout_options[0]);
    }

#if VIA_EEPROM_CUSTOM_CONFIG_SIZE 
    rc = load_immediate_value("via_ee/custom", &via_ee_device.custom[0], sizeof(via_ee_device.custom));
    if (rc == -ENOENT) {
       
        LOG_DBG("via_ee/custom:%02x (default)\n",via_ee_device.custom[0]);
    } else if (rc == 0) {
        LOG_DBG("via_ee/custom:%02x \n",via_ee_device.custom[0]);
    }
#endif     

    via_ee_read_keymaps();
    // via_ee_read_changed_keys();

#ifdef ENCODER_MAP_ENABLE
    rc = load_immediate_value("via_ee/encoder", &via_ee_device.encoder[0], sizeof(via_ee_device.encoder));
    if (rc == -ENOENT) {
       
        LOG_DBG("via_ee/encoder:%02x (default)\n",via_ee_device.encoder[0]);
    } else if (rc == 0) {
        LOG_DBG("via_ee/encoder:%02x \n",via_ee_device.encoder[0]);
    }
#endif 
    //  rc = load_immediate_value("via_ee/macros", &via_ee_device.macros[0], sizeof(via_ee_device.macros));
    // if (rc == -ENOENT) {
       
    //     LOG_DBG("via_ee/macros:%02x (default)\n",via_ee_device.macros[0]);
    // } else if (rc == 0) {
    //     LOG_DBG("via_ee/macros:%02x \n",via_ee_device.macros[0]);
    // }
    via_ee_read_macros();

    via_ee_read_debounce();
}

void via_ee_read_magic(void)
{
    int rc;
    rc = load_immediate_value("via_ee/magic", &via_ee_device.magic[0], sizeof(via_ee_device.magic));
    if (rc == -ENOENT) {
       
        LOG_DBG("via_ee/magic:%02x%02x%02x (default)\n",via_ee_device.magic[0],via_ee_device.magic[1],via_ee_device.magic[2] );
    } else if (rc == 0) {
        LOG_DBG("via_ee/magic:%02x%02x%02x\n",via_ee_device.magic[0],via_ee_device.magic[1],via_ee_device.magic[2] );
    }
}
void via_ee_update_magic(void)
{
    int rc;
    rc = settings_save_one("via_ee/magic", (const void *)&via_ee_device.magic[0], sizeof(via_ee_device.magic));
    if (rc) {
        LOG_DBG("write failed:%d", rc);
    } else {
        LOG_DBG("OK.\n");
    }
}
void via_ee_update_layout(void)
{
    int rc;
    rc = settings_save_one("via_ee/layout", (const void *)&via_ee_device.layout_options[0], sizeof(via_ee_device.layout_options));
    if (rc) {
        LOG_DBG("write failed:%d", rc);
    } else {
        LOG_DBG("OK.\n");
    }
}
#if VIA_EEPROM_CUSTOM_CONFIG_SIZE 
void via_ee_update_custom(void)
{
    int rc;
    rc = settings_save_one("via_ee/custom", (const void *)&via_ee_device.custom[0], sizeof(via_ee_device.custom));
    if (rc) {
        LOG_DBG("write failed:%d", rc);
    } else {
        LOG_DBG("OK.\n");
    }
}
#endif
void via_ee_update_keymap(uint8_t i)
{
    if(i >=DYNAMIC_KEYMAP_LAYER_COUNT) return;
    int rc;
    char setting_name[20];
    sprintf(setting_name,"via_ee/keymaps/%d",i);
    rc = settings_save_one(setting_name, &via_ee_device.keymaps[i*KEYMAP_LEN], KEYMAP_LEN);
    if (rc) {
        LOG_DBG("write failed:%d", rc);
    } else {
        LOG_DBG("OK.\n");
    }

    // via_ee_update_changed_keys(i);
}
void via_ee_read_keymap(uint8_t layer)
{
    if(layer >=DYNAMIC_KEYMAP_LAYER_COUNT) return;
    int rc;
    char setting_name[20];
    sprintf(setting_name,"via_ee/keymaps/%d",layer);
    LOG_HEXDUMP_DBG(&via_ee_device.keymaps[layer*KEYMAP_LEN],32,"keymap");
    rc = load_immediate_value(setting_name, &via_ee_device.keymaps[layer*KEYMAP_LEN], KEYMAP_LEN);
    if (rc == -ENOENT) {
        LOG_DBG("via_ee/keymaps/%d:%02x (default)\n",layer,via_ee_device.keymaps[layer*KEYMAP_LEN]);
    } else if (rc == 0) {
        LOG_HEXDUMP_DBG(&via_ee_device.keymaps[layer*KEYMAP_LEN],32,"store keymap");

        for (int row = 0; row < MATRIX_ROWS; row++) {
            for (int column = 0; column < MATRIX_COLS; column++) {
                uint16_t keycode=(via_ee_device.keymaps[layer * KEYMAP_LEN+row*MATRIX_COLS*2+ column*2]<<8) +via_ee_device.keymaps[layer * KEYMAP_LEN+row*MATRIX_COLS*2+ column*2+1];
                if(keycode !=keycode_at_keymap_location_raw(layer, row, column))
                {
                    LOG_DBG("set layer:%d,row:%d,column:%d,keycode:%x",layer,row,column,keycode);
                    set_zmk_keymap(layer,row,column,keycode);
                }
            }
        }
    }
}

void via_ee_read_keymaps(void)
{
    for(int i=0;i<DYNAMIC_KEYMAP_LAYER_COUNT;i++)
    {
        via_ee_read_keymap(i);        
    }
}
void via_ee_update_keymaps(void)
{
    for(int i=0;i<DYNAMIC_KEYMAP_LAYER_COUNT;i++)
    {
        via_ee_update_keymap(i);        
    }
}
#ifdef ENCODER_MAP_ENABLE
void via_ee_update_encoder(void)
{
    int rc;
    rc = settings_save_one("via_ee/encoder", (const void *)&via_ee_device.encoder[0], sizeof(via_ee_device.encoder));
    if (rc) {
        LOG_DBG("write failed:%d", rc);
    } else {
        LOG_DBG("OK.\n");
    }
}
#endif 
int check_macros_len(void)
{
    int i;
    for(i= DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE-1;i>=0;i--)
    {
        if(via_ee_device.macros[i]!=0)
            break;
    }
    return i+1;
}
void via_ee_update_macros(void)
{
    int rc;
    int len =check_macros_len();
    LOG_DBG("macros len:%d",len);
    if(len >0)
    {
        rc = settings_save_one("via_ee/macros/len", (const void *)&len, sizeof(len));
        if (rc) {
            LOG_DBG("write failed:%d", rc);
        } else {
            LOG_DBG("OK.\n");
        }
        rc = settings_save_one("via_ee/macros/datas", (const void *)&via_ee_device.macros[0], len);
        if (rc) {
            LOG_DBG("write failed:%d", rc);
        } else {
            LOG_DBG("OK.\n");
        }
    }
}
void via_ee_delete_macros(void)
{
    LOG_DBG(".");
    settings_delete("via_ee/macros/len");
    settings_delete("via_ee/macros/datas");
}
void via_ee_read_macros(void)
{
    int rc;
    int len=0;
    rc=load_immediate_value("via_ee/macros/len", &len, sizeof(len));
    if (rc == -ENOENT) {
        LOG_DBG("via_ee/macros/len:%02x (default)\n",len);
        memset(&via_ee_device.macros,0,sizeof(via_ee_device.macros));
    } else if (rc == 0) {
        LOG_DBG("via_ee/macros/len:%02x \n",len);
    }

    if(len >0)
    {
        rc=load_immediate_value("via_ee/macros/datas", &via_ee_device.macros[0], len);
        if (rc == -ENOENT) {
           
            LOG_DBG("via_ee/macros/data:%02x (default)\n",len);
        } else if (rc == 0) {

            LOG_HEXDUMP_DBG(&via_ee_device.macros[0],32,"macros");
        }    
    }
}
// void via_ee_read_changed_keys(void)
// {
//     int rc;

//     char setting_name[20]={0};
//     for(int i=0;i<DYNAMIC_KEYMAP_LAYER_COUNT;i++)
//     {
//         sprintf(setting_name,"via_ee/changed/%d",i);
//         rc=load_immediate_value(setting_name, &via_ee_device.changed_keys[i], 20);
//         if (rc == -ENOENT) {
//             LOG_DBG("via_ee/changed/%02x (NONE)\n",i);
//             memset(&via_ee_device.changed_keys[i],0,20);
//         } else if (rc == 0) {
//             LOG_DBG("layer:%d",i);
//             LOG_HEXDUMP_DBG(&via_ee_device.changed_keys[i],20,"changed keys");
// #if 1            
//             uint8_t index =0;
//             for(int j=0;j<20;j++)
//             {
//                 uint8_t bits=0x01;
//                 for(int k=0;k<8;k++)
//                 {
//                     if(via_ee_device.changed_keys[i][j]&bits )
//                     {
//                         index = j*8+k;
//                         uint8_t row =index/MATRIX_COLS;
//                         uint8_t col = index%MATRIX_COLS;
//                         uint8_t layer =i;
//                         uint16_t keycode=(via_ee_device.keymaps[layer * KEYMAP_LEN+row*MATRIX_COLS*2+ col*2]<<8) +via_ee_device.keymaps[layer * KEYMAP_LEN+row*MATRIX_COLS*2+ col*2+1];
//                         LOG_DBG("key changed,index:%d,row:%d,col:%d,j:%d,bits:%x,keycode:%x",index,row,col,j,bits,keycode);
//                         set_zmk_keymap(layer,row,col,keycode);
//                     }
//                     bits <<=1;
//                 }
//             }
// #endif             
//         }
//     }
// }

// void via_ee_update_changed_keys(uint8_t layer)
// {
//     if(layer >=DYNAMIC_KEYMAP_LAYER_COUNT) return;
//     int rc;
//     char setting_name[20]={0};
//     sprintf(setting_name,"via_ee/changed/%d",layer);
//     rc = settings_save_one(setting_name, &via_ee_device.changed_keys[layer], 20);
//     if (rc) {
//         LOG_DBG("write failed:%d", rc);
//     } else {
//         LOG_DBG("OK.\n");
//     }
// }
// void via_set_changed_key(uint8_t layer,uint8_t row,uint8_t col)
// {
//     uint8_t index = row*MATRIX_COLS +col;
//     uint8_t pos = index/8;
//     uint8_t bits = 1<<(index %8);
//     via_ee_device.changed_keys[layer][pos] |= bits;
//     LOG_DBG("layer:%d,row:%d,col:%d,pos:%d,bits:%x",layer,row,col,pos,bits);
// }
// void via_ee_reset_changed_keys(void)
// {
//     // settings_delete("via_ee/changed");
//     char setting_name[20]={0};
//     for(int i=0;i<DYNAMIC_KEYMAP_LAYER_COUNT;i++)
//     {
//         sprintf(setting_name,"via_ee/changed/%d",i);
//         settings_delete(setting_name);
//     }
// }
void via_ee_reset_keymaps(void)
{
    LOG_DBG(".");
    char setting_name[20]={0};
    for(int i=0;i<DYNAMIC_KEYMAP_LAYER_COUNT;i++)
    {
        memset(setting_name,0,sizeof(setting_name));
        sprintf(setting_name,"via_ee/keymaps/%d",i);
        settings_delete(setting_name);
        // memset(setting_name,0,sizeof(setting_name));
        // sprintf(setting_name,"via_ee/changed/%d",i);
        // settings_delete(setting_name);
    }
}

uint32_t matrix_get_row(uint8_t row);
// {
//     return 0;
// }


void raw_hid_send(uint8_t *data, uint8_t length) {
    // TODO: implement variable size packet
    if (length != RAW_EPSIZE) {
        return;
    }
    
    switch(via_path)
    {
    case VIA_PATH_USB:
        zmk_usb_hid_via_send( data, length);
        break;
    case VIA_PATH_BLE:
        break;
    case VIA_PATH_24G:
        zmk_24g_send_via_report(data,length);
        break;
    }
    
}
bool via_eeprom_is_valid(void) {
    char *  p      = QMK_BUILDDATE; // e.g. "2019-11-05-11:29:54"
    uint8_t magic0 = ((p[2] & 0x0F) << 4) | (p[3] & 0x0F);
    uint8_t magic1 = ((p[5] & 0x0F) << 4) | (p[6] & 0x0F);
    uint8_t magic2 = ((p[8] & 0x0F) << 4) | (p[9] & 0x0F);

    return (eeprom_read_byte(VIA_EEPROM_MAGIC_ADDR + 0) == magic0 && eeprom_read_byte(VIA_EEPROM_MAGIC_ADDR + 1) == magic1 && eeprom_read_byte(VIA_EEPROM_MAGIC_ADDR + 2) == magic2);
}

// Sets VIA/keyboard level usage of EEPROM to valid/invalid
// Keyboard level code (eg. via_init_kb()) should not call this
void via_eeprom_set_valid(bool valid) {
    char *  p      = QMK_BUILDDATE; // e.g. "2019-11-05-11:29:54"
    uint8_t magic0 = ((p[2] & 0x0F) << 4) | (p[3] & 0x0F);
    uint8_t magic1 = ((p[5] & 0x0F) << 4) | (p[6] & 0x0F);
    uint8_t magic2 = ((p[8] & 0x0F) << 4) | (p[9] & 0x0F);

    eeprom_update_byte(VIA_EEPROM_MAGIC_ADDR + 0, valid ? magic0 : 0xFF);
    eeprom_update_byte(VIA_EEPROM_MAGIC_ADDR + 1, valid ? magic1 : 0xFF);
    eeprom_update_byte(VIA_EEPROM_MAGIC_ADDR + 2, valid ? magic2 : 0xFF);

    via_ee_update_magic();
}

// Override this at the keyboard code level to check
// VIA's EEPROM valid state and reset to defaults as needed.
// Used by keyboards that store their own state in EEPROM,
// for backlight, rotary encoders, etc.
// The override should not set via_eeprom_set_valid(true) as
// the caller also needs to check the valid state.
__attribute__((weak)) void via_init_kb(void) {
    int rc = settings_subsys_init();
    if (rc) {
        printk("settings subsys initialization: fail (err %d)\n", rc);
        return;
    }
    generate_via_keymaps();
    via_ee_read_magic();
}

// Called by QMK core to initialize dynamic keymaps etc.
void via_init(void) {
    // Let keyboard level test EEPROM valid state,
    // but not set it valid, it is done here.
    via_init_kb();
    via_set_layout_options_kb(via_get_layout_options());

    // If the EEPROM has the magic, the data is good.
    // OK to load from EEPROM.
    if (!via_eeprom_is_valid()) {
        via_ee_delete();
        eeconfig_init_via();
    }
    via_ee_read_all();
}

void eeconfig_init_via(void) {
    // set the magic number to false, in case this gets interrupted
    via_eeprom_set_valid(false);
    // This resets the layout options
    via_set_layout_options(VIA_EEPROM_LAYOUT_OPTIONS_DEFAULT);
    // This resets the keymaps in EEPROM to what is in flash.
    dynamic_keymap_reset(false);
    // This resets the macros in EEPROM to nothing.
    dynamic_keymap_macro_reset(false);
    // Save the magic number last, in case saving was interrupted
    via_eeprom_set_valid(true);
}

// This is generalized so the layout options EEPROM usage can be
// variable, between 1 and 4 bytes.
uint32_t via_get_layout_options(void) {
    uint32_t value = 0;
    // Start at the most significant byte
    uint16_t source = (VIA_EEPROM_LAYOUT_OPTIONS_ADDR);
    for (uint8_t i = 0; i < VIA_EEPROM_LAYOUT_OPTIONS_SIZE; i++) {
        value = value << 8;
        value |= eeprom_read_byte(source);
        source++;
    }
    return value;
}

__attribute__((weak)) void via_set_layout_options_kb(uint32_t value) {}

void via_set_layout_options(uint32_t value) {
    via_set_layout_options_kb(value);
    // Start at the least significant byte
    uint16_t target = (VIA_EEPROM_LAYOUT_OPTIONS_ADDR + VIA_EEPROM_LAYOUT_OPTIONS_SIZE - 1);
    for (uint8_t i = 0; i < VIA_EEPROM_LAYOUT_OPTIONS_SIZE; i++) {
        eeprom_update_byte(target, value & 0xFF);
        value = value >> 8;
        target--;
    }
    via_ee_update_layout();
}

#if defined(AUDIO_ENABLE)
float via_device_indication_song[][2] = SONG(STARTUP_SOUND);
#endif // AUDIO_ENABLE

// Used by VIA to tell a device to flash LEDs (or do something else) when that
// device becomes the active device being configured, on startup or switching
// between devices. This function will be called six times, at 200ms interval,
// with an incrementing value starting at zero. Since this function is called
// an even number of times, it can call a toggle function and leave things in
// the original state.
__attribute__((weak)) void via_set_device_indication(uint8_t value) {
#if defined(BACKLIGHT_ENABLE)
    backlight_toggle();
#endif // BACKLIGHT_ENABLE
#if defined(RGBLIGHT_ENABLE)
    rgblight_toggle_noeeprom();
#endif // RGBLIGHT_ENABLE
#if defined(RGB_MATRIX_ENABLE)
    rgb_matrix_toggle_noeeprom();
#endif // RGB_MATRIX_ENABLE
#if defined(LED_MATRIX_ENABLE)
    led_matrix_toggle_noeeprom();
#endif // LED_MATRIX_ENABLE
#if defined(AUDIO_ENABLE)
    if (value == 0) {
        wait_ms(10);
        PLAY_SONG(via_device_indication_song);
    }
#endif // AUDIO_ENABLE
}

// Called by QMK core to process VIA-specific keycodes.
// bool process_record_via(uint16_t keycode, keyrecord_t *record) {
//     // Handle macros
//     if (record->event.pressed) {
//         if (keycode >= QK_MACRO && keycode <= QK_MACRO_MAX) {
//             uint8_t id = keycode - QK_MACRO;
//             dynamic_keymap_macro_send(id);
//             return false;
//         }
//     }

//     return true;
// }

//
// via_custom_value_command() has the default handling of custom values for Core modules.
// If a keyboard is using the default Core modules, it does not need to be overridden,
// the VIA keyboard definition will have matching channel/IDs.
//
// If a keyboard has some extra custom values, then via_custom_value_command_kb() can be
// overridden to handle the extra custom values, leaving via_custom_value_command() to
// handle the custom values for Core modules.
//
// If a keyboard has custom values and code that are overlapping with Core modules,
// then via_custom_value_command() can be overridden and call the same functions
// as the default implementation, or do whatever else is required.
//
// DO NOT call raw_hid_send() in the override function.
//

// This is the default handler for "extra" custom values, i.e. keyboard-specific custom values
// that are not handled by via_custom_value_command().
__attribute__((weak)) void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    // data = [ command_id, channel_id, value_id, value_data ]
    uint8_t *command_id = &(data[0]);
    // Return the unhandled state
    *command_id = id_unhandled;
}

// This is the default handler for custom value commands.
// It routes commands with channel IDs to command handlers as such:
//
//      id_qmk_backlight_channel    ->  via_qmk_backlight_command()
//      id_qmk_rgblight_channel     ->  via_qmk_rgblight_command()
//      id_qmk_rgb_matrix_channel   ->  via_qmk_rgb_matrix_command()
//      id_qmk_led_matrix_channel   ->  via_qmk_led_matrix_command()
//      id_qmk_audio_channel        ->  via_qmk_audio_command()
//
__attribute__((weak)) void via_custom_value_command(uint8_t *data, uint8_t length) {
    // data = [ command_id, channel_id, value_id, value_data ]
    uint8_t *channel_id = &(data[1]);

#if defined(BACKLIGHT_ENABLE)
    if (*channel_id == id_qmk_backlight_channel) {
        via_qmk_backlight_command(data, length);
        return;
    }
#endif // BACKLIGHT_ENABLE

#if defined(RGBLIGHT_ENABLE)
    if (*channel_id == id_qmk_rgblight_channel) {
        via_qmk_rgblight_command(data, length);
        return;
    }
#endif // RGBLIGHT_ENABLE

#if defined(RGB_MATRIX_ENABLE)
    if (*channel_id == id_qmk_rgb_matrix_channel) {
        via_qmk_rgb_matrix_command(data, length);
        return;
    }
#endif // RGB_MATRIX_ENABLE

#if defined(LED_MATRIX_ENABLE)
    if (*channel_id == id_qmk_led_matrix_channel) {
        via_qmk_led_matrix_command(data, length);
        return;
    }
#endif // LED_MATRIX_ENABLE

#if defined(AUDIO_ENABLE)
    if (*channel_id == id_qmk_audio_channel) {
        via_qmk_audio_command(data, length);
        return;
    }
#endif // AUDIO_ENABLE

    (void)channel_id; // force use of variable

    // If we haven't returned before here, then let the keyboard level code
    // handle this, if it is overridden, otherwise by default, this will
    // return the unhandled state.
    via_custom_value_command_kb(data, length);
}

// Keyboard level code can override this, but shouldn't need to.
// Controlling custom features should be done by overriding
// via_custom_value_command_kb() instead.
__attribute__((weak)) bool via_command_kb(uint8_t *data, uint8_t length) {
    return false;
}

void raw_hid_receive(uint8_t *data, uint8_t length) {
    uint8_t *command_id   = &(data[0]);
    uint8_t *command_data = &(data[1]);

    // If via_command_kb() returns true, the command was fully
    // handled, including calling raw_hid_send()
    if (via_command_kb(data, length)) {
        return;
    }

    switch (*command_id) {
    case id_bootloader_jump:
        sys_reboot(0x4e);
        break;
    case kc_get_protocol_version:
            command_data[0]=1;
        break;
    case kc_get_default_layer:
        command_data[0]=zmk_keymap_highest_layer_active();
        break;
    case kc_get_firmware_version:
        // uint32_t value  = ZMK_VERSION;
        // command_data[1] = (value >> 24) & 0xFF;
        // command_data[2] = (value >> 16) & 0xFF;
        // command_data[3] = (value >> 8) & 0xFF;
        // command_data[4] = value & 0xFF;
        uint8_t offset =0;
        uint8_t len =strlen(APP_VERSION_STRING);
        memcpy(&command_data[offset],APP_VERSION_STRING,len);

        offset += len;
        command_data[offset++] =' ';
        len =strlen(__DATE__);
        memcpy(&command_data[offset],__DATE__,len);

        offset += len;
        command_data[offset++] =' ';    
 
        len =strlen(__TIME__);
        memcpy(&command_data[offset],__TIME__,len);
        
        break;

        case id_get_protocol_version: {
            command_data[0] = VIA_PROTOCOL_VERSION >> 8;
            command_data[1] = VIA_PROTOCOL_VERSION & 0xFF;
            break;
        }
        case id_get_keyboard_value: {
            switch (command_data[0]) {
                case id_uptime: {
                    uint32_t value  = k_uptime_get();//timer_read32();
                    command_data[1] = (value >> 24) & 0xFF;
                    command_data[2] = (value >> 16) & 0xFF;
                    command_data[3] = (value >> 8) & 0xFF;
                    command_data[4] = value & 0xFF;
                    break;
                }
                case id_layout_options: {
                    uint32_t value  = via_get_layout_options();
                    command_data[1] = (value >> 24) & 0xFF;
                    command_data[2] = (value >> 16) & 0xFF;
                    command_data[3] = (value >> 8) & 0xFF;
                    command_data[4] = value & 0xFF;
                    break;
                }
                case id_switch_matrix_state: {
                    uint8_t offset = command_data[1];
                    uint8_t rows   = 28 / ((MATRIX_COLS + 7) / 8);
                    uint8_t i      = 2;
                    for (uint8_t row = 0; row < rows && row + offset < MATRIX_ROWS; row++) {
                        matrix_row_t value = matrix_get_row(row + offset);
#if (MATRIX_COLS > 24)
                        command_data[i++] = (value >> 24) & 0xFF;
#endif
#if (MATRIX_COLS > 16)
                        command_data[i++] = (value >> 16) & 0xFF;
#endif
#if (MATRIX_COLS > 8)
                        command_data[i++] = (value >> 8) & 0xFF;
#endif
                        command_data[i++] = value & 0xFF;
                    }
                    break;
                }
                case id_firmware_version: {
                    uint32_t value  = VIA_FIRMWARE_VERSION;
                    command_data[1] = (value >> 24) & 0xFF;
                    command_data[2] = (value >> 16) & 0xFF;
                    command_data[3] = (value >> 8) & 0xFF;
                    command_data[4] = value & 0xFF;
                    break;
                }
                default: {
                    // The value ID is not known
                    // Return the unhandled state
                    *command_id = id_unhandled;
                    break;
                }
            }
            break;
        }
        case id_set_keyboard_value: {
            switch (command_data[0]) {
                case id_layout_options: {
                    uint32_t value = ((uint32_t)command_data[1] << 24) | ((uint32_t)command_data[2] << 16) | ((uint32_t)command_data[3] << 8) | (uint32_t)command_data[4];
                    via_set_layout_options(value);
                    break;
                }
                case id_device_indication: {
                    uint8_t value = command_data[1];
                    via_set_device_indication(value);
                    break;
                }
                default: {
                    // The value ID is not known
                    // Return the unhandled state
                    *command_id = id_unhandled;
                    break;
                }
            }
            break;
        }
        case id_dynamic_keymap_get_keycode: {
            uint16_t keycode = dynamic_keymap_get_keycode(command_data[0], command_data[1], command_data[2]);
            command_data[3]  = keycode >> 8;
            command_data[4]  = keycode & 0xFF;
            break;
        }
        case id_dynamic_keymap_set_keycode: {
            dynamic_keymap_set_keycode(command_data[0], command_data[1], command_data[2], (command_data[3] << 8) | command_data[4]);
            break;
        }
        case id_dynamic_keymap_reset: {
            dynamic_keymap_reset(true);
            
            break;
        }
        case id_custom_set_value:
        case id_custom_get_value:
        case id_custom_save: {
            via_custom_value_command(data, length);
            break;
        }
#ifdef VIA_EEPROM_ALLOW_RESET
        case id_eeprom_reset: {
            via_eeprom_set_valid(false);
            eeconfig_init_via();
            break;
        }
#endif
        case id_dynamic_keymap_macro_get_count: {
            command_data[0] = dynamic_keymap_macro_get_count();
            break;
        }
        case id_dynamic_keymap_macro_get_buffer_size: {
            uint16_t size   = dynamic_keymap_macro_get_buffer_size();
            command_data[0] = size >> 8;
            command_data[1] = size & 0xFF;
            break;
        }
        case id_dynamic_keymap_macro_get_buffer: {
            uint16_t offset = (command_data[0] << 8) | command_data[1];
            uint16_t size   = command_data[2]; // size <= 28
            dynamic_keymap_macro_get_buffer(offset, size, &command_data[3]);
            break;
        }
        case id_dynamic_keymap_macro_set_buffer: {
            uint16_t offset = (command_data[0] << 8) | command_data[1];
            uint16_t size   = command_data[2]; // size <= 28
            dynamic_keymap_macro_set_buffer(offset, size, &command_data[3]);
            break;
        }
        case id_dynamic_keymap_macro_reset: {
            dynamic_keymap_macro_reset(true);
            break;
        }
        case id_dynamic_keymap_get_layer_count: {
            command_data[0] = dynamic_keymap_get_layer_count();
            break;
        }
        case id_dynamic_keymap_get_buffer: {
            uint16_t offset = (command_data[0] << 8) | command_data[1];
            uint16_t size   = command_data[2]; // size <= 28
            dynamic_keymap_get_buffer(offset, size, &command_data[3]);
            break;
        }
        case id_dynamic_keymap_set_buffer: {
            uint16_t offset = (command_data[0] << 8) | command_data[1];
            uint16_t size   = command_data[2]; // size <= 28
            dynamic_keymap_set_buffer(offset, size, &command_data[3]);
            break;
        }
#ifdef ENCODER_MAP_ENABLE
        case id_dynamic_keymap_get_encoder: {
            uint16_t keycode = dynamic_keymap_get_encoder(command_data[0], command_data[1], command_data[2] != 0);
            command_data[3]  = keycode >> 8;
            command_data[4]  = keycode & 0xFF;
            break;
        }
        case id_dynamic_keymap_set_encoder: {
            dynamic_keymap_set_encoder(command_data[0], command_data[1], command_data[2] != 0, (command_data[3] << 8) | command_data[4]);
            break;
        }
#endif
        case 0xAB:
            factory_test_rx(data, length);
            break;

        case kc_set_key_debounce:
            if(command_data[0]==0 || command_data[1]==0 ||command_data[2] ==0)
            {
                break;
            }
            user_set_debounce(command_data[0],command_data[1],command_data[2]);
            debouce.scan_period_ms =command_data[0];
            debouce.debounce_press_ms = command_data[1];
            debouce.debounce_release_ms = command_data[2];
            via_save_debounce();
            break;
        case kc_get_key_debounce:
            command_data[0] =debouce.scan_period_ms;
            command_data[1] =debouce.debounce_press_ms;
            command_data[2] =debouce.debounce_release_ms;
            break;
        default: {
            // The command ID is not known
            // Return the unhandled state
            *command_id = id_unhandled;
            break;
        }
    }

    // Return the same buffer, optionally with values changed
    // (i.e. returning state to the host, or the unhandled state).
    raw_hid_send(data, length);
}

#if defined(BACKLIGHT_ENABLE)

void via_qmk_backlight_command(uint8_t *data, uint8_t length) {
    // data = [ command_id, channel_id, value_id, value_data ]
    uint8_t *command_id        = &(data[0]);
    uint8_t *value_id_and_data = &(data[2]);

    switch (*command_id) {
        case id_custom_set_value: {
            via_qmk_backlight_set_value(value_id_and_data);
            break;
        }
        case id_custom_get_value: {
            via_qmk_backlight_get_value(value_id_and_data);
            break;
        }
        case id_custom_save: {
            via_qmk_backlight_save();
            break;
        }
        default: {
            *command_id = id_unhandled;
            break;
        }
    }
}

#    if BACKLIGHT_LEVELS == 0
#        error BACKLIGHT_LEVELS == 0
#    endif

void via_qmk_backlight_get_value(uint8_t *data) {
    // data = [ value_id, value_data ]
    uint8_t *value_id   = &(data[0]);
    uint8_t *value_data = &(data[1]);
    switch (*value_id) {
        case id_qmk_backlight_brightness: {
            // level / BACKLIGHT_LEVELS * 255
            value_data[0] = ((uint16_t)get_backlight_level() * UINT8_MAX) / BACKLIGHT_LEVELS;
            break;
        }
        case id_qmk_backlight_effect: {
#    ifdef BACKLIGHT_BREATHING
            value_data[0] = is_backlight_breathing() ? 1 : 0;
#    else
            value_data[0] = 0;
#    endif
            break;
        }
    }
}

void via_qmk_backlight_set_value(uint8_t *data) {
    // data = [ value_id, value_data ]
    uint8_t *value_id   = &(data[0]);
    uint8_t *value_data = &(data[1]);
    switch (*value_id) {
        case id_qmk_backlight_brightness: {
            // level / 255 * BACKLIGHT_LEVELS
            backlight_level_noeeprom(((uint16_t)value_data[0] * BACKLIGHT_LEVELS) / UINT8_MAX);
            break;
        }
        case id_qmk_backlight_effect: {
#    ifdef BACKLIGHT_BREATHING
            if (value_data[0] == 0) {
                backlight_disable_breathing();
            } else {
                backlight_enable_breathing();
            }
#    endif
            break;
        }
    }
}

void via_qmk_backlight_save(void) {
    eeconfig_update_backlight_current();
}

#endif // BACKLIGHT_ENABLE

#if defined(RGBLIGHT_ENABLE)
#    ifndef RGBLIGHT_LIMIT_VAL
#        define RGBLIGHT_LIMIT_VAL 255
#    endif

void via_qmk_rgblight_command(uint8_t *data, uint8_t length) {
    // data = [ command_id, channel_id, value_id, value_data ]
    uint8_t *command_id        = &(data[0]);
    uint8_t *value_id_and_data = &(data[2]);

    switch (*command_id) {
        case id_custom_set_value: {
            via_qmk_rgblight_set_value(value_id_and_data);
            break;
        }
        case id_custom_get_value: {
            via_qmk_rgblight_get_value(value_id_and_data);
            break;
        }
        case id_custom_save: {
            via_qmk_rgblight_save();
            break;
        }
        default: {
            *command_id = id_unhandled;
            break;
        }
    }
}

void via_qmk_rgblight_get_value(uint8_t *data) {
    // data = [ value_id, value_data ]
    uint8_t *value_id   = &(data[0]);
    uint8_t *value_data = &(data[1]);
    switch (*value_id) {
        case id_qmk_rgblight_brightness: {
            value_data[0] = ((uint16_t)rgblight_get_val() * UINT8_MAX) / RGBLIGHT_LIMIT_VAL;
            break;
        }
        case id_qmk_rgblight_effect: {
            value_data[0] = rgblight_is_enabled() ? rgblight_get_mode() : 0;
            break;
        }
        case id_qmk_rgblight_effect_speed: {
            value_data[0] = rgblight_get_speed();
            break;
        }
        case id_qmk_rgblight_color: {
            value_data[0] = rgblight_get_hue();
            value_data[1] = rgblight_get_sat();
            break;
        }
    }
}

void via_qmk_rgblight_set_value(uint8_t *data) {
    // data = [ value_id, value_data ]
    uint8_t *value_id   = &(data[0]);
    uint8_t *value_data = &(data[1]);
    switch (*value_id) {
        case id_qmk_rgblight_brightness: {
            rgblight_sethsv_noeeprom(rgblight_get_hue(), rgblight_get_sat(), ((uint16_t)value_data[0] * RGBLIGHT_LIMIT_VAL) / UINT8_MAX);
            break;
        }
        case id_qmk_rgblight_effect: {
            if (value_data[0] == 0) {
                rgblight_disable_noeeprom();
            } else {
                rgblight_enable_noeeprom();
                rgblight_mode_noeeprom(value_data[0]);
            }
            break;
        }
        case id_qmk_rgblight_effect_speed: {
            rgblight_set_speed_noeeprom(value_data[0]);
            break;
        }
        case id_qmk_rgblight_color: {
            rgblight_sethsv_noeeprom(value_data[0], value_data[1], rgblight_get_val());
            break;
        }
    }
}

void via_qmk_rgblight_save(void) {
    eeconfig_update_rgblight_current();
}

#endif // QMK_RGBLIGHT_ENABLE

#if defined(RGB_MATRIX_ENABLE)

void via_qmk_rgb_matrix_command(uint8_t *data, uint8_t length) {
    // data = [ command_id, channel_id, value_id, value_data ]
    uint8_t *command_id        = &(data[0]);
    uint8_t *value_id_and_data = &(data[2]);

    switch (*command_id) {
        case id_custom_set_value: {
            via_qmk_rgb_matrix_set_value(value_id_and_data);
            break;
        }
        case id_custom_get_value: {
            via_qmk_rgb_matrix_get_value(value_id_and_data);
            break;
        }
        case id_custom_save: {
            via_qmk_rgb_matrix_save();
            break;
        }
        default: {
            *command_id = id_unhandled;
            break;
        }
    }
}

void via_qmk_rgb_matrix_get_value(uint8_t *data) {
    // data = [ value_id, value_data ]
    uint8_t *value_id   = &(data[0]);
    uint8_t *value_data = &(data[1]);

    switch (*value_id) {
        case id_qmk_rgb_matrix_brightness: {
            value_data[0] = ((uint16_t)rgb_matrix_get_val() * UINT8_MAX) / RGB_MATRIX_MAXIMUM_BRIGHTNESS;
            break;
        }
        case id_qmk_rgb_matrix_effect: {
            value_data[0] = rgb_matrix_is_enabled() ? rgb_matrix_get_mode() : 0;
            break;
        }
        case id_qmk_rgb_matrix_effect_speed: {
            value_data[0] = rgb_matrix_get_speed();
            break;
        }
        case id_qmk_rgb_matrix_color: {
            value_data[0] = rgb_matrix_get_hue();
            value_data[1] = rgb_matrix_get_sat();
            break;
        }
    }
}

void via_qmk_rgb_matrix_set_value(uint8_t *data) {
    // data = [ value_id, value_data ]
    uint8_t *value_id   = &(data[0]);
    uint8_t *value_data = &(data[1]);
    switch (*value_id) {
        case id_qmk_rgb_matrix_brightness: {
            rgb_matrix_sethsv_noeeprom(rgb_matrix_get_hue(), rgb_matrix_get_sat(), scale8(value_data[0], RGB_MATRIX_MAXIMUM_BRIGHTNESS));
            break;
        }
        case id_qmk_rgb_matrix_effect: {
            if (value_data[0] == 0) {
                rgb_matrix_disable_noeeprom();
            } else {
                rgb_matrix_enable_noeeprom();
                rgb_matrix_mode_noeeprom(value_data[0]);
            }
            break;
        }
        case id_qmk_rgb_matrix_effect_speed: {
            rgb_matrix_set_speed_noeeprom(value_data[0]);
            break;
        }
        case id_qmk_rgb_matrix_color: {
            rgb_matrix_sethsv_noeeprom(value_data[0], value_data[1], rgb_matrix_get_val());
            break;
        }
    }
}

void via_qmk_rgb_matrix_save(void) {
    eeconfig_update_rgb_matrix();
}

#endif // RGB_MATRIX_ENABLE

#if defined(LED_MATRIX_ENABLE)

void via_qmk_led_matrix_command(uint8_t *data, uint8_t length) {
    // data = [ command_id, channel_id, value_id, value_data ]
    uint8_t *command_id        = &(data[0]);
    uint8_t *value_id_and_data = &(data[2]);

    switch (*command_id) {
        case id_custom_set_value: {
            via_qmk_led_matrix_set_value(value_id_and_data);
            break;
        }
        case id_custom_get_value: {
            via_qmk_led_matrix_get_value(value_id_and_data);
            break;
        }
        case id_custom_save: {
            via_qmk_led_matrix_save();
            break;
        }
        default: {
            *command_id = id_unhandled;
            break;
        }
    }
}

void via_qmk_led_matrix_get_value(uint8_t *data) {
    // data = [ value_id, value_data ]
    uint8_t *value_id   = &(data[0]);
    uint8_t *value_data = &(data[1]);

    switch (*value_id) {
        case id_qmk_led_matrix_brightness: {
            value_data[0] = ((uint16_t)led_matrix_get_val() * UINT8_MAX) / LED_MATRIX_MAXIMUM_BRIGHTNESS;
            break;
        }
        case id_qmk_led_matrix_effect: {
            value_data[0] = led_matrix_is_enabled() ? led_matrix_get_mode() : 0;
            break;
        }
        case id_qmk_led_matrix_effect_speed: {
            value_data[0] = led_matrix_get_speed();
            break;
        }
    }
}

void via_qmk_led_matrix_set_value(uint8_t *data) {
    // data = [ value_id, value_data ]
    uint8_t *value_id   = &(data[0]);
    uint8_t *value_data = &(data[1]);
    switch (*value_id) {
        case id_qmk_led_matrix_brightness: {
            led_matrix_set_val_noeeprom(scale8(value_data[0], LED_MATRIX_MAXIMUM_BRIGHTNESS));
            break;
        }
        case id_qmk_led_matrix_effect: {
            if (value_data[0] == 0) {
                led_matrix_disable_noeeprom();
            } else {
                led_matrix_enable_noeeprom();
                led_matrix_mode_noeeprom(value_data[0]);
            }
            break;
        }
        case id_qmk_led_matrix_effect_speed: {
            led_matrix_set_speed_noeeprom(value_data[0]);
            break;
        }
    }
}

void via_qmk_led_matrix_save(void) {
    eeconfig_update_led_matrix();
}

#endif // LED_MATRIX_ENABLE

#if defined(AUDIO_ENABLE)

extern audio_config_t audio_config;

void via_qmk_audio_command(uint8_t *data, uint8_t length) {
    // data = [ command_id, channel_id, value_id, value_data ]
    uint8_t *command_id        = &(data[0]);
    uint8_t *value_id_and_data = &(data[2]);

    switch (*command_id) {
        case id_custom_set_value: {
            via_qmk_audio_set_value(value_id_and_data);
            break;
        }
        case id_custom_get_value: {
            via_qmk_audio_get_value(value_id_and_data);
            break;
        }
        case id_custom_save: {
            via_qmk_audio_save();
            break;
        }
        default: {
            *command_id = id_unhandled;
            break;
        }
    }
}

void via_qmk_audio_get_value(uint8_t *data) {
    // data = [ value_id, value_data ]
    uint8_t *value_id   = &(data[0]);
    uint8_t *value_data = &(data[1]);
    switch (*value_id) {
        case id_qmk_audio_enable: {
            value_data[0] = audio_config.enable ? 1 : 0;
            break;
        }
        case id_qmk_audio_clicky_enable: {
            value_data[0] = audio_config.clicky_enable ? 1 : 0;
            break;
        }
    }
}

void via_qmk_audio_set_value(uint8_t *data) {
    // data = [ value_id, value_data ]
    uint8_t *value_id   = &(data[0]);
    uint8_t *value_data = &(data[1]);
    switch (*value_id) {
        case id_qmk_audio_enable: {
            audio_config.enable = value_data[0] ? 1 : 0;
            break;
        }
        case id_qmk_audio_clicky_enable: {
            audio_config.clicky_enable = value_data[0] ? 1 : 0;
            break;
        }
    }
}

void via_qmk_audio_save(void) {
    eeconfig_update_audio(audio_config.raw);
}

#endif // QMK_AUDIO_ENABLE

void factory_test_send(uint8_t *payload, uint8_t length) {

    uint16_t checksum         = 0;
    uint8_t  data[RAW_EPSIZE] = {0};

    uint8_t i = 0;
    data[i++] = 0xAB;

    memcpy(&data[i], payload, length);
    i += length;

    for (uint8_t i = 1; i < RAW_EPSIZE - 3; i++)
        checksum += data[i];
    data[RAW_EPSIZE - 2] = checksum & 0xFF;
    data[RAW_EPSIZE - 1] = (checksum >> 8) & 0xFF;

    raw_hid_send(data, RAW_EPSIZE);

}
enum {
    FACTORY_TEST_CMD_BACKLIGHT = 0x01,
    FACTORY_TEST_CMD_OS_SWITCH,
    FACTORY_TEST_CMD_JUMP_TO_BL,
    FACTORY_TEST_CMD_INT_PIN,
    FACTORY_TEST_CMD_GET_TRANSPORT,
    FACTORY_TEST_CMD_CHARGING_ADC,
    FACTORY_TEST_CMD_RADIO_CARRIER,
    FACTORY_TEST_CMD_GET_BUILD_TIME,
};
enum {
    OS_SWITCH = 0x01,
};
void factory_test_rx(uint8_t *data, uint8_t length) {
    if (data[0] == 0xAB) {
        uint16_t checksum = 0;

        for (uint8_t i = 1; i < RAW_EPSIZE - 3; i++) {
            checksum += data[i];
        }
        /* Verify checksum */
        if ((checksum & 0xFF) != data[RAW_EPSIZE - 2] || checksum >> 8 != data[RAW_EPSIZE - 1]) return;



        switch (data[1]) {

            case FACTORY_TEST_CMD_OS_SWITCH:
                report_os_sw_state = data[2];
                if (report_os_sw_state) {
                    // dip_switch_read(true);
                }
                break;

        }
    }
}

bool dip_switch_update_user(uint8_t index, bool active) {
    LOG_ERR("index:%d,active:%d",index,active);
    if (report_os_sw_state) {

        uint8_t payload[3] = {FACTORY_TEST_CMD_OS_SWITCH, OS_SWITCH, active};
        factory_test_send(payload, 3);
    }

    return true;
}

void via_ee_read_debounce(void)
{
    int rc;
    rc = load_immediate_value("via_ee/debounce", &debouce, sizeof(debouce));
    if (rc == -ENOENT) {
       
        LOG_DBG("via_ee/debounce,(default)\n");
        debouce.scan_period_ms = DT_PROP(DT_NODELABEL(kscan0), debounce_scan_period_ms);
        debouce.debounce_press_ms = DT_PROP(DT_NODELABEL(kscan0), debounce_press_ms);
        debouce.debounce_release_ms = DT_PROP(DT_NODELABEL(kscan0), debounce_release_ms);
    } else if (rc == 0) {
        user_set_debounce(debouce.scan_period_ms,debouce.debounce_press_ms,debouce.debounce_release_ms);
    }
    LOG_ERR("via_ee/debounce:%02x%02x%02x\n",debouce.scan_period_ms,debouce.debounce_press_ms,debouce.debounce_release_ms );

}
void via_ee_update_debounce(void)
{
    int rc;
    rc = settings_save_one("via_ee/debounce", (const void *)&debouce, sizeof(debouce));
    if (rc) {
        LOG_DBG("write failed:%d", rc);
    } else {
        LOG_DBG("OK.\n");
    }
}

