
#include <app_version.h>
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
#include <zephyr/drivers/kscan.h>
#include <zmk/matrix.h>
#include "rtl_pinmux.h"
#include "rtl_gpio.h"
#include "trace.h"
//
#if (CONFIG_SHIELD_KEYCHRON_B6_JIS || CONFIG_SHIELD_KEYCHRON_B1_JIS ||                             \
     CONFIG_SHIELD_KEYCHRON_B2_JIS)
// for jis layout!
#include "sendstring_japanese.h"
#endif
#if CONFIG_LED_STRIP
#include "../rgb/rgb_matrix.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <zmk/activity.h>

extern lpm_settings_t lpm_set;

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define HID_GET_REPORT_TYPE_MASK 0xff00
#define HID_GET_REPORT_ID_MASK 0x00ff
#define HID_REPORT_TYPE_INPUT 0x100
#define HID_REPORT_TYPE_OUTPUT 0x200
#define HID_REPORT_TYPE_FEATURE 0x300

#if CONFIG_LAUNCHER_USER_SET_DEBOUNCE
void user_set_debounce(uint32_t debounce_press_ms, uint32_t debounce_release_ms);
#endif
void set_report_rate(uint8_t div);
uint32_t matrix_get_row(uint8_t row);
uint8_t zmk_keymap_highest_layer_active();
void zmk_set_nkro_status(bool enable);

static uint8_t launcher_path;
void launcher_set_path(uint8_t path) { launcher_path = path; }
int zmk_24g_send_launcher_report(uint8_t *payload, uint8_t payload_len);
static uint8_t report_os_sw_state;
void factory_test_rx(uint8_t *data, uint8_t length);
K_MSGQ_DEFINE(launcher_usb_msgq, 32, 16, 4);
void launcher_usb_worker(struct k_work *work);
K_WORK_DEFINE(launcher_usb_work, launcher_usb_worker);

static const struct device *hid_launcher_dev;

static K_SEM_DEFINE(hid_launcher_sem, 1, 1);

void launcher_usb_worker(struct k_work *work) {
    uint8_t usb_data[32];
    while (k_msgq_get(&launcher_usb_msgq, usb_data, K_NO_WAIT) == 0) {
        launcher_set_path(LAUNCHER_PATH_USB);
        raw_hid_receive(usb_data, sizeof(usb_data));
    }
}

static void in_ready_cb(const struct device *dev) { k_sem_give(&hid_launcher_sem); }
#if 1
static void out_ready_cb(const struct device *dev) {
    uint8_t rev_buf[32] = {0};
    uint32_t rev_bytes = 0;
    hid_int_ep_read(dev, rev_buf, sizeof(rev_buf), &rev_bytes);

    int err = k_msgq_put(&launcher_usb_msgq, rev_buf, K_MSEC(100));
    if (err) {
        switch (err) {
        case -EAGAIN: {
            LOG_WRN("queue full");
            uint8_t buf[32];
            k_msgq_get(&launcher_usb_msgq, buf, K_NO_WAIT);
            k_msgq_put(&launcher_usb_msgq, rev_buf, K_MSEC(100));
        }
        default:
            LOG_WRN("Failed to queue launcher data (%d)", err);
        }
    }

    err = k_work_submit(&launcher_usb_work);
}
#else
static int set_report_cb(const struct device *dev, struct usb_setup_packet *setup, int32_t *len,
                         uint8_t **data) {
    if ((setup->wValue & HID_GET_REPORT_TYPE_MASK) != HID_REPORT_TYPE_OUTPUT) {
        LOG_ERR("Unsupported report type %d requested",
                (setup->wValue & HID_GET_REPORT_TYPE_MASK) >> 8);
        return -ENOTSUP;
    }
    LOG_WRN("setup->wValue:%d", setup->wValue);
    switch (setup->wValue & HID_GET_REPORT_ID_MASK) {
    case 0:
        uint8_t rev_buf[32] = {0};
        uint16_t length = *len;
        LOG_DBG("len:%d", length);
        // LOG_HEXDUMP_DBG(*data,length,"usb");
        memcpy(rev_buf, *data, sizeof(rev_buf));
        int err = k_msgq_put(&launcher_usb_msgq, rev_buf, K_MSEC(100));
        if (err) {
            switch (err) {
            case -EAGAIN: {
                LOG_WRN("queue full");
                uint8_t buf[32];
                k_msgq_get(&launcher_usb_msgq, buf, K_NO_WAIT);
                k_msgq_put(&launcher_usb_msgq, rev_buf, K_MSEC(100));
            }
            default:
                LOG_WRN("Failed to queue launcher data (%d)", err);
            }
        }
        err = k_work_submit(&launcher_usb_work);
        break;
    }
    return 0;
}
#endif
static int get_report_cb(const struct device *dev, struct usb_setup_packet *setup, int32_t *len,
                         uint8_t **data) {

    /*
     * 7.2.1 of the HID v1.11 spec is unclear about handling requests for reports that do not exist
     * For requested reports that aren't input reports, return -ENOTSUP like the Zephyr subsys does
     */
    if ((setup->wValue & HID_GET_REPORT_TYPE_MASK) != HID_REPORT_TYPE_INPUT) {
        LOG_ERR("Unsupported report type %d requested", (setup->wValue & HID_GET_REPORT_TYPE_MASK)
                                                            << 8);
        return -ENOTSUP;
    }
    LOG_WRN("setup->wValue:%d", setup->wValue);
    switch (setup->wValue & HID_GET_REPORT_ID_MASK) {
    case 0:
        break;
    }
    return 0;
}
static const struct hid_ops ops = {
    .int_in_ready = in_ready_cb, .int_out_ready = out_ready_cb, .get_report = get_report_cb,
    // .set_report = set_report_cb,
};

int zmk_usb_hid_launcher_send(const uint8_t *report, size_t len) {
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
        k_sem_take(&hid_launcher_sem, K_MSEC(10));
        LOG_HEXDUMP_DBG(report, 8, "usb");
        int err = hid_int_ep_write(hid_launcher_dev, report, len, NULL);

        if (err != 0) {
            LOG_ERR("write err:%d", err);
            // k_sem_give(&hid_launcher_sem);
        }

        return err;
    }
}

static int zmk_usb_hid_launcher_init(void) {
    hid_launcher_dev = device_get_binding("HID_1");
    if (hid_launcher_dev == NULL) {
        LOG_ERR("Unable to locate HID device");
        return -EINVAL;
    }

    usb_hid_register_device(hid_launcher_dev, zmk_hid_launcher_report_desc,
                            sizeof(zmk_hid_launcher_report_desc), &ops);
    usb_hid_init(hid_launcher_dev);
    // launcher_init();
    return 0;
}
#define CONFIG_APPLICATION_LAUNCHER_INIT_PRIORITY 95
SYS_INIT(zmk_usb_hid_launcher_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
SYS_INIT(launcher_init, APPLICATION, CONFIG_APPLICATION_LAUNCHER_INIT_PRIORITY);

_launcher_device launcher_device;
#if CONFIG_LAUNCHER_USER_SET_DEBOUNCE
static struct user_debounce debounce;
#endif
#if 1
struct direct_immediate_value {
    size_t len;
    void *dest;
    uint8_t fetched;
};
static int direct_loader_immediate_value(const char *name, size_t len, settings_read_cb read_cb,
                                         void *cb_arg, void *param) {
    const char *next;
    size_t name_len;
    int rc;
    struct direct_immediate_value *one_value = (struct direct_immediate_value *)param;

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

int load_immediate_value(const char *name, void *dest, size_t len) {
    int rc;
    struct direct_immediate_value dov;

    dov.fetched = 0;
    dov.len = len;
    dov.dest = dest;

    rc = settings_load_subtree_direct(name, direct_loader_immediate_value, (void *)&dov);
    if (rc == 0) {
        if (!dov.fetched) {
            rc = -ENOENT;
        }
    }

    return rc;
}
#endif
void launcher_delete(void) {
    LOG_DBG(".");
    settings_delete("launcher/magic");
    settings_delete("launcher/layout");
    settings_delete("launcher/custom");
    // settings_delete("launcher/keymaps");
    settings_delete("launcher/encoder");
    settings_delete("launcher/macros/len");
    settings_delete("launcher/macros/datas");
    settings_delete("launcher/nkro");
#if CONFIG_LAUNCHER_USER_SET_DEBOUNCE
    settings_delete("launcher/debounce");
#endif
    // settings_delete("launcher/changed");
    char setting_name[20] = {0};
    for (int i = 0; i < DYNAMIC_KEYMAP_LAYER_COUNT; i++) {
        memset(setting_name, 0, sizeof(setting_name));
        sprintf(setting_name, "launcher/keymaps/%d", i);
        settings_delete(setting_name);
        // memset(setting_name,0,sizeof(setting_name));
        // sprintf(setting_name,"launcher/changed/%d",i);
        // settings_delete(setting_name);
    }
}
static int launcher_handle_set(const char *name, size_t len, settings_read_cb read_cb,
                               void *cb_arg) {
    static int macro_len = 0;
    const char *next;
    LOG_DBG("Setting launcher value %s", name);

    if (settings_name_steq(name, "magic", NULL)) {
        if (len != sizeof(launcher_device.magic)) {
            LOG_ERR("Invalid magic size (got %d expected %d)", len, sizeof(launcher_device.magic));
            return -EINVAL;
        }

        int err = read_cb(cb_arg, &launcher_device.magic[0], sizeof(launcher_device.magic));
        if (err <= 0) {
            LOG_ERR("Failed to read magic  from settings (err %d)", err);
            return err;
        }
        LOG_DBG("magic:%02x,%02x,%02x", launcher_device.magic[0], launcher_device.magic[1],
                launcher_device.magic[2]);
    } else if (settings_name_steq(name, "layout", NULL)) {
        if (len != sizeof(launcher_device.layout_options)) {
            LOG_ERR("Invalid layout size (got %d expected %d)", len,
                    sizeof(launcher_device.layout_options));
            return -EINVAL;
        }

        int err = read_cb(cb_arg, &launcher_device.layout_options[0],
                          sizeof(launcher_device.layout_options));
        if (err <= 0) {
            LOG_ERR("Failed to read layout from settings (err %d)", err);
            return err;
        }
    } else if (settings_name_steq(name, "nkro", NULL)) {
        uint8_t nkro = 0;
        if (len != sizeof(nkro)) {
            LOG_ERR("Invalid nkro size (got %d expected %d)", len, sizeof(nkro));
            return -EINVAL;
        }

        int err = read_cb(cb_arg, &nkro, sizeof(nkro));
        if (err <= 0) {
            LOG_ERR("Failed to read nkro from settings (err %d)", err);
            return err;
        }
        zmk_set_nkro_status(nkro);
        LOG_ERR("set nkro:%d ", nkro);
    }
#ifdef CONFIG_ENABLE_WIN_LOCK
    else if (settings_name_steq(name, "winlock", NULL)) {
        extern uint8_t fn_win_lock;
        if (len != sizeof(fn_win_lock)) {
            LOG_ERR("Invalid fn_win_lock size (got %d expected %d)", len, sizeof(fn_win_lock));
            return -EINVAL;
        }

        int err = read_cb(cb_arg, &fn_win_lock, sizeof(fn_win_lock));
        if (err <= 0) {
            LOG_ERR("Failed to read fn_win_lock from settings (err %d)", err);
            return err;
        }

        LOG_ERR("set fn_win_lock:%d ", fn_win_lock);

    }
#endif
    else if (settings_name_steq(name, "keymaps", &next) && next) {
        char *endptr;
        uint8_t layer = strtoul(next, &endptr, 10);
        if (*endptr != '\0') {
            LOG_WRN("Invalid profile index: %s", next);
            return -EINVAL;
        }
        if (layer > DYNAMIC_KEYMAP_LAYER_COUNT)
            return -EINVAL;
        if (len != KEYMAP_LEN) {
            LOG_ERR("Invalid keymap/0 size (got %d expected %d)", len, KEYMAP_LEN);
            return -EINVAL;
        }

        int err = read_cb(cb_arg, &launcher_device.keymaps[layer * KEYMAP_LEN], KEYMAP_LEN);
        if (err <= 0) {
            LOG_ERR("Failed to keymap:%d from settings (err %d)", layer, err);
            return err;
        }
        LOG_DBG("update keymap:%d", layer);
        for (int row = 0; row < MATRIX_ROWS; row++) {
            for (int column = 0; column < MATRIX_COLS; column++) {
                uint16_t keycode =
                    (launcher_device
                         .keymaps[layer * KEYMAP_LEN + row * MATRIX_COLS * 2 + column * 2]
                     << 8) +
                    launcher_device
                        .keymaps[layer * KEYMAP_LEN + row * MATRIX_COLS * 2 + column * 2 + 1];
                if (keycode != keycode_at_keymap_location_raw(layer, row, column)) {
                    // LOG_DBG("set layer:%d,row:%d,column:%d,keycode:%x",layer,row,column,keycode);
                    set_zmk_keymap(layer, row, column, keycode);
                }
            }
        }
    }
#ifdef ENCODER_MAP_ENABLE
    else if (settings_name_steq(name, "encoder", NULL)) {
        if (len != sizeof(launcher_device.encoder)) {
            LOG_ERR("Invalid encoder size (got %d expected %d)", len,
                    sizeof(launcher_device.encoder));
            return -EINVAL;
        }

        int err = read_cb(cb_arg, &launcher_device.encoder[0], sizeof(launcher_device.encoder));
        if (err <= 0) {
            LOG_ERR("Failed to read encoder from settings (err %d)", err);
            return err;
        }
        for (int layer = 0; layer < DYNAMIC_KEYMAP_LAYER_COUNT; layer++) {
            for (int i = 0; i < 2; i++) {
                uint16_t keycode = (launcher_device.encoder[layer * 4 + i * 2] << 8) +
                                   launcher_device.encoder[layer * 4 + i * 2 + 1];
                LOG_DBG("layer:%d,i:%d,keycode:%x", layer, i, keycode);
                if (keycode != keycode_at_encodermap_location_raw(layer, 0, i == 0)) {
                    set_zmk_encoders(layer, i == 0, keycode);
                }
            }
        }

    }
#endif
    else if (settings_name_steq(name, "macros/len", NULL)) {

        if (len != sizeof(macro_len)) {
            LOG_ERR("Invalid macros/len size (got %d expected %d)", len, sizeof(macro_len));
            return -EINVAL;
        }

        int err = read_cb(cb_arg, &macro_len, sizeof(macro_len));
        if (err <= 0) {
            LOG_ERR("Failed to read macros/len  from settings (err %d)", err);
            return err;
        }
        LOG_DBG("macro len:%d", macro_len);
    } else if (settings_name_steq(name, "macros/datas", NULL)) {
        if (len != macro_len) {
            LOG_ERR("Invalid macros/datas (got %d expected %d)", len, macro_len);
            return -EINVAL;
        }

        int err = read_cb(cb_arg, &launcher_device.macros, macro_len);
        if (err <= 0) {
            LOG_ERR("Failed to read magic  from settings (err %d)", err);
            return err;
        }
        LOG_HEXDUMP_DBG(&launcher_device.macros[0], 32, "macros");
    }
#if CONFIG_LAUNCHER_USER_SET_DEBOUNCE
    else if (settings_name_steq(name, "debounce", NULL)) {
        if (len != sizeof(debounce)) {
            LOG_ERR("Invalid debounce (got %d expected %d)", len, sizeof(debounce));
            return -EINVAL;
        }

        int err = read_cb(cb_arg, &debounce, sizeof(debounce));
        if (err <= 0) {
            LOG_ERR("Failed to read debounce  from settings (err %d)", err);
            return err;
        }
        user_set_debounce(debounce.debounce_press_ms, debounce.debounce_release_ms);
#if CONFIG_ADD_PPT_REPORT_RATE
        if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_USB) {
            if (debounce.div <= 6)
                set_report_rate(debounce.div);
            LOG_ERR("stored usb div:%d", debounce.div);
        } else if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_PPT) {
            if (debounce.ppt_div <= 6)
                set_report_rate(debounce.ppt_div);
            LOG_ERR("stored ppt div:%d", debounce.ppt_div);
        }
#else
        if (debounce.div <= 6)
            set_report_rate(debounce.div);
#endif
    }
#endif
    else if (settings_name_steq(name, "lpm_set", NULL)) {
        if (len != sizeof(lpm_set)) {
            LOG_ERR("Invalid lpm_set size (got %d expected %d)", len, sizeof(lpm_set));
            return -EINVAL;
        }

        int err = read_cb(cb_arg, &lpm_set, sizeof(lpm_set));
        if (err <= 0) {
            LOG_ERR("Failed to read retail_demo_enable  from settings (err %d)", err);
            return err;
        }

        LOG_ERR("setting,lpm idle:%d,sleep:%d", lpm_set.max_idle_time, lpm_set.max_sleep_time);
        update_lpm_set(lpm_set.max_idle_time, lpm_set.max_sleep_time);
    }

    return 0;
}
struct settings_handler launcher_handler = {.name = "launcher", .h_set = launcher_handle_set};
#if 0
void launcher_read_all(void)
{
    int rc;
    rc = load_immediate_value("launcher/magic", &launcher_device.magic[0], sizeof(launcher_device.magic));
    if (rc == -ENOENT) {

        LOG_DBG("launcher/magic:%02x%02x%02x (default)\n",launcher_device.magic[0],launcher_device.magic[1],launcher_device.magic[2] );
    } else if (rc == 0) {
        LOG_DBG("launcher/magic:%02x%02x%02x\n",launcher_device.magic[0],launcher_device.magic[1],launcher_device.magic[2] );
    }

    rc = load_immediate_value("launcher/layout", &launcher_device.layout_options[0], sizeof(launcher_device.layout_options));
    if (rc == -ENOENT) {

        LOG_DBG("launcher/layout:%02x (default)\n",launcher_device.layout_options[0]);
    } else if (rc == 0) {

        LOG_DBG("launcher/layout:%02x \n",launcher_device.layout_options[0]);
    }

#if LAUNCHER_EEPROM_CUSTOM_CONFIG_SIZE
    rc = load_immediate_value("launcher/custom", &launcher_device.custom[0], sizeof(launcher_device.custom));
    if (rc == -ENOENT) {

        LOG_DBG("launcher/custom:%02x (default)\n",launcher_device.custom[0]);
    } else if (rc == 0) {
        LOG_DBG("launcher/custom:%02x \n",launcher_device.custom[0]);
    }
#endif
    uint8_t  nkro=0;
    rc = load_immediate_value("launcher/nkro", &nkro, sizeof(nkro));
    if (rc == -ENOENT) {

        LOG_DBG("launcher/nkro:%02x (default)\n",nkro);
    } else if (rc == 0) {

        LOG_DBG("launcher/nkro:%02x \n",nkro);

    }
    zmk_set_nkro_status(nkro);
    LOG_ERR("set nkro:%d ",nkro);
    launcher_read_keymaps();
    // launcher_read_changed_keys();

#ifdef ENCODER_MAP_ENABLE
    rc = load_immediate_value("launcher/encoder", &launcher_device.encoder[0], sizeof(launcher_device.encoder));
    if (rc == -ENOENT) {

        LOG_DBG("launcher/encoder:%02x (default)\n",launcher_device.encoder[0]);
    } else if (rc == 0) {

        for(int layer=0;layer<DYNAMIC_KEYMAP_LAYER_COUNT;layer++)
        {
            for(int i=0;i<2;i++)
            {
                uint16_t keycode=(launcher_device.encoder[layer *4+i*2 ]<<8) +launcher_device.encoder[layer*4 +i*2+1];
                LOG_DBG("layer:%d,i:%d,keycode:%x",layer,i,keycode);
                if(keycode !=keycode_at_encodermap_location_raw(layer,0 , i==0))
                {
                    set_zmk_encoders(layer,i==0,keycode);
                }
            }

        }

    }
#endif
    //  rc = load_immediate_value("launcher/macros", &launcher_device.macros[0], sizeof(launcher_device.macros));
    // if (rc == -ENOENT) {

    //     LOG_DBG("launcher/macros:%02x (default)\n",launcher_device.macros[0]);
    // } else if (rc == 0) {
    //     LOG_DBG("launcher/macros:%02x \n",launcher_device.macros[0]);
    // }
    launcher_read_macros();
#if CONFIG_LAUNCHER_USER_SET_DEBOUNCE
    launcher_read_debounce();
#endif
}
#endif

void launcher_read_magic(void) {
    int rc;
    rc = load_immediate_value("launcher/magic", &launcher_device.magic[0],
                              sizeof(launcher_device.magic));
    if (rc == -ENOENT) {

        LOG_DBG("launcher/magic:%02x%02x%02x (default)\n", launcher_device.magic[0],
                launcher_device.magic[1], launcher_device.magic[2]);
    } else if (rc == 0) {
        LOG_DBG("launcher/magic:%02x%02x%02x\n", launcher_device.magic[0], launcher_device.magic[1],
                launcher_device.magic[2]);
    }
}
void launcher_update_magic(void) {
    int rc;
    rc = settings_save_one("launcher/magic", (const void *)&launcher_device.magic[0],
                           sizeof(launcher_device.magic));
    if (rc) {
        LOG_DBG("write failed:%d", rc);
    } else {
        LOG_DBG("OK.\n");
    }
}
void launcher_update_layout(void) {
    int rc;
    rc = settings_save_one("launcher/layout", (const void *)&launcher_device.layout_options[0],
                           sizeof(launcher_device.layout_options));
    if (rc) {
        LOG_DBG("write failed:%d", rc);
    } else {
        LOG_DBG("OK.\n");
    }
}
void launcher_update_nkro(uint8_t nkro) {
    int rc;

    rc = settings_save_one("launcher/nkro", (const void *)&nkro, sizeof(nkro));
    if (rc) {
        LOG_DBG("write failed:%d", rc);
    } else {
        LOG_DBG("OK.\n");
    }
}
#ifdef CONFIG_ENABLE_WIN_LOCK
void launcher_update_winlock(uint8_t winlock) {
    int rc;

    rc = settings_save_one("launcher/winlock", (const void *)&winlock, sizeof(winlock));
    if (rc) {
        LOG_DBG("write failed:%d", rc);
    } else {
        LOG_DBG("OK.\n");
    }
}
#endif
#if LAUNCHER_EEPROM_CUSTOM_CONFIG_SIZE
void launcher_update_custom(void) {
    int rc;
    rc = settings_save_one("launcher/custom", (const void *)&launcher_device.custom[0],
                           sizeof(launcher_device.custom));
    if (rc) {
        LOG_DBG("write failed:%d", rc);
    } else {
        LOG_DBG("OK.\n");
    }
}
#endif
void launcher_update_keymap(uint8_t i) {
    if (i >= DYNAMIC_KEYMAP_LAYER_COUNT)
        return;
    int rc;
    char setting_name[20];
    sprintf(setting_name, "launcher/keymaps/%d", i);
    rc = settings_save_one(setting_name, &launcher_device.keymaps[i * KEYMAP_LEN], KEYMAP_LEN);
    if (rc) {
        LOG_DBG("write failed:%d", rc);
    } else {
        LOG_DBG("OK.\n");
    }
}
#if 0
void launcher_read_keymap(uint8_t layer)
{
    if(layer >=DYNAMIC_KEYMAP_LAYER_COUNT) return;
    int rc;
    char setting_name[20];
    sprintf(setting_name,"launcher/keymaps/%d",layer);
    LOG_HEXDUMP_DBG(&launcher_device.keymaps[layer*KEYMAP_LEN],32,"keymap");
    rc = load_immediate_value(setting_name, &launcher_device.keymaps[layer*KEYMAP_LEN], KEYMAP_LEN);
    if (rc == -ENOENT) {
        LOG_DBG("launcher/keymaps/%d:%02x (default)\n",layer,launcher_device.keymaps[layer*KEYMAP_LEN]);
    } else if (rc == 0) {
        LOG_HEXDUMP_DBG(&launcher_device.keymaps[layer*KEYMAP_LEN],32,"store keymap");

        for (int row = 0; row < MATRIX_ROWS; row++) {
            for (int column = 0; column < MATRIX_COLS; column++) {
                uint16_t keycode=(launcher_device.keymaps[layer * KEYMAP_LEN+row*MATRIX_COLS*2+ column*2]<<8) +launcher_device.keymaps[layer * KEYMAP_LEN+row*MATRIX_COLS*2+ column*2+1];
                if(keycode !=keycode_at_keymap_location_raw(layer, row, column))
                {
                    // LOG_DBG("set layer:%d,row:%d,column:%d,keycode:%x",layer,row,column,keycode);
                    set_zmk_keymap(layer,row,column,keycode);
                }
            }
        }
    }
}

void launcher_read_keymaps(void)
{
    for(int i=0;i<DYNAMIC_KEYMAP_LAYER_COUNT;i++)
    {
        launcher_read_keymap(i);
    }
}
#endif
void launcher_update_keymaps(void) {
    for (int i = 0; i < DYNAMIC_KEYMAP_LAYER_COUNT; i++) {
        launcher_update_keymap(i);
    }
}
#ifdef ENCODER_MAP_ENABLE
void launcher_update_encoder(void) {
    int rc;
    rc = settings_save_one("launcher/encoder", (const void *)&launcher_device.encoder[0],
                           sizeof(launcher_device.encoder));
    if (rc) {
        LOG_DBG("write failed:%d", rc);
    } else {
        LOG_DBG("OK.\n");
    }
}
#endif
int check_macros_len(void) {
    int i;
    for (i = DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE - 1; i >= 0; i--) {
        if (launcher_device.macros[i] != 0)
            break;
    }
    return i + 1;
}
void launcher_update_macros(void) {
    int rc;
    int len = check_macros_len();
    LOG_DBG("macros len:%d", len);
    if (len > 0) {
        rc = settings_save_one("launcher/macros/datas", (const void *)&launcher_device.macros[0],
                               len);
        if (rc) {
            LOG_DBG("write failed:%d", rc);
        } else {
            LOG_DBG("OK.\n");
        }
        rc = settings_save_one("launcher/macros/len", (const void *)&len, sizeof(len));
        if (rc) {
            LOG_DBG("write failed:%d", rc);
        } else {
            LOG_DBG("OK.\n");
        }
    }
}
void launcher_delete_macros(void) {
    LOG_DBG(".");
    settings_delete("launcher/macros/len");
    settings_delete("launcher/macros/datas");
}
#if 0
void launcher_read_macros(void)
{
    int rc;
    int len=0;
    rc=load_immediate_value("launcher/macros/len", &len, sizeof(len));
    if (rc == -ENOENT) {
        LOG_DBG("launcher/macros/len:%02x (default)\n",len);
        memset(&launcher_device.macros,0,sizeof(launcher_device.macros));
    } else if (rc == 0) {
        LOG_DBG("launcher/macros/len:%02x \n",len);
    }

    if(len >0)
    {
        rc=load_immediate_value("launcher/macros/datas", &launcher_device.macros[0], len);
        if (rc == -ENOENT) {

            LOG_DBG("launcher/macros/data:%02x (default)\n",len);
        } else if (rc == 0) {

            LOG_HEXDUMP_DBG(&launcher_device.macros[0],32,"macros");
        }
    }
}
#endif

void launcher_reset_keymaps(void) {
    LOG_DBG(".");
    char setting_name[20] = {0};
    for (int i = 0; i < DYNAMIC_KEYMAP_LAYER_COUNT; i++) {
        memset(setting_name, 0, sizeof(setting_name));
        sprintf(setting_name, "launcher/keymaps/%d", i);
        settings_delete(setting_name);
    }
    settings_delete("launcher/encoder");
}

void raw_hid_send(uint8_t *data, uint8_t length) {
    // TODO: implement variable size packet
    if (length != RAW_EPSIZE) {
        return;
    }

    switch (launcher_path) {
    case LAUNCHER_PATH_USB:
        zmk_usb_hid_launcher_send(data, length);
        break;
    case LAUNCHER_PATH_BLE:
        break;
    case LAUNCHER_PATH_PPT:
        zmk_24g_send_launcher_report(data, length);
        break;
    }
}
bool launcher_eeprom_is_valid(void) {
    char *p = ZMK_BUILDDATE; // e.g. "2024-04-05"
    uint8_t magic0 = ((p[2] & 0x0F) << 4) | (p[3] & 0x0F);
    uint8_t magic1 = ((p[5] & 0x0F) << 4) | (p[6] & 0x0F);
    uint8_t magic2 = ((p[8] & 0x0F) << 4) | (p[9] & 0x0F);

    return (eeprom_read_byte(LAUNCHER_EEPROM_MAGIC_ADDR + 0) == magic0 &&
            eeprom_read_byte(LAUNCHER_EEPROM_MAGIC_ADDR + 1) == magic1 &&
            eeprom_read_byte(LAUNCHER_EEPROM_MAGIC_ADDR + 2) == magic2);
}

// Sets LAUNCHER/keyboard level usage of EEPROM to valid/invalid
// Keyboard level code (eg. launcher_init_kb()) should not call this
void launcher_eeprom_set_valid(bool valid) {
    char *p = ZMK_BUILDDATE; // e.g. "2024-04-05"
    uint8_t magic0 = ((p[2] & 0x0F) << 4) | (p[3] & 0x0F);
    uint8_t magic1 = ((p[5] & 0x0F) << 4) | (p[6] & 0x0F);
    uint8_t magic2 = ((p[8] & 0x0F) << 4) | (p[9] & 0x0F);

    eeprom_update_byte(LAUNCHER_EEPROM_MAGIC_ADDR + 0, valid ? magic0 : 0xFF);
    eeprom_update_byte(LAUNCHER_EEPROM_MAGIC_ADDR + 1, valid ? magic1 : 0xFF);
    eeprom_update_byte(LAUNCHER_EEPROM_MAGIC_ADDR + 2, valid ? magic2 : 0xFF);

    launcher_update_magic();
}

// Override this at the keyboard code level to check
// LAUNCHER's EEPROM valid state and reset to defaults as needed.
// Used by keyboards that store their own state in EEPROM,
// for backlight, rotary encoders, etc.
// The override should not set launcher_eeprom_set_valid(true) as
// the caller also needs to check the valid state.
__attribute__((weak)) void launcher_init_kb(void) {
    int rc = settings_subsys_init();
    if (rc) {
        printk("settings subsys initialization: fail (err %d)\n", rc);
        return;
    }
    rc = settings_register(&launcher_handler);
    if (rc) {
        LOG_ERR("Failed to setup the profile settings handler (err %d)", rc);
        return;
    }
    generate_launcher_keymaps();
    launcher_read_magic();
}

// Called by ZMK core to initialize dynamic keymaps etc.
int launcher_init(void) {
    // Let keyboard level test EEPROM valid state,
    // but not set it valid, it is done here.
    launcher_init_kb();
    launcher_set_layout_options_kb(launcher_get_layout_options());

    // If the EEPROM has the magic, the data is good.
    // OK to load from EEPROM.
    if (!launcher_eeprom_is_valid()) {
        launcher_delete();
        eeconfig_init_launcher();
    }
    // launcher_read_all();
#if CONFIG_LAUNCHER_USER_SET_DEBOUNCE

    debounce.scan_period_ms = DT_PROP(DT_NODELABEL(kscan0), debounce_scan_period_ms);
    debounce.debounce_press_ms = DT_PROP(DT_NODELABEL(kscan0), debounce_press_ms);
    debounce.debounce_release_ms = DT_PROP(DT_NODELABEL(kscan0), debounce_release_ms);
#if defined(CONFIG_SHIELD_KEYCHRON_RS87_ANSI) || defined(CONFIG_SHIELD_KEYCHRON_K3SE2_ANSI) ||     \
    defined(CONFIG_SHIELD_KEYCHRON_K5SE2_ANSI)
    debounce.div = 3;
#if CONFIG_ADD_PPT_REPORT_RATE
    debounce.ppt_div = 3;
#endif
#else
    debounce.div = 3;
#if CONFIG_ADD_PPT_REPORT_RATE
    debounce.ppt_div = 3;
#endif
#endif
    debounce.mac_win_layer = 1;

    LOG_DBG("user debouce init,scan:%d,press:%d,release:%d", debounce.scan_period_ms,
            debounce.debounce_press_ms, debounce.debounce_release_ms);
#endif
    settings_load_subtree("launcher");
    return 0;
}

void eeconfig_init_launcher(void) {
    // set the magic number to false, in case this gets interrupted
    launcher_eeprom_set_valid(false);
    // This resets the layout options
    launcher_set_layout_options(LAUNCHER_EEPROM_LAYOUT_OPTIONS_DEFAULT);
    // This resets the keymaps in EEPROM to what is in flash.
    dynamic_keymap_reset(false);
    // This resets the macros in EEPROM to nothing.
    dynamic_keymap_macro_reset(false);
    // Save the magic number last, in case saving was interrupted
    launcher_eeprom_set_valid(true);
}

// This is generalized so the layout options EEPROM usage can be
// variable, between 1 and 4 bytes.
uint32_t launcher_get_layout_options(void) {
    uint32_t value = 0;
    // Start at the most significant byte
    uint16_t source = (LAUNCHER_EEPROM_LAYOUT_OPTIONS_ADDR);
    for (uint8_t i = 0; i < LAUNCHER_EEPROM_LAYOUT_OPTIONS_SIZE; i++) {
        value = value << 8;
        value |= eeprom_read_byte(source);
        source++;
    }
    return value;
}

__attribute__((weak)) void launcher_set_layout_options_kb(uint32_t value) {}

void launcher_set_layout_options(uint32_t value) {
    launcher_set_layout_options_kb(value);
    // Start at the least significant byte
    uint16_t target =
        (LAUNCHER_EEPROM_LAYOUT_OPTIONS_ADDR + LAUNCHER_EEPROM_LAYOUT_OPTIONS_SIZE - 1);
    for (uint8_t i = 0; i < LAUNCHER_EEPROM_LAYOUT_OPTIONS_SIZE; i++) {
        eeprom_update_byte(target, value & 0xFF);
        value = value >> 8;
        target--;
    }
    launcher_update_layout();
}

#if defined(AUDIO_ENABLE)
float launcher_device_indication_song[][2] = SONG(STARTUP_SOUND);
#endif // AUDIO_ENABLE

// Used by LAUNCHER to tell a device to flash LEDs (or do something else) when that
// device becomes the active device being configured, on startup or switching
// between devices. This function will be called six times, at 200ms interval,
// with an incrementing value starting at zero. Since this function is called
// an even number of times, it can call a toggle function and leave things in
// the original state.
__attribute__((weak)) void launcher_set_device_indication(uint8_t value) {
#if defined(BACKLIGHT_ENABLE)
    backlight_toggle();
#endif // BACKLIGHT_ENABLE
#if defined(RGBLIGHT_ENABLE)
    rgblight_toggle_noeeprom();
#endif // RGBLIGHT_ENABLE
#if defined(RGB_MATRIX_ENABLE)
    zmk_rgb_matrix_toggle_no_save();
#endif // RGB_MATRIX_ENABLE
#if defined(LED_MATRIX_ENABLE)
    led_matrix_toggle_noeeprom();
#endif // LED_MATRIX_ENABLE
#if defined(AUDIO_ENABLE)
    if (value == 0) {
        wait_ms(10);
        PLAY_SONG(launcher_device_indication_song);
    }
#endif // AUDIO_ENABLE
}

// Called by ZMK core to process LAUNCHER-specific keycodes.
// bool process_record_launcher(uint16_t keycode, keyrecord_t *record) {
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
// launcher_custom_value_command() has the default handling of custom values for Core modules.
// If a keyboard is using the default Core modules, it does not need to be overridden,
// the LAUNCHER keyboard definition will have matching channel/IDs.
//
// If a keyboard has some extra custom values, then launcher_custom_value_command_kb() can be
// overridden to handle the extra custom values, leaving launcher_custom_value_command() to
// handle the custom values for Core modules.
//
// If a keyboard has custom values and code that are overlapping with Core modules,
// then launcher_custom_value_command() can be overridden and call the same functions
// as the default implementation, or do whatever else is required.
//
// DO NOT call raw_hid_send() in the override function.
//

// This is the default handler for "extra" custom values, i.e. keyboard-specific custom values
// that are not handled by launcher_custom_value_command().
__attribute__((weak)) void launcher_custom_value_command_kb(uint8_t *data, uint8_t length) {
    // data = [ command_id, channel_id, value_id, value_data ]
    uint8_t *command_id = &(data[0]);
    // Return the unhandled state
    *command_id = id_unhandled;
}

// This is the default handler for custom value commands.
// It routes commands with channel IDs to command handlers as such:
//
//      id_zmk_backlight_channel    ->  launcher_zmk_backlight_command()
//      id_zmk_rgblight_channel     ->  launcher_zmk_rgblight_command()
//      id_zmk_rgb_matrix_channel   ->  launcher_zmk_rgb_matrix_command()
//      id_zmk_led_matrix_channel   ->  launcher_zmk_led_matrix_command()
//      id_zmk_audio_channel        ->  launcher_zmk_audio_command()
//
__attribute__((weak)) void launcher_custom_value_command(uint8_t *data, uint8_t length) {
    // data = [ command_id, channel_id, value_id, value_data ]
    uint8_t *channel_id = &(data[1]);

#if defined(BACKLIGHT_ENABLE)
    if (*channel_id == id_zmk_backlight_channel) {
        launcher_zmk_backlight_command(data, length);
        return;
    }
#endif // BACKLIGHT_ENABLE

#if defined(RGBLIGHT_ENABLE)
    if (*channel_id == id_zmk_rgblight_channel) {
        launcher_zmk_rgblight_command(data, length);
        return;
    }
#endif // RGBLIGHT_ENABLE

#if defined(RGB_MATRIX_ENABLE)
    if (*channel_id == id_zmk_rgb_matrix_channel) {
        launcher_zmk_rgb_matrix_command(data, length);
        return;
    }
#endif // RGB_MATRIX_ENABLE

#if defined(LED_MATRIX_ENABLE)
    if (*channel_id == id_zmk_led_matrix_channel) {
        launcher_zmk_led_matrix_command(data, length);
        return;
    }
#endif // LED_MATRIX_ENABLE

#if defined(AUDIO_ENABLE)
    if (*channel_id == id_zmk_audio_channel) {
        launcher_zmk_audio_command(data, length);
        return;
    }
#endif // AUDIO_ENABLE

    (void)channel_id; // force use of variable

    // If we haven't returned before here, then let the keyboard level code
    // handle this, if it is overridden, otherwise by default, this will
    // return the unhandled state.
    launcher_custom_value_command_kb(data, length);
}

// Keyboard level code can override this, but shouldn't need to.
// Controlling custom features should be done by overriding
// launcher_custom_value_command_kb() instead.
__attribute__((weak)) bool launcher_command_kb(uint8_t *data, uint8_t length) { return false; }

void raw_hid_receive(uint8_t *data, uint8_t length) {
    uint8_t *command_id = &(data[0]);
    uint8_t *command_data = &(data[1]);

    // If launcher_command_kb() returns true, the command was fully
    // handled, including calling raw_hid_send()
    if (launcher_command_kb(data, length)) {
        return;
    }

    switch (*command_id) {

    case kc_get_protocol_version:
        command_data[0] = 1;
        command_data[1] = 0;
        command_data[2] = 1;
        break;
    case kc_get_default_layer:
        command_data[0] = zmk_keymap_highest_layer_active();
        break;
    case kc_get_firmware_version:
        // uint32_t value  = ZMK_VERSION;
        // command_data[1] = (value >> 24) & 0xFF;
        // command_data[2] = (value >> 16) & 0xFF;
        // command_data[3] = (value >> 8) & 0xFF;
        // command_data[4] = value & 0xFF;
        uint8_t offset = 0;
        uint8_t len = strlen(LAUNCHER_VERSION_STRING);
        memcpy(&command_data[offset], LAUNCHER_VERSION_STRING, len);

        offset += len;
        command_data[offset++] = ' ';
        len = strlen(__DATE__);
        memcpy(&command_data[offset], __DATE__, len);

        offset += len;
        command_data[offset++] = ' ';

        len = strlen(__TIME__);
        memcpy(&command_data[offset], __TIME__, len);

        break;

    case kc_get_support_feature:
        // command_data[0]=0;
        command_data[0] = FEATURE_DEFAULT_LAYER
#ifdef CONFIG_LAUNCHER_USER_SET_DEBOUNCE
                          | FEATURE_DYNAMIC_DEBOUNCE
#endif
#ifdef CONFIG_SNAP_CLICK_ENABLE
                          | FEATURE_SNAP_CLICK
#endif
#ifdef CONFIG_KEYCHRON_RGB_ENABLE
                          | FEATURE_KEYCHRON_RGB
#endif
            ;

        break;

    case id_get_protocol_version: {
        command_data[0] = LAUNCHER_PROTOCOL_VERSION >> 8;
        command_data[1] = LAUNCHER_PROTOCOL_VERSION & 0xFF;
        break;
    }
    case id_get_keyboard_value: {
        switch (command_data[0]) {
        case id_uptime: {
            uint32_t value = k_uptime_get(); // timer_read32();
            command_data[1] = (value >> 24) & 0xFF;
            command_data[2] = (value >> 16) & 0xFF;
            command_data[3] = (value >> 8) & 0xFF;
            command_data[4] = value & 0xFF;
            break;
        }
        case id_layout_options: {
            uint32_t value = launcher_get_layout_options();
            command_data[1] = (value >> 24) & 0xFF;
            command_data[2] = (value >> 16) & 0xFF;
            command_data[3] = (value >> 8) & 0xFF;
            command_data[4] = value & 0xFF;
            break;
        }
        case id_switch_matrix_state: {
            uint8_t offset = command_data[1];
            uint8_t rows = 28 / ((MATRIX_COLS + 7) / 8);
            uint8_t i = 2;
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
            uint32_t value = LAUNCHER_FIRMWARE_VERSION;
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
            uint32_t value = ((uint32_t)command_data[1] << 24) | ((uint32_t)command_data[2] << 16) |
                             ((uint32_t)command_data[3] << 8) | (uint32_t)command_data[4];
            launcher_set_layout_options(value);
            break;
        }
        case id_device_indication: {
            uint8_t value = command_data[1];
            launcher_set_device_indication(value);
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
        uint16_t keycode =
            dynamic_keymap_get_keycode(command_data[0], command_data[1], command_data[2]);
        command_data[3] = keycode >> 8;
        command_data[4] = keycode & 0xFF;
        break;
    }
    case id_dynamic_keymap_set_keycode: {
        dynamic_keymap_set_keycode(command_data[0], command_data[1], command_data[2],
                                   (command_data[3] << 8) | command_data[4]);
        break;
    }
    case id_dynamic_keymap_reset: {
        dynamic_keymap_reset(true);

        break;
    }
    case id_custom_set_value:
    case id_custom_get_value:
    case id_custom_save: {
        launcher_custom_value_command(data, length);
        break;
    }
#ifdef LAUNCHER_EEPROM_ALLOW_RESET
    case id_eeprom_reset: {
        launcher_eeprom_set_valid(false);
        eeconfig_init_launcher();
        break;
    }
#endif
    case id_dynamic_keymap_macro_get_count: {
        command_data[0] = dynamic_keymap_macro_get_count();
        break;
    }
    case id_dynamic_keymap_macro_get_buffer_size: {
        uint16_t size = dynamic_keymap_macro_get_buffer_size();
        command_data[0] = size >> 8;
        command_data[1] = size & 0xFF;
        break;
    }
    case id_dynamic_keymap_macro_get_buffer: {
        uint16_t offset = (command_data[0] << 8) | command_data[1];
        uint16_t size = command_data[2]; // size <= 28
        dynamic_keymap_macro_get_buffer(offset, size, &command_data[3]);
        break;
    }
    case id_dynamic_keymap_macro_set_buffer: {
        uint16_t offset = (command_data[0] << 8) | command_data[1];
        uint16_t size = command_data[2]; // size <= 28
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
        uint16_t size = command_data[2]; // size <= 28
        dynamic_keymap_get_buffer(offset, size, &command_data[3]);
        break;
    }
    case id_dynamic_keymap_set_buffer: {
        uint16_t offset = (command_data[0] << 8) | command_data[1];
        uint16_t size = command_data[2]; // size <= 28
        dynamic_keymap_set_buffer(offset, size, &command_data[3]);
        break;
    }
#ifdef ENCODER_MAP_ENABLE
    case id_dynamic_keymap_get_encoder: {
        uint16_t keycode =
            dynamic_keymap_get_encoder(command_data[0], command_data[1], command_data[2] != 0);
        command_data[3] = keycode >> 8;
        command_data[4] = keycode & 0xFF;
        break;
    }
    case id_dynamic_keymap_set_encoder: {
        dynamic_keymap_set_encoder(command_data[0], command_data[1], command_data[2] != 0,
                                   (command_data[3] << 8) | command_data[4]);
        break;
    }
#endif
    case 0xAB:
        factory_test_rx(data, length);
        return;
        break;
    case 0xa7:
        void kc_user_cmd(uint8_t * command_data);
        kc_user_cmd(command_data);
        break;
#ifdef CONFIG_KEYCHRON_RGB_ENABLE
    case 0xa8:
        void kc_rgb_matrix_rx(uint8_t * data, uint8_t length);
        kc_rgb_matrix_rx(data, length);
        break;
#endif
#if 0 // CONFIG_LAUNCHER_USER_SET_DEBOUNCE
        case kc_get_set_debounce:
        {
            switch(command_data[0])
            {
            case kc_set_key_debounce:
                uint8_t debounce_type = command_data[1];
                uint8_t debounce_sub_type = command_data[2];
                if(debounce_type==1)
                {
                    if(debounce_sub_type ==1)
                    {
                        debounce.debounce_press_ms = command_data[3];
                        debounce.debounce_release_ms = command_data[3];
                    }
                    else if(debounce_sub_type ==2)
                    {
                        debounce.debounce_press_ms = command_data[3];
                    }
                    else if(debounce_sub_type ==3)
                    {
                        debounce.debounce_release_ms = command_data[4];
                    }
                    else if(debounce_sub_type ==4)
                    {
                        debounce.debounce_press_ms = command_data[3];
                        debounce.debounce_release_ms = command_data[4];
                    }

                    user_set_debounce(debounce.debounce_press_ms,debounce.debounce_release_ms);

                    launcher_save_debounce();
                }

                break;
            case kc_get_key_debounce:

                command_data[1] =1;
                command_data[2] =4;
                command_data[3] =debounce.debounce_press_ms;
                command_data[4] =debounce.debounce_release_ms;
                break;
            }
        }
        break;
#endif
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

void launcher_zmk_backlight_command(uint8_t *data, uint8_t length) {
    // data = [ command_id, channel_id, value_id, value_data ]
    uint8_t *command_id = &(data[0]);
    uint8_t *value_id_and_data = &(data[2]);

    switch (*command_id) {
    case id_custom_set_value: {
        launcher_zmk_backlight_set_value(value_id_and_data);
        break;
    }
    case id_custom_get_value: {
        launcher_zmk_backlight_get_value(value_id_and_data);
        break;
    }
    case id_custom_save: {
        launcher_zmk_backlight_save();
        break;
    }
    default: {
        *command_id = id_unhandled;
        break;
    }
    }
}

#if BACKLIGHT_LEVELS == 0
#error BACKLIGHT_LEVELS == 0
#endif

void launcher_zmk_backlight_get_value(uint8_t *data) {
    // data = [ value_id, value_data ]
    uint8_t *value_id = &(data[0]);
    uint8_t *value_data = &(data[1]);
    switch (*value_id) {
    case id_zmk_backlight_brightness: {
        // level / BACKLIGHT_LEVELS * 255
        value_data[0] = ((uint16_t)get_backlight_level() * UINT8_MAX) / BACKLIGHT_LEVELS;
        break;
    }
    case id_zmk_backlight_effect: {
#ifdef BACKLIGHT_BREATHING
        value_data[0] = is_backlight_breathing() ? 1 : 0;
#else
        value_data[0] = 0;
#endif
        break;
    }
    }
}

void launcher_zmk_backlight_set_value(uint8_t *data) {
    // data = [ value_id, value_data ]
    uint8_t *value_id = &(data[0]);
    uint8_t *value_data = &(data[1]);
    switch (*value_id) {
    case id_zmk_backlight_brightness: {
        // level / 255 * BACKLIGHT_LEVELS
        backlight_level_noeeprom(((uint16_t)value_data[0] * BACKLIGHT_LEVELS) / UINT8_MAX);
        break;
    }
    case id_zmk_backlight_effect: {
#ifdef BACKLIGHT_BREATHING
        if (value_data[0] == 0) {
            backlight_disable_breathing();
        } else {
            backlight_enable_breathing();
        }
#endif
        break;
    }
    }
}

void launcher_zmk_backlight_save(void) { eeconfig_update_backlight_current(); }

#endif // BACKLIGHT_ENABLE

#if defined(RGBLIGHT_ENABLE)
#ifndef RGBLIGHT_LIMIT_VAL
#define RGBLIGHT_LIMIT_VAL 255
#endif

void launcher_zmk_rgblight_command(uint8_t *data, uint8_t length) {
    // data = [ command_id, channel_id, value_id, value_data ]
    uint8_t *command_id = &(data[0]);
    uint8_t *value_id_and_data = &(data[2]);

    switch (*command_id) {
    case id_custom_set_value: {
        launcher_zmk_rgblight_set_value(value_id_and_data);
        break;
    }
    case id_custom_get_value: {
        launcher_zmk_rgblight_get_value(value_id_and_data);
        break;
    }
    case id_custom_save: {
        launcher_zmk_rgblight_save();
        break;
    }
    default: {
        *command_id = id_unhandled;
        break;
    }
    }
}

void launcher_zmk_rgblight_get_value(uint8_t *data) {
    // data = [ value_id, value_data ]
    uint8_t *value_id = &(data[0]);
    uint8_t *value_data = &(data[1]);
    switch (*value_id) {
    case id_zmk_rgblight_brightness: {
        value_data[0] = ((uint16_t)rgblight_get_val() * UINT8_MAX) / RGBLIGHT_LIMIT_VAL;
        break;
    }
    case id_zmk_rgblight_effect: {
        value_data[0] = rgblight_is_enabled() ? rgblight_get_mode() : 0;
        break;
    }
    case id_zmk_rgblight_effect_speed: {
        value_data[0] = rgblight_get_speed();
        break;
    }
    case id_zmk_rgblight_color: {
        value_data[0] = rgblight_get_hue();
        value_data[1] = rgblight_get_sat();
        break;
    }
    }
}

void launcher_zmk_rgblight_set_value(uint8_t *data) {
    // data = [ value_id, value_data ]
    uint8_t *value_id = &(data[0]);
    uint8_t *value_data = &(data[1]);
    switch (*value_id) {
    case id_zmk_rgblight_brightness: {
        rgblight_sethsv_noeeprom(rgblight_get_hue(), rgblight_get_sat(),
                                 ((uint16_t)value_data[0] * RGBLIGHT_LIMIT_VAL) / UINT8_MAX);
        break;
    }
    case id_zmk_rgblight_effect: {
        if (value_data[0] == 0) {
            rgblight_disable_noeeprom();
        } else {
            rgblight_enable_noeeprom();
            rgblight_mode_noeeprom(value_data[0]);
        }
        break;
    }
    case id_zmk_rgblight_effect_speed: {
        rgblight_set_speed_noeeprom(value_data[0]);
        break;
    }
    case id_zmk_rgblight_color: {
        rgblight_sethsv_noeeprom(value_data[0], value_data[1], rgblight_get_val());
        break;
    }
    }
}

void launcher_zmk_rgblight_save(void) { eeconfig_update_rgblight_current(); }

#endif // ZMK_RGBLIGHT_ENABLE

#if defined(RGB_MATRIX_ENABLE)

void launcher_zmk_rgb_matrix_command(uint8_t *data, uint8_t length) {
    // data = [ command_id, channel_id, value_id, value_data ]
    uint8_t *command_id = &(data[0]);
    uint8_t *value_id_and_data = &(data[2]);

    switch (*command_id) {
    case id_custom_set_value: {
        launcher_zmk_rgb_matrix_set_value(value_id_and_data);
        break;
    }
    case id_custom_get_value: {
        launcher_zmk_rgb_matrix_get_value(value_id_and_data);
        break;
    }
    case id_custom_save: {
        launcher_zmk_rgb_matrix_save();
        break;
    }
    default: {
        *command_id = id_unhandled;
        break;
    }
    }
}

void launcher_zmk_rgb_matrix_get_value(uint8_t *data) {
    // data = [ value_id, value_data ]
    uint8_t *value_id = &(data[0]);
    uint8_t *value_data = &(data[1]);

    switch (*value_id) {
    case id_zmk_rgb_matrix_brightness: {
        value_data[0] =
            ((uint16_t)zmk_rgb_matrix_get_val() * UINT8_MAX) / RGB_MATRIX_MAXIMUM_BRIGHTNESS;
        break;
    }
    case id_zmk_rgb_matrix_effect: {
        value_data[0] = zmk_rgb_matrix_is_enabled() ? zmk_rgb_matrix_get_mode() : 0;
        break;
    }
    case id_zmk_rgb_matrix_effect_speed: {
        value_data[0] = zmk_rgb_matrix_get_speed();
        break;
    }
    case id_zmk_rgb_matrix_color: {
        value_data[0] = zmk_rgb_matrix_get_hue();
        value_data[1] = zmk_rgb_matrix_get_sat();
        break;
    }
    }
}
extern uint8_t rgb_onoff_status;
void launcher_zmk_rgb_matrix_set_value(uint8_t *data) {
    // data = [ value_id, value_data ]
    uint8_t *value_id = &(data[0]);
    uint8_t *value_data = &(data[1]);
    switch (*value_id) {
    case id_zmk_rgb_matrix_brightness: {
        zmk_rgb_matrix_sethsv_no_save(zmk_rgb_matrix_get_hue(), zmk_rgb_matrix_get_sat(),
                                      scale8(value_data[0], RGB_MATRIX_MAXIMUM_BRIGHTNESS));
        break;
    }
    case id_zmk_rgb_matrix_effect: {
        if (value_data[0] == 0) {
            // rgb_matrix_disable_noeeprom();
            rgb_matrix_config.enable = 0;
            rgb_onoff_status = rgb_matrix_config.enable;
        } else {
            // rgb_matrix_enable_noeeprom();
            // rgb_matrix_mode_noeeprom(value_data[0]);
            rgb_matrix_config.enable = 1;
            rgb_onoff_status = rgb_matrix_config.enable;
            zmk_rgb_matrix_mode_no_save(value_data[0]);
            // fix during ppt connected when launcher change rgb effect!
            if (rgb_led_indicators.rgb_enable) {
                rgb_led_indicators.rgb_enable = 0;
                rgb_matrix_config.back_mode = rgb_matrix_config.mode;
                LOG_ERR("clear indicator");
            }
            zmk_rgb_matrix_on();
            void raise_active_event(void);
            raise_active_event();
        }
        break;
    }
    case id_zmk_rgb_matrix_effect_speed: {
        // rgb_matrix_set_speed_noeeprom(value_data[0]);
        rgb_matrix_config.speed = value_data[0];
        break;
    }
    case id_zmk_rgb_matrix_color: {
        zmk_rgb_matrix_sethsv_no_save(value_data[0], value_data[1], zmk_rgb_matrix_get_val());
        break;
    }
    }
}

void launcher_zmk_rgb_matrix_save(void) {
    // eeconfig_update_rgb_matrix();
    save_rgb_matrix_config();
}

#endif // RGB_MATRIX_ENABLE

#if defined(LED_MATRIX_ENABLE)

void launcher_zmk_led_matrix_command(uint8_t *data, uint8_t length) {
    // data = [ command_id, channel_id, value_id, value_data ]
    uint8_t *command_id = &(data[0]);
    uint8_t *value_id_and_data = &(data[2]);

    switch (*command_id) {
    case id_custom_set_value: {
        launcher_zmk_led_matrix_set_value(value_id_and_data);
        break;
    }
    case id_custom_get_value: {
        launcher_zmk_led_matrix_get_value(value_id_and_data);
        break;
    }
    case id_custom_save: {
        launcher_zmk_led_matrix_save();
        break;
    }
    default: {
        *command_id = id_unhandled;
        break;
    }
    }
}

void launcher_zmk_led_matrix_get_value(uint8_t *data) {
    // data = [ value_id, value_data ]
    uint8_t *value_id = &(data[0]);
    uint8_t *value_data = &(data[1]);

    switch (*value_id) {
    case id_zmk_led_matrix_brightness: {
        value_data[0] =
            ((uint16_t)led_matrix_get_val() * UINT8_MAX) / LED_MATRIX_MAXIMUM_BRIGHTNESS;
        break;
    }
    case id_zmk_led_matrix_effect: {
        value_data[0] = led_matrix_is_enabled() ? led_matrix_get_mode() : 0;
        break;
    }
    case id_zmk_led_matrix_effect_speed: {
        value_data[0] = led_matrix_get_speed();
        break;
    }
    }
}

void launcher_zmk_led_matrix_set_value(uint8_t *data) {
    // data = [ value_id, value_data ]
    uint8_t *value_id = &(data[0]);
    uint8_t *value_data = &(data[1]);
    switch (*value_id) {
    case id_zmk_led_matrix_brightness: {
        led_matrix_set_val_noeeprom(scale8(value_data[0], LED_MATRIX_MAXIMUM_BRIGHTNESS));
        break;
    }
    case id_zmk_led_matrix_effect: {
        if (value_data[0] == 0) {
            led_matrix_disable_noeeprom();
        } else {
            led_matrix_enable_noeeprom();
            led_matrix_mode_noeeprom(value_data[0]);
        }
        break;
    }
    case id_zmk_led_matrix_effect_speed: {
        led_matrix_set_speed_noeeprom(value_data[0]);
        break;
    }
    }
}

void launcher_zmk_led_matrix_save(void) { eeconfig_update_led_matrix(); }

#endif // LED_MATRIX_ENABLE

#if defined(AUDIO_ENABLE)

extern audio_config_t audio_config;

void launcher_zmk_audio_command(uint8_t *data, uint8_t length) {
    // data = [ command_id, channel_id, value_id, value_data ]
    uint8_t *command_id = &(data[0]);
    uint8_t *value_id_and_data = &(data[2]);

    switch (*command_id) {
    case id_custom_set_value: {
        launcher_zmk_audio_set_value(value_id_and_data);
        break;
    }
    case id_custom_get_value: {
        launcher_zmk_audio_get_value(value_id_and_data);
        break;
    }
    case id_custom_save: {
        launcher_zmk_audio_save();
        break;
    }
    default: {
        *command_id = id_unhandled;
        break;
    }
    }
}

void launcher_zmk_audio_get_value(uint8_t *data) {
    // data = [ value_id, value_data ]
    uint8_t *value_id = &(data[0]);
    uint8_t *value_data = &(data[1]);
    switch (*value_id) {
    case id_zmk_audio_enable: {
        value_data[0] = audio_config.enable ? 1 : 0;
        break;
    }
    case id_zmk_audio_clicky_enable: {
        value_data[0] = audio_config.clicky_enable ? 1 : 0;
        break;
    }
    }
}

void launcher_zmk_audio_set_value(uint8_t *data) {
    // data = [ value_id, value_data ]
    uint8_t *value_id = &(data[0]);
    uint8_t *value_data = &(data[1]);
    switch (*value_id) {
    case id_zmk_audio_enable: {
        audio_config.enable = value_data[0] ? 1 : 0;
        break;
    }
    case id_zmk_audio_clicky_enable: {
        audio_config.clicky_enable = value_data[0] ? 1 : 0;
        break;
    }
    }
}

void launcher_zmk_audio_save(void) { eeconfig_update_audio(audio_config.raw); }

#endif // ZMK_AUDIO_ENABLE

void factory_test_send(uint8_t *payload, uint8_t length) {

    uint16_t checksum = 0;
    uint8_t data[RAW_EPSIZE] = {0};

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
    FACTORY_TEST_CMD_GET_DEVICE_ID,
    FACTORY_TEST_CMD_GET_HALL_WAKE_STATE,
    FACTORY_TEST_CMD_IO_TEST_START, // 0x0b no param,return :result ,port0 ... port10 skip
                                    // pins(bit=1 skip)
    FACTORY_TEST_CMD_IO_TEST_SET,  // 0X0c ,byte1=port(0-10),byte2=value(bit0-bit7=0/1)
    FACTORY_TEST_CMD_IO_TEST_READ, // 0X0d ,result ,port(0-10),value(bit0-bit7=0/1)
};
enum {
    OS_SWITCH = 0x01,
};
void zmk_rgb_test_handle_cmd(uint8_t cmd);
const uint8_t skip_pins[] = {
    0b10001000, // P0_3,P0_7
    0b01011011, // P1_3,P1_4,P1_6, //p1_1,p1_0 mode sel
    0b00001011, // p2_0,P2_1, //p2_3 usb det
    0b00000000, //
    0b11110000, // P4_4,P4_5,P4_6,P4_7,
    0b11111111, // P5_0,P5_1,P5_2,P5_3,P5_4,P5_5,P5_6,P5_7,
    0b01000001, // P6_0,P6_6,
    0b11111111, // P7_0,P7_1,P7_2,P7_3,P7_4,P7_5,P7_6,P7_7
    0b11111000, // DACP,DACN,
    0b00000100, // P9_2
    0b11111110,
};
// #include "rtl_aon_qdec.h"
static uint8_t io_test_inited;
void io_test_start(void) {
    void swd_pin_disable(void);
    void stop_battery_timer(void);
    swd_pin_disable();
#if defined(RGB_MATRIX_ENABLE)
    zmk_rgb_matrix_off();
#endif
    stop_battery_timer();
    // AON_QDEC_Cmd(AON_QDEC, AON_QDEC_AXIS_X, DISABLE);
    LOG_ERR("io_test_start");
    k_msleep(20);

    // 1. 禁止 kscan
    const struct device *kscan_dev = DEVICE_DT_GET(ZMK_MATRIX_NODE_ID);
    if (kscan_dev != NULL && device_is_ready(kscan_dev)) {
        kscan_disable_callback(kscan_dev);
        DBG_DIRECT("kscan disabled");
        LOG_ERR("kscan disabled");
    }
    RCC_PeriphClockCmd(APBPeriph_GPIOA, APBPeriph_GPIOA_CLOCK, ENABLE);
    RCC_PeriphClockCmd(APBPeriph_GPIOB, APBPeriph_GPIOB_CLOCK, ENABLE);
    DBG_DIRECT("io_test_start");
    io_test_inited = 1;
}

void io_test_set(uint8_t port, uint8_t value) {
    if (port > 10)
        return;
    GPIO_InitTypeDef gpio_init_struct;
    GPIO_StructInit(&gpio_init_struct);
    if (port == 6) {
        // disconnect P9
        for (int i = P9_0; i <= P9_7; i++) {
            Pinmux_Deinit(i);
            Pad_Config(i, PAD_SW_MODE, PAD_NOT_PWRON, PAD_PULL_NONE, PAD_OUT_DISABLE, PAD_OUT_HIGH);
        }
    } else if (port == 9) {
        // disconnect P6
        for (int i = P6_0; i <= P6_7; i++) {
            Pinmux_Deinit(i);
            Pad_Config(i, PAD_SW_MODE, PAD_NOT_PWRON, PAD_PULL_NONE, PAD_OUT_DISABLE, PAD_OUT_HIGH);
        }
    }
    uint8_t pin = 0;
    for (uint8_t i = 0; i < 8; i++) {
        pin = port * 8 + i;
        if (skip_pins[port] & (1 << i))
            continue;
        // LOG_ERR("set pin %d",pin);
        gpio_init_struct.GPIO_Pin = GPIO_GetPin(pin);
        gpio_init_struct.GPIO_Mode = GPIO_Mode_OUT;
        gpio_init_struct.GPIO_ITCmd = DISABLE;
        GPIO_Init(GPIO_GetPort(pin), &gpio_init_struct);
        // 设置 pinmux 为 GPIO 模式
        Pad_Config(pin, PAD_PINMUX_MODE, PAD_IS_PWRON, value ? PAD_PULL_UP : PAD_PULL_DOWN,
                   PAD_OUT_ENABLE, value ? PAD_OUT_HIGH : PAD_OUT_LOW);
        Pinmux_Config(pin, DWGPIO);

        GPIO_WriteBit(GPIO_GetPort(pin), GPIO_GetPin(pin),
                      (value & (1 << i)) ? Bit_SET : Bit_RESET);
    }
}
uint8_t io_test_read(uint8_t port) {
    if (port > 10)
        return -1;
    uint8_t value = 0;
    GPIO_InitTypeDef gpio_init_struct;
    GPIO_StructInit(&gpio_init_struct);
    uint8_t pin = 0;
    for (uint8_t i = 0; i < 8; i++) {
        pin = port * 8 + i;
        if (skip_pins[port] & (1 << i))
            continue;

        gpio_init_struct.GPIO_Pin = GPIO_GetPin(pin);
        gpio_init_struct.GPIO_Mode = GPIO_Mode_IN;
        gpio_init_struct.GPIO_ITCmd = DISABLE;
        GPIO_Init(GPIO_GetPort(pin), &gpio_init_struct);

        Pinmux_Config(pin, DWGPIO);
        Pad_Config(pin, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_DISABLE,
                   PAD_OUT_HIGH);

        uint8_t level = GPIO_ReadInputDataBit(GPIO_GetPort(pin), GPIO_GetPin(pin));
        value |= (level ? 1 : 0) << i;
    }
    return value;
}

void factory_test_rx(uint8_t *data, uint8_t length) {
    if (data[0] == 0xAB) {
        uint16_t checksum = 0;

        for (uint8_t i = 1; i < RAW_EPSIZE - 3; i++) {
            checksum += data[i];
        }
        /* Verify checksum */
        if ((checksum & 0xFF) != data[RAW_EPSIZE - 2] || checksum >> 8 != data[RAW_EPSIZE - 1])
            return;

        switch (data[1]) {
#ifdef CONFIG_LED_STRIP
        case FACTORY_TEST_CMD_BACKLIGHT:
            zmk_rgb_test_handle_cmd(data[2]);
            break;
#endif
        case FACTORY_TEST_CMD_OS_SWITCH:
            report_os_sw_state = data[2];
            if (report_os_sw_state) {
                // dip_switch_read(true);
            }
            break;

        case FACTORY_TEST_CMD_RADIO_CARRIER:
            void single_tone_test_start(uint8_t ch);
            single_tone_test_start(data[2]);
            break;
        case FACTORY_TEST_CMD_GET_DEVICE_ID: {
            uint8_t payload[16] = {0};
            uint8_t len = 0;
            payload[len++] = FACTORY_TEST_CMD_GET_DEVICE_ID;
            payload[len++] = 14; // UUID length
            uint8_t *get_ic_euid(void);
            uint8_t *p = get_ic_euid();

            if (p != NULL)
                memcpy(&payload[len], p, 14);
            len += 14;
            factory_test_send(payload, len);
        } break;
        case FACTORY_TEST_CMD_IO_TEST_START: {
            io_test_start();
            uint8_t buf[32] = {0};
            buf[0] = FACTORY_TEST_CMD_IO_TEST_START;
            buf[1] = 0; // success
            memcpy(buf + 2, &skip_pins, sizeof(skip_pins));
            factory_test_send(buf, 2 + sizeof(skip_pins));
        } break;
        case FACTORY_TEST_CMD_IO_TEST_SET: {
            uint8_t port = data[2];
            uint8_t value = data[3];
            LOG_ERR("io_test_set,port:%02x,value:%02x", port, value);
            uint8_t result = 0;
            if (io_test_inited)
                io_test_set(port, value);
            else
                result = 1; // fail

            uint8_t buf[2] = {0};
            buf[0] = FACTORY_TEST_CMD_IO_TEST_SET;
            buf[1] = result;
            factory_test_send(buf, 2);
        } break;

        case FACTORY_TEST_CMD_IO_TEST_READ: {
            uint8_t port = data[2];

            LOG_ERR("io_test_read,port:%d", port);
            uint8_t result = 0;
            uint8_t value = 0;
            if (io_test_inited) {
                value = io_test_read(port);
            } else
                result = 1; // fail
            uint8_t buf[4] = {0};
            buf[0] = FACTORY_TEST_CMD_IO_TEST_READ;
            buf[1] = result;
            buf[2] = port;
            buf[3] = value;
            factory_test_send(buf, sizeof(buf));
        } break;
        }
    }
}

bool dip_switch_update_user(uint8_t index, bool active) {
    LOG_ERR("index:%d,active:%d", index, active);
    if (report_os_sw_state) {

        uint8_t payload[3] = {FACTORY_TEST_CMD_OS_SWITCH, OS_SWITCH, active};
        factory_test_send(payload, 3);
    }

    return true;
}
#if 0 // CONFIG_LAUNCHER_USER_SET_DEBOUNCE
void launcher_read_debounce(void)  //no use!
{
    int rc;
    rc = load_immediate_value("launcher/debounce", &debounce, sizeof(debounce));
    if (rc == -ENOENT) {

        LOG_DBG("launcher/debounce,(default)\n");
        debounce.scan_period_ms = DT_PROP(DT_NODELABEL(kscan0), debounce_scan_period_ms);
        debounce.debounce_press_ms = DT_PROP(DT_NODELABEL(kscan0), debounce_press_ms);
        debounce.debounce_release_ms = DT_PROP(DT_NODELABEL(kscan0), debounce_release_ms);
    } else if (rc == 0) {
        user_set_debounce(debounce.debounce_press_ms,debounce.debounce_release_ms);
    }
    LOG_ERR("launcher/debounce:%02x%02x%02x\n",debounce.scan_period_ms,debounce.debounce_press_ms,debounce.debounce_release_ms );

}
#endif
void launcher_update_debounce(void) {
    int rc;
    rc = settings_save_one("launcher/debounce", (const void *)&debounce, sizeof(debounce));
    if (rc) {
        LOG_DBG("write failed:%d", rc);
    } else {
        LOG_DBG("OK.\n");
    }
}

enum {
    kc_user_cmd_protocol = 0x01,
    kc_user_cmd_dfu_info = 0x02,
    kc_user_cmd_kb_lang = 0x03,
    kc_user_cmd_set_kb_lang = 0x04,
    kc_user_cmd_debounce = 0x05,
    kc_user_cmd_set_debounce = 0x06,
    kc_user_cmd_snap_click_get_info, // SNAP_CLICK_GET_INFO,
    kc_user_cmd_snap_click_get,      // SNAP_CLICK_GET,
    kc_user_cmd_snap_click_set,      // SNAP_CLICK_SET,
    kc_user_cmd_snap_click_save,     // SNAP_CLICK_SAVE,
    kc_user_cmd_sleep_time = 0x0b,
    kc_user_cmd_set_sleep_time = 0x0c,
    kc_user_cmd_report_rate = 0x0d,
    kc_user_cmd_set_report_rate = 0x0e,
    kc_user_cmd_battery = 0x31,
};
/*
write : cmd data
ACK   : cmd result  data
result =0 suc ,1 fail;
*/
void kc_user_cmd(uint8_t *command_data) {
    uint8_t cmd = command_data[0];
    LOG_ERR("kc cmd:%x", cmd);
    switch (cmd) {
    case kc_user_cmd_protocol:
        command_data[1] = 0;
#if CONFIG_ADD_PPT_REPORT_RATE
        command_data[2] = 3; // version =0x0001-->0x0003
#else
        command_data[2] = 1;
#endif
        command_data[3] = 0;
        break;
#if 1
    case kc_user_cmd_debounce:
        command_data[1] = 0; // result
        command_data[2] = 1; // solution
        command_data[3] = debounce.debounce_press_ms;
        command_data[4] = debounce.debounce_release_ms;
        LOG_ERR("kc read debounce[%d,%d]", debounce.debounce_press_ms,
                debounce.debounce_release_ms);
        break;
    case kc_user_cmd_set_debounce:

        if (command_data[1] > 50 || (command_data[2] > 50 || command_data[2] == 0)) {
            command_data[1] = 1;
            command_data[2] = 0;
        } else {
            debounce.debounce_press_ms = command_data[1];
            debounce.debounce_release_ms = command_data[2];
            user_set_debounce(debounce.debounce_press_ms, debounce.debounce_release_ms);
            LOG_ERR("kc set debounce[%d,%d]", debounce.debounce_press_ms,
                    debounce.debounce_release_ms);
            launcher_save_debounce();
            command_data[1] = 0;
            command_data[2] = 0;
        }
        break;
#endif
    case kc_user_cmd_report_rate:
#if !(CONFIG_ADD_PPT_REPORT_RATE)
        command_data[1] = 0;
        command_data[2] = debounce.div;
        command_data[3] = 0x7f;
#else
        uint8_t index = 1;
        command_data[index++] = 0;
#if defined(CONFIG_SHIELD_KEYCHRON_K3SE2_ANSI) || defined(CONFIG_SHIELD_KEYCHRON_K5SE2_ANSI)
        command_data[index++] = 1000 & 0xff;  // max  rate
        command_data[index++] = 1000 >> 8;    // max  rate
        command_data[index++] = 0x78;         // capability
        command_data[index++] = debounce.div; // div
        command_data[index++] = 0;            // read only =false;
        command_data[index++] = 1000 & 0xff;
        command_data[index++] = 1000 >> 8;
        command_data[index++] = 0x78;
        command_data[index++] = debounce.ppt_div;
        command_data[index++] = 0;
#else
        command_data[index++] = 8000 & 0xff;  // max  rate
        command_data[index++] = 8000 >> 8;    // max  rate
        command_data[index++] = 0x7f;         // capability
        command_data[index++] = debounce.div; // div
        command_data[index++] = 0;            // read only =false;
        command_data[index++] = 8000 & 0xff;
        command_data[index++] = 8000 >> 8;
        command_data[index++] = 0x7f;
        command_data[index++] = debounce.ppt_div;
        command_data[index++] = 0; // read only =false;
#endif
        command_data[index++] = 125;
        command_data[index++] = 0;
        command_data[index++] = 0x40;
        command_data[index++] = 0x40;
        command_data[index++] = 1; // read only =true;
#endif
        LOG_ERR("kc read report rate:%d", debounce.div);
        break;
    case kc_user_cmd_set_report_rate:
        if (command_data[1] <= 6 && command_data[2] <= 6 && command_data[3] <= 6) {
            debounce.div = command_data[1];
#if CONFIG_ADD_PPT_REPORT_RATE
            if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_USB) {
                set_report_rate(debounce.div);
                LOG_ERR("kc set usb report rate:%d", debounce.div);
            }
            debounce.ppt_div = command_data[2];
            if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_PPT) {
                set_report_rate(debounce.ppt_div);
                LOG_ERR("kc set ppt report rate:%d", debounce.div);
            }
#endif
            command_data[1] = 0;
            launcher_save_debounce();
        } else
            command_data[1] = 1;
        break;
#ifdef CONFIG_SNAP_CLICK_ENABLE
    case kc_user_cmd_snap_click_get_info ... kc_user_cmd_snap_click_save:
        void snap_click_rx(uint8_t * data, uint8_t length);
        snap_click_rx(command_data, 0);
        break;
#endif
    case kc_user_cmd_sleep_time:

    {
        uint16_t idle_time = 0;
        uint16_t sleep_time = 0;
        get_lpm_set(&idle_time, &sleep_time);
        command_data[1] = 0;
        command_data[2] = idle_time & 0xff;
        command_data[3] = idle_time >> 8;
        command_data[4] = sleep_time & 0xff;
        command_data[5] = sleep_time >> 8;

    } break;
    case kc_user_cmd_set_sleep_time: {
        uint16_t idle_time = 0;
        uint16_t sleep_time = 0;
        idle_time = command_data[1] | command_data[2] << 8;
        sleep_time = command_data[3] | command_data[4] << 8;
        LOG_ERR("idle time:%d,sleep:%d", idle_time, sleep_time);
        if (idle_time < 5 || sleep_time < 60) {
            command_data[1] = 1;
        } else {
            command_data[1] = 0;
            set_lpm_set(idle_time, sleep_time);
        }
    } break;
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    // Reply: A7 31 <percent> <charging>, over USB and over the 2.4 GHz receiver.
    case kc_user_cmd_battery: {
        uint8_t zmk_battery_state_of_charge(void);
        uint8_t get_charge_state(void);
        command_data[1] = zmk_battery_state_of_charge();
        command_data[2] = get_charge_state();
    } break;
#endif
    }
}
uint8_t get_report_rate_div(void) { return debounce.div; }
void set_report_rate_div(uint8_t div) {
    if (div > 6)
        return;
#if CONFIG_ADD_PPT_REPORT_RATE
    if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_USB)
        debounce.div = div;
    else if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_PPT)
        debounce.ppt_div = div;
#else
    debounce.div = div;
#endif
}
void save_mac_win_layer(uint8_t win) {
    debounce.mac_win_layer = win;
    launcher_save_debounce();
}
uint8_t get_mac_win_layer(void) {
    LOG_ERR("is win layer:%d", debounce.mac_win_layer);
    return debounce.mac_win_layer;
}
void update_launcher_report_rate(void) {
    uint8_t command_data[RAW_EPSIZE] = {0};
    uint8_t index = 0;
    command_data[index++] = 0xa7;
    command_data[index++] = 0x0d;
    command_data[index++] = 0;
#if CONFIG_ADD_PPT_REPORT_RATE
#if defined(CONFIG_SHIELD_KEYCHRON_K3SE2_ANSI) || defined(CONFIG_SHIELD_KEYCHRON_K5SE2_ANSI)
    command_data[index++] = 1000 & 0xff;  // max  rate
    command_data[index++] = 1000 >> 8;    // max  rate
    command_data[index++] = 0x78;         // capability
    command_data[index++] = debounce.div; // div
    command_data[index++] = 0;            // read only =false;
    command_data[index++] = 1000 & 0xff;
    command_data[index++] = 1000 >> 8;
    command_data[index++] = 0x78;
    command_data[index++] = debounce.ppt_div;
    command_data[index++] = 0; // read only =false;
#else
    command_data[index++] = 8000 & 0xff;  // max  rate
    command_data[index++] = 8000 >> 8;    // max  rate
    command_data[index++] = 0x7f;         // capability
    command_data[index++] = debounce.div; // div
    command_data[index++] = 0;            // read only =false;
    command_data[index++] = 8000 & 0xff;
    command_data[index++] = 8000 >> 8;
    command_data[index++] = 0x7f;
    command_data[index++] = debounce.ppt_div;
    command_data[index++] = 0; // read only =false;
#endif
    command_data[index++] = 125;
    command_data[index++] = 0;
    command_data[index++] = 0x40;
    command_data[index++] = 0x40;
    command_data[index++] = 1; // read only =true;
#else
    command_data[index++] = debounce.div;
    command_data[index++] = 0x7f;
#endif

    raw_hid_send(command_data, RAW_EPSIZE);
}

void set_usb_report_rate(void) { set_report_rate(debounce.div); }
void set_ppt_report_rate(void) {
#if CONFIG_ADD_PPT_REPORT_RATE
    set_report_rate(debounce.ppt_div);
#else
    set_report_rate(debounce.div);
#endif
}