/*
 * Copyright (c) 2020-2021 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "kscan_gpio.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/kscan.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/pinctrl.h>
#include <zmk/debounce.h>
#include <zephyr/pm/device.h>
#include "rtl_gpio.h"
#include "trace.h"

LOG_MODULE_DECLARE(zmk, 4); // CONFIG_ZMK_LOG_LEVEL);

#define RTK_GPIO_FAST 1
void swd_pin_disable(void);
/* GPIO buses definitions */

struct gpio_rtl87x2g_irq_info {
    const struct device *irq_dev;
    uint8_t num_irq;
    struct gpio_irq_info {
        uint32_t irq;
        uint32_t priority;
    } gpio_irqs[];
};

/**
 * @brief configuration of GPIO device
 */
struct gpio_rtl87x2g_config {
    struct gpio_driver_config common;
    uint16_t clkid;
    uint8_t port_num;
    GPIO_TypeDef *port_base;
    struct gpio_rtl87x2g_irq_info *irq_info;
};
#if defined(CONFIG_COL_595_OUTPUT)
const struct gpio_dt_spec hc595_shift_clk = GPIO_DT_SPEC_GET(DT_NODELABEL(clk_595), gpios);
const struct gpio_dt_spec hc595_data = GPIO_DT_SPEC_GET(DT_NODELABEL(data_595), gpios);
const struct gpio_dt_spec hc595_latch_clk = GPIO_DT_SPEC_GET(DT_NODELABEL(rclk_595), gpios);

#if defined(CONFIG_COL_595_AND_GPIOS_OUTPUT)
const struct gpio_dt_spec c19_gpio = GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(kscan0), col_gpios, 19);
const struct gpio_dt_spec c18_gpio = GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(kscan0), col_gpios, 18);
const struct gpio_dt_spec c17_gpio = GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(kscan0), col_gpios, 17);
const struct gpio_dt_spec c16_gpio = GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(kscan0), col_gpios, 16);
#endif

#define HC595_SHIFT_CLK_LOW (GPIOA->GPIO_DR &= ~BIT(6))
#define HC595_SHIFT_CLK_HIGH (GPIOA->GPIO_DR |= BIT(6))
#define HC595_DATA_LOW (GPIOB->GPIO_DR &= ~BIT(21))
#define HC595_DATA_HIGH (GPIOB->GPIO_DR |= BIT(21))
#define HC595_LATCH_CLK_LOW (GPIOA->GPIO_DR &= ~BIT(5))
#define HC595_LATCH_CLK_HIGH (GPIOA->GPIO_DR |= BIT(5))
#endif
// static int gpio_rtl87x2g_gpio2pad(uint8_t port_num, uint32_t pin)
// {
//  /* There is no reuse situation for gpioa */
//  if (port_num == 0) {
//      if (pin < 16) {
//          return pin;
//      } else if (pin >= 21 && pin < 32) {
//          return pin - 5;
//      } else if (pin >= 16 && pin < 21) {
//          return pin + 48;
//      }
//  }
//  /* Handle reuse situation for gpiob */
//  else if (port_num == 1) {
//      if (pin < 19) {
//          return pin + 27;
//      }
// #if CONFIG_SOC_RTL8777G
//      return pin + 51;
// #elif CONFIG_SOC_RTL8762GKU || CONFIG_SOC_RTL8762GKH || CONFIG_SOC_RTL8762GTP
//      if (pin < 21) {
//          return pin + 27;
//      } else {
//          return pin + 51;
//      }
// #elif CONFIG_SOC_RTL8762GTU || CONFIG_SOC_RTL8762GTH
//      if (pin == 20) {
//          return 49;
//      } else if (pin == 21) {
// #if CONFIG_RTL87X2G_USE_P6_2_AS_GPIOB21
//          return 50;
// #else
//          return 72;
// #endif
//      } else if (pin == 22) {
// #if CONFIG_RTL87X2G_USE_P6_3_AS_GPIOB22
//          return 51;
// #else
//          return 73;
// #endif
//      } else if (pin == 23) {
//          return 52;
//      } else if (pin == 24) {
// #if CONFIG_RTL87X2G_USE_P6_5_AS_GPIOB24
//          return 53;
// #else
//          return 75;
// #endif
//      } else if (pin == 25) {
//          return 76;
//      } else if (pin == 26) {
// #if CONFIG_RTL87X2G_USE_P6_7_AS_GPIOB26
//          return 55;
// #else
//          return 77;
// #endif
//      } else if (pin < 30) {
//          return pin + 51;
//      }

// #elif CONFIG_SOC_RTL8772GWP
//      if (pin >= 21) {
//          return pin + 51;
//      }
// #elif CONFIG_SOC_RTL8772GWF
//      if (pin >= 21 && pin < 24) {
//          return pin + 51;
//      } else if (pin >= 27 && pin < 32) {
//          return pin + 29;
//      }
// #endif
//  }

//  return -EIO;
// }
void toggle_debug_pin(void);
// static void toggle_debug_pin(void) {}

#define DT_DRV_COMPAT zmk_kscan_gpio_matrix

#define INST_DIODE_DIR(n) DT_ENUM_IDX(DT_DRV_INST(n), diode_direction)
#define COND_DIODE_DIR(n, row2col_code, col2row_code)                                              \
    COND_CODE_0(INST_DIODE_DIR(n), row2col_code, col2row_code)

#define INST_ROWS_LEN(n) DT_INST_PROP_LEN(n, row_gpios)
#define INST_COLS_LEN(n) DT_INST_PROP_LEN(n, col_gpios)
#define INST_MATRIX_LEN(n) (INST_ROWS_LEN(n) * INST_COLS_LEN(n))
#define INST_INPUTS_LEN(n) COND_DIODE_DIR(n, (INST_COLS_LEN(n)), (INST_ROWS_LEN(n)))

#if CONFIG_ZMK_KSCAN_DEBOUNCE_PRESS_MS >= 0
#define INST_DEBOUNCE_PRESS_MS(n) CONFIG_ZMK_KSCAN_DEBOUNCE_PRESS_MS
#else
#define INST_DEBOUNCE_PRESS_MS(n)                                                                  \
    DT_INST_PROP_OR(n, debounce_period, DT_INST_PROP(n, debounce_press_ms))
#endif

#if CONFIG_ZMK_KSCAN_DEBOUNCE_RELEASE_MS >= 0
#define INST_DEBOUNCE_RELEASE_MS(n) CONFIG_ZMK_KSCAN_DEBOUNCE_RELEASE_MS
#else
#define INST_DEBOUNCE_RELEASE_MS(n)                                                                \
    DT_INST_PROP_OR(n, debounce_period, DT_INST_PROP(n, debounce_release_ms))
#endif

#define USE_POLLING IS_ENABLED(CONFIG_ZMK_KSCAN_MATRIX_POLLING)
#define USE_INTERRUPTS (!USE_POLLING)

#define COND_INTERRUPTS(code) COND_CODE_1(CONFIG_ZMK_KSCAN_MATRIX_POLLING, (), code)
#define COND_POLL_OR_INTERRUPTS(pollcode, intcode)                                                 \
    COND_CODE_1(CONFIG_ZMK_KSCAN_MATRIX_POLLING, pollcode, intcode)

#define KSCAN_GPIO_ROW_CFG_INIT(idx, inst_idx)                                                     \
    KSCAN_GPIO_GET_BY_IDX(DT_DRV_INST(inst_idx), row_gpios, idx)
#define KSCAN_GPIO_COL_CFG_INIT(idx, inst_idx)                                                     \
    KSCAN_GPIO_GET_BY_IDX(DT_DRV_INST(inst_idx), col_gpios, idx)

enum kscan_diode_direction {
    KSCAN_ROW2COL,
    KSCAN_COL2ROW,
};

struct kscan_matrix_irq_callback {
    const struct device *dev;
    struct gpio_callback callback;
};

struct kscan_matrix_data {
    const struct device *dev;
    struct kscan_gpio_list inputs;
    struct kscan_gpio_list outputs;
    kscan_callback_t callback;
    struct k_work_delayable work;
#if USE_INTERRUPTS
    /** Array of length config->inputs.len */
    struct kscan_matrix_irq_callback *irqs;
#endif
    /** Timestamp of the current or scheduled scan. */
    int64_t scan_time;
    /**
     * Current state of the matrix as a flattened 2D array of length
     * (config->rows * config->cols)
     */
    struct zmk_debounce_state *matrix_state;
    uint32_t press_rows;
};

struct kscan_matrix_config {
    struct kscan_gpio_list outputs;
    struct zmk_debounce_config debounce_config;
    size_t rows;
    size_t cols;
    int32_t debounce_scan_period_ms;
    int32_t poll_period_ms;
    enum kscan_diode_direction diode_direction;
    const struct pinctrl_dev_config *pcfg;
};

extern bool kscan_enabled_dlps;
#if CONFIG_FAST_KSCAN
const struct device *const kscan_dev = DEVICE_DT_GET(DT_NODELABEL(kscan0));
#include <zephyr/drivers/counter.h>
#define TIMER DT_NODELABEL(timer3)
uint8_t get_report_rate(void);
bool is_ble(void);
extern uint8_t macro_running;

static uint16_t scan_idle_count = 0;
static const struct device *const counter_dev = DEVICE_DT_GET(TIMER);
static void rtk_counter_start(uint32_t usec);
#endif

#if CONFIG_LAUNCHER_USER_SET_DEBOUNCE

struct user_debounce_config {
    struct zmk_debounce_config debounce_config;
    uint32_t debounce_scan_period_ms;
    uint32_t default_scan_period;
};
static struct user_debounce_config user_debounce;
void user_set_debounce(uint32_t debounce_press_ms, uint32_t debounce_release_ms) {
    user_debounce.debounce_config.debounce_press_ms = debounce_press_ms;
    user_debounce.debounce_config.debounce_release_ms = debounce_release_ms;
    LOG_DBG("set debounce,scan:%d,press:%d,release:%d", user_debounce.debounce_scan_period_ms,
            debounce_press_ms, debounce_release_ms);
}
void user_set_scan_period(uint32_t period) { user_debounce.debounce_scan_period_ms = period; }
uint32_t get_default_scan_period(void) { return user_debounce.default_scan_period; }
#endif
/**
 * Get the index into a matrix state array from a row and column.
 */
static int state_index_rc(const struct kscan_matrix_config *config, const int row, const int col) {
    __ASSERT(row < config->rows, "Invalid row %i", row);
    __ASSERT(col < config->cols, "Invalid column %i", col);

    return (col * config->rows) + row;
}

/**
 * Get the index into a matrix state array from input/output pin indices.
 */
static int state_index_io(const struct kscan_matrix_config *config, const int input_idx,
                          const int output_idx) {
    return (config->diode_direction == KSCAN_ROW2COL)
               ? state_index_rc(config, output_idx, input_idx)
               : state_index_rc(config, input_idx, output_idx);
}

static int kscan_matrix_set_all_outputs(const struct device *dev, const int value) {
#if (!RTK_GPIO_FAST)
    const struct kscan_matrix_config *config = dev->config;
    for (int i = 0; i < config->outputs.len; i++) {

        const struct gpio_dt_spec *gpio = &config->outputs.gpios[i].spec;
        // add
        gpio_pin_configure_dt(gpio, GPIO_OUTPUT);

        int err = gpio_pin_set_dt(gpio, value);
        if (err) {
            LOG_ERR("Failed to set output %i to %i: %i", i, value, err);
            return err;
        }
    }
#else
#if !(defined(CONFIG_COL_595_OUTPUT))
    struct kscan_matrix_data *data = dev->data;

    for (int i = 0; i < data->outputs.len; i++) {
        struct kscan_gpio *gpio = &data->outputs.gpios[i];
        gpio->GPIOx->GPIO_DDR |= BIT(gpio->spec.pin); // set output;
        if (value)
            gpio->GPIOx->GPIO_DR |= BIT(gpio->spec.pin); // set 1
        else
            gpio->GPIOx->GPIO_DR &= ~BIT(gpio->spec.pin); // set 0
    }

#else
    LOG_ERR("set output:%d", value);
#if defined(CONFIG_COL_595_AND_GPIOS_OUTPUT)
    gpio_pin_set_dt(&c19_gpio, value ? 0 : 1);
    gpio_pin_set_dt(&c18_gpio, value ? 0 : 1);
    gpio_pin_set_dt(&c17_gpio, value ? 0 : 1);
    gpio_pin_set_dt(&c16_gpio, value ? 0 : 1);
#endif
    value ? HC595_DATA_LOW : HC595_DATA_HIGH;

    HC595_LATCH_CLK_LOW;
    HC595_SHIFT_CLK_LOW;
    for (int i = 0; i < 16; i++) {
        HC595_SHIFT_CLK_HIGH;
        HC595_SHIFT_CLK_LOW;
    }
    HC595_LATCH_CLK_HIGH;
    HC595_DATA_LOW;
    HC595_LATCH_CLK_LOW;
#endif
#endif
    return 0;
}

#if USE_INTERRUPTS
static int kscan_matrix_interrupt_configure(const struct device *dev, const gpio_flags_t flags) {
    const struct kscan_matrix_data *data = dev->data;

    for (int i = 0; i < data->inputs.len; i++) {
        const struct gpio_dt_spec *gpio = &data->inputs.gpios[i].spec;

        int err = gpio_pin_interrupt_configure_dt(gpio, flags);
        if (err) {
            LOG_ERR("Unable to configure interrupt for pin %u on %s,err:%d", gpio->pin,
                    gpio->port->name, err);
            return err;
        }
    }

    return 0;
}
#endif

#if USE_INTERRUPTS
static int kscan_matrix_interrupt_enable(const struct device *dev) {
    int err = kscan_matrix_interrupt_configure(dev, GPIO_INT_LEVEL_ACTIVE);
    if (err) {
        return err;
    }

    // While interrupts are enabled, set all outputs active so a pressed key
    // will trigger an interrupt.
    return kscan_matrix_set_all_outputs(dev, 1);
}
#endif

#if USE_INTERRUPTS
static int kscan_matrix_interrupt_disable(const struct device *dev) {
    // LOG_DBG("int disable");
    int err =
        kscan_matrix_interrupt_configure(dev, GPIO_INT_MODE_DISABLE_ONLY); // GPIO_INT_DISABLE); //
    if (err) {
        return err;
    }

    // While interrupts are disabled, set all outputs inactive so
    // kscan_matrix_read() can scan them one by one.
    return kscan_matrix_set_all_outputs(dev, 0);
}
#endif

#if USE_INTERRUPTS
static void kscan_matrix_irq_callback_handler(const struct device *port, struct gpio_callback *cb,
                                              const gpio_port_pins_t pin) {
    struct kscan_matrix_irq_callback *irq_data =
        CONTAINER_OF(cb, struct kscan_matrix_irq_callback, callback);
    if (irq_data->dev == NULL)
        return;
    struct kscan_matrix_data *data = irq_data->dev->data;
    // is_app_enabled_dlps=true;
    toggle_debug_pin();
    // DBG_DIRECT("int,pin:%x", pin);
    LOG_ERR("int,pin:%x", pin);
    // Disable our interrupts temporarily to avoid re-entry while we scan.
    kscan_matrix_interrupt_disable(data->dev);

    data->scan_time = k_uptime_get();

    k_work_reschedule(&data->work, K_NO_WAIT);
}
#endif

static void kscan_matrix_read_continue(const struct device *dev) {
    struct kscan_matrix_data *data = dev->data;
#if CONFIG_LAUNCHER_USER_SET_DEBOUNCE
    data->scan_time += user_debounce.debounce_scan_period_ms;
#else
    const struct kscan_matrix_config *config = dev->config;
    data->scan_time += config->debounce_scan_period_ms;
#endif
    // LOG_DBG("read continue,period:%d",config->debounce_scan_period_ms);
    k_work_reschedule(&data->work, K_TIMEOUT_ABS_MS(data->scan_time));
}

static void kscan_matrix_read_end(const struct device *dev) {
    LOG_DBG("read end");
#if USE_INTERRUPTS
    // Return to waiting for an interrupt.
    kscan_matrix_interrupt_enable(dev);
#else
    struct kscan_matrix_data *data = dev->data;
    const struct kscan_matrix_config *config = dev->config;

    data->scan_time += config->poll_period_ms;

    // Return to polling slowly.
    k_work_reschedule(&data->work, K_TIMEOUT_ABS_MS(data->scan_time));
#endif
}
#if RTK_GPIO_FAST
static int kscan_matrix_read(const struct device *dev) {
    struct kscan_matrix_data *data = dev->data;
    const struct kscan_matrix_config *config = dev->config;
    // toggle_debug_pin();
    // is_app_enabled_dlps =true;
#if CONFIG_FAST_KSCAN
    uint32_t elapsed_ms = 0;
    int64_t delta = k_uptime_delta(&data->scan_time);
    if (delta >= 1) {
        data->scan_time = k_uptime_get();
        elapsed_ms = delta;
    }
    if (get_report_rate() >= 8 || is_ble()) //|| macro_running)
    {
        elapsed_ms = user_debounce.debounce_scan_period_ms;
    }
#endif
#if !(defined(CONFIG_COL_595_OUTPUT))
    volatile uint32_t port;
    uint32_t key;
    key = irq_lock();
#if CONFIG_SHIELD_KEYCHRON_V2_ULTRA_ANSI
    GPIOA->GPIO_DDR |= 0xa015;
    GPIOA->GPIO_DR &= ~0xa015;
#elif CONFIG_SHIELD_KEYCHRON_V0_ULTRA_ANSI
    GPIOB->GPIO_DDR |= 0x10001f;
    GPIOB->GPIO_DR &= ~0x10001f;
#elif CONFIG_SHIELD_KEYCHRON_Z270_ANSI
    GPIOA->GPIO_DDR |= 0x8017;
    GPIOA->GPIO_DR &= ~0x8017;
#else
    GPIOA->GPIO_DDR |= 0xa017;
    GPIOA->GPIO_DR &= ~0xa017;
#endif
    irq_unlock(key);
    // note:delay for about 2us;
    for (int i = 0; i < 20; i++) {
        port = data->inputs.gpios[0].GPIOx->GPIO_PAD_STATE;
    }

    for (int col = 0; col < config->outputs.len; col++) {
        const struct kscan_gpio *out_gpio = &data->outputs.gpios[col];
        key = irq_lock();

        out_gpio->GPIOx->GPIO_DR |= BIT(out_gpio->spec.pin); // set 1
        // note: cols are in one gpio !
#if CONFIG_SHIELD_KEYCHRON_RS87_ANSI || CONFIG_SHIELD_KEYCHRON_Q3_ULTRA_ANSI ||                    \
    CONFIG_SHIELD_KEYCHRON_Q3_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_Q3_ULTRA_JIS
        port = (GPIOB->GPIO_PAD_STATE & 0x7fffffff) | (GPIOA->GPIO_PAD_STATE & (1 << 31));
#elif CONFIG_SHIELD_KEYCHRON_RS45_ANSI
        port = (GPIOB->GPIO_PAD_STATE & 0x7ffcffff) |
               (GPIOA->GPIO_PAD_STATE & ((1 << 16) | (1 << 17) | (1 << 31)));
#elif (CONFIG_SHIELD_KEYCHRON_K2_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_K2_ULTRA_ISO ||              \
       CONFIG_SHIELD_KEYCHRON_K2_ULTRA_JIS || CONFIG_SHIELD_KEYCHRON_K2_ULTRA_ANSI_V2)
        port = (GPIOB->GPIO_PAD_STATE & 0xfff2ff7f) |
               ((GPIOA->GPIO_PAD_STATE & ((1 << 16) | (1 << 27))) >> 9);
#elif CONFIG_SHIELD_KEYCHRON_K4_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_K4_ULTRA_ISO ||               \
    CONFIG_SHIELD_KEYCHRON_K4_ULTRA_JIS || CONFIG_SHIELD_KEYCHRON_K4_ULTRA_WB_ISO ||               \
    CONFIG_SHIELD_KEYCHRON_K4_ULTRA_V2_ANSI
        port = (GPIOB->GPIO_PAD_STATE & 0x7ffdfbff) |
               (GPIOA->GPIO_PAD_STATE & ((1 << 10) | (1 << 17) | (1 << 31)));
#elif (CONFIG_SHIELD_KEYCHRON_V5_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_V5_ULTRA_ISO_V2 ||           \
       CONFIG_SHIELD_KEYCHRON_V5_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_V5_ULTRA_JIS ||               \
       CONFIG_SHIELD_KEYCHRON_V5_ULTRA_ANSI_V2)
        port = (GPIOB->GPIO_PAD_STATE & 0xfff3ff7f) |
               ((GPIOA->GPIO_PAD_STATE & ((1 << 16) | (1 << 27) | (1 << 28))) >> 9);
#elif (CONFIG_SHIELD_KEYCHRON_Q1_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_Q1_ULTRA_ANSI_V2 ||          \
       CONFIG_SHIELD_KEYCHRON_Q1_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_Q1_ULTRA_JIS ||               \
       CONFIG_SHIELD_KEYCHRON_K1_ULTRA_JIS || CONFIG_SHIELD_KEYCHRON_K1_ULTRA_ANSI ||              \
       CONFIG_SHIELD_KEYCHRON_K1_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_K3_ULTRA_CAC ||               \
       CONFIG_SHIELD_KEYCHRON_K3_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_K3_ULTRA_ANSI_V2 ||          \
       CONFIG_SHIELD_KEYCHRON_K3_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_K3_ULTRA_JIS ||               \
       CONFIG_SHIELD_KEYCHRON_K8_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_K8_ULTRA_ANSI_V2 ||          \
       CONFIG_SHIELD_KEYCHRON_K8_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_K8_ULTRA_JIS ||               \
       CONFIG_SHIELD_KEYCHRON_J9_ULTRA_ANSI || CONFIG_SHIELD_SKN_BIFANGULTRA_ANSI)
        port = (GPIOB->GPIO_PAD_STATE & 0xfffdffff) | (GPIOA->GPIO_PAD_STATE & (1 << 17));
#elif CONFIG_SHIELD_KEYCHRON_V2_ULTRA_ANSI
        port = (GPIOB->GPIO_PAD_STATE & 0xfff3feff) |
               ((GPIOA->GPIO_PAD_STATE & ((1 << 17) | (1 << 27) | (1 << 28))) >> 9);
#elif (CONFIG_SHIELD_KEYCHRON_QN104_ANSI || CONFIG_SHIELD_KEYCHRON_J2_ULTRA_ANSI ||                \
       CONFIG_SHIELD_KEYCHRON_Q5_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_Q5_ULTRA_ISO ||              \
       CONFIG_SHIELD_KEYCHRON_Q5_ULTRA_JIS || CONFIG_SHIELD_KEYCHRON_Q6_ULTRA_ANSI ||              \
       CONFIG_SHIELD_KEYCHRON_Q6_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_Q6_ULTRA_JIS ||               \
       CONFIG_SHIELD_KEYCHRON_V6_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_V6_ULTRA_ANSI_V2 ||          \
       CONFIG_SHIELD_KEYCHRON_V6_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_V6_ULTRA_JIS ||               \
       CONFIG_SHIELD_KEYCHRON_V3_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_V3_ULTRA_ANSI_V2 ||          \
       CONFIG_SHIELD_KEYCHRON_V3_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_V3_ULTRA_JIS ||               \
       CONFIG_SHIELD_KEYCHRON_V10_ULTRA_ANSI)
        port = (GPIOB->GPIO_PAD_STATE & 0xfff3fe7f) |
               ((GPIOA->GPIO_PAD_STATE & ((1 << 16) | (1 << 17) | (1 << 27) | (1 << 28))) >> 9);
#elif CONFIG_SHIELD_KEYCHRON_V0_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_V0_ULTRA_ANSI_V2
        port = GPIOA->GPIO_PAD_STATE;
#elif (CONFIG_SHIELD_KEYCHRON_Q13_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_K5_ULTRA_ANSI ||            \
       CONFIG_SHIELD_KEYCHRON_K5_ULTRA_JIS_V2 || CONFIG_SHIELD_KEYCHRON_K5_ULTRA_ANSI_V2 ||        \
       CONFIG_SHIELD_KEYCHRON_K5_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_K10_ULTRA_ANSI ||             \
       CONFIG_SHIELD_KEYCHRON_K10_ULTRA_ANSI_V2 || CONFIG_SHIELD_KEYCHRON_K10_ULTRA_ISO ||         \
       CONFIG_SHIELD_KEYCHRON_K10_ULTRA_WB_ISO)
        port = (GPIOB->GPIO_PAD_STATE & 0x7ffbfe7f) | (GPIOA->GPIO_PAD_STATE & (1 << 31));
        port |= ((GPIOA->GPIO_PAD_STATE & ((1 << 16) | (1 << 17) | (1 << 27))) >> 9);
#elif CONFIG_SHIELD_KEYCHRON_Z270_ANSI
        port = (GPIOB->GPIO_PAD_STATE & 0x7fffffff) | (GPIOA->GPIO_PAD_STATE & (1 << 31));
#else
        port = data->inputs.gpios[0].GPIOx->GPIO_PAD_STATE;
#endif
        // note:delay for about 2us;
        for (int i = 0; i < 50; i++) {
#if CONFIG_SHIELD_KEYCHRON_RS87_ANSI || CONFIG_SHIELD_KEYCHRON_Q3_ULTRA_ANSI ||                    \
    CONFIG_SHIELD_KEYCHRON_Q3_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_Q3_ULTRA_JIS
            port = (GPIOB->GPIO_PAD_STATE & 0x7fffffff) | (GPIOA->GPIO_PAD_STATE & (1 << 31));
#elif CONFIG_SHIELD_KEYCHRON_RS45_ANSI
            port = (GPIOB->GPIO_PAD_STATE & 0x7ffcffff) |
                   (GPIOA->GPIO_PAD_STATE & ((1 << 16) | (1 << 17) | (1 << 31)));
#elif (CONFIG_SHIELD_KEYCHRON_K2_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_K2_ULTRA_ISO ||              \
       CONFIG_SHIELD_KEYCHRON_K2_ULTRA_JIS || CONFIG_SHIELD_KEYCHRON_K2_ULTRA_ANSI_V2)
            port = (GPIOB->GPIO_PAD_STATE & 0xfff2ff7f) |
                   ((GPIOA->GPIO_PAD_STATE & ((1 << 16) | (1 << 27))) >> 9);
#elif CONFIG_SHIELD_KEYCHRON_K4_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_K4_ULTRA_ISO ||               \
    CONFIG_SHIELD_KEYCHRON_K4_ULTRA_JIS || CONFIG_SHIELD_KEYCHRON_K4_ULTRA_WB_ISO ||               \
    CONFIG_SHIELD_KEYCHRON_K4_ULTRA_V2_ANSI
            port = (GPIOB->GPIO_PAD_STATE & 0x7ffdfbff) |
                   (GPIOA->GPIO_PAD_STATE & ((1 << 10) | (1 << 17) | (1 << 31)));
#elif (CONFIG_SHIELD_KEYCHRON_V5_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_V5_ULTRA_ISO ||              \
       CONFIG_SHIELD_KEYCHRON_V5_ULTRA_JIS || CONFIG_SHIELD_KEYCHRON_V5_ULTRA_ANSI_V2 ||           \
       CONFIG_SHIELD_KEYCHRON_V5_ULTRA_ISO_V2)
            port = (GPIOB->GPIO_PAD_STATE & 0xfff3ff7f) |
                   ((GPIOA->GPIO_PAD_STATE & ((1 << 16) | (1 << 27) | (1 << 28))) >> 9);
#elif (CONFIG_SHIELD_KEYCHRON_Q1_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_Q1_ULTRA_ANSI_V2 ||          \
       CONFIG_SHIELD_KEYCHRON_Q1_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_Q1_ULTRA_JIS ||               \
       CONFIG_SHIELD_KEYCHRON_K1_ULTRA_JIS || CONFIG_SHIELD_KEYCHRON_K1_ULTRA_ANSI ||              \
       CONFIG_SHIELD_KEYCHRON_K1_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_K3_ULTRA_CAC ||               \
       CONFIG_SHIELD_KEYCHRON_K3_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_K3_ULTRA_ANSI_V2 ||          \
       CONFIG_SHIELD_KEYCHRON_K3_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_K3_ULTRA_JIS ||               \
       CONFIG_SHIELD_KEYCHRON_K8_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_K8_ULTRA_ANSI_V2 ||          \
       CONFIG_SHIELD_KEYCHRON_K8_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_K8_ULTRA_JIS ||               \
       CONFIG_SHIELD_KEYCHRON_J9_ULTRA_ANSI || CONFIG_SHIELD_SKN_BIFANGULTRA_ANSI)
            port = (GPIOB->GPIO_PAD_STATE & 0xfffdffff) | (GPIOA->GPIO_PAD_STATE & (1 << 17));
#elif CONFIG_SHIELD_KEYCHRON_V2_ULTRA_ANSI
            port = (GPIOB->GPIO_PAD_STATE & 0xfff3feff) |
                   ((GPIOA->GPIO_PAD_STATE & ((1 << 17) | (1 << 27) | (1 << 28))) >> 9);
#elif (CONFIG_SHIELD_KEYCHRON_QN104_ANSI || CONFIG_SHIELD_KEYCHRON_J2_ULTRA_ANSI ||                \
       CONFIG_SHIELD_KEYCHRON_Q5_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_Q5_ULTRA_ISO ||              \
       CONFIG_SHIELD_KEYCHRON_Q5_ULTRA_JIS || CONFIG_SHIELD_KEYCHRON_Q6_ULTRA_ANSI ||              \
       CONFIG_SHIELD_KEYCHRON_Q6_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_Q6_ULTRA_JIS ||               \
       CONFIG_SHIELD_KEYCHRON_V6_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_V6_ULTRA_ANSI_V2 ||          \
       CONFIG_SHIELD_KEYCHRON_V6_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_V6_ULTRA_JIS ||               \
       CONFIG_SHIELD_KEYCHRON_V3_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_V3_ULTRA_ANSI_V2 ||          \
       CONFIG_SHIELD_KEYCHRON_V3_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_V3_ULTRA_JIS ||               \
       CONFIG_SHIELD_KEYCHRON_V10_ULTRA_ANSI)
            port = (GPIOB->GPIO_PAD_STATE & 0xfff3fe7f) |
                   ((GPIOA->GPIO_PAD_STATE & ((1 << 16) | (1 << 17) | (1 << 27) | (1 << 28))) >> 9);
#elif CONFIG_SHIELD_KEYCHRON_V0_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_V0_ULTRA_ANSI_V2
            port = GPIOA->GPIO_PAD_STATE;
#elif (CONFIG_SHIELD_KEYCHRON_Q13_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_K5_ULTRA_ANSI ||            \
       CONFIG_SHIELD_KEYCHRON_K5_ULTRA_JIS_V2 || CONFIG_SHIELD_KEYCHRON_K5_ULTRA_ANSI_V2 ||        \
       CONFIG_SHIELD_KEYCHRON_K5_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_K10_ULTRA_ANSI ||             \
       CONFIG_SHIELD_KEYCHRON_K10_ULTRA_ANSI_V2 || CONFIG_SHIELD_KEYCHRON_K10_ULTRA_ISO ||         \
       CONFIG_SHIELD_KEYCHRON_K10_ULTRA_WB_ISO)
            port = (GPIOB->GPIO_PAD_STATE & 0x7ffbfe7f) | (GPIOA->GPIO_PAD_STATE & (1 << 31));
            port |= ((GPIOA->GPIO_PAD_STATE & ((1 << 16) | (1 << 17) | (1 << 27))) >> 9);
#elif CONFIG_SHIELD_KEYCHRON_Z270_ANSI
            port = (GPIOB->GPIO_PAD_STATE & 0x7fffffff) | (GPIOA->GPIO_PAD_STATE & (1 << 31));
#else
            port = data->inputs.gpios[0].GPIOx->GPIO_PAD_STATE;
#endif
        }
        irq_unlock(key);
        // LOG_DBG("col:%d,count:%d,port:%d",col,count,port);

        for (int j = 0; j < data->inputs.len; j++) {
            const struct kscan_gpio *in_gpio = &data->inputs.gpios[j];
            const int index = state_index_io(config, in_gpio->index, col);
            int pos = in_gpio->spec.pin;
#if (CONFIG_SHIELD_KEYCHRON_K2_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_K2_ULTRA_ANSI_V2 ||            \
     CONFIG_SHIELD_KEYCHRON_K2_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_K2_ULTRA_JIS ||                 \
     CONFIG_SHIELD_KEYCHRON_J2_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_V5_ULTRA_ANSI ||               \
     CONFIG_SHIELD_KEYCHRON_V5_ULTRA_ISO_V2 || CONFIG_SHIELD_KEYCHRON_V5_ULTRA_ANSI_V2 ||          \
     CONFIG_SHIELD_KEYCHRON_V5_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_V5_ULTRA_JIS ||                 \
     CONFIG_SHIELD_KEYCHRON_Q5_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_Q5_ULTRA_ISO ||                \
     CONFIG_SHIELD_KEYCHRON_Q5_ULTRA_JIS || CONFIG_SHIELD_KEYCHRON_QN104_ANSI ||                   \
     CONFIG_SHIELD_KEYCHRON_V2_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_Q6_ULTRA_ANSI ||               \
     CONFIG_SHIELD_KEYCHRON_Q6_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_Q6_ULTRA_JIS ||                 \
     CONFIG_SHIELD_KEYCHRON_V6_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_V6_ULTRA_ANSI_V2 ||            \
     CONFIG_SHIELD_KEYCHRON_V6_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_V6_ULTRA_JIS ||                 \
     CONFIG_SHIELD_KEYCHRON_V3_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_V3_ULTRA_ANSI_V2 ||            \
     CONFIG_SHIELD_KEYCHRON_V3_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_V3_ULTRA_JIS ||                 \
     CONFIG_SHIELD_KEYCHRON_V10_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_Q13_ULTRA_ANSI ||             \
     CONFIG_SHIELD_KEYCHRON_K5_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_K5_ULTRA_JIS_V2 ||             \
     CONFIG_SHIELD_KEYCHRON_K5_ULTRA_ANSI_V2 || CONFIG_SHIELD_KEYCHRON_K5_ULTRA_ISO ||             \
     CONFIG_SHIELD_KEYCHRON_K10_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_K10_ULTRA_ANSI_V2 ||          \
     CONFIG_SHIELD_KEYCHRON_K10_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_K10_ULTRA_WB_ISO)
            // fix pin
            if (in_gpio->GPIOx == GPIOA) {
#if (CONFIG_SHIELD_KEYCHRON_Q13_ULTRA_ANSI || CONFIG_SHIELD_KEYCHRON_K5_ULTRA_ANSI ||              \
     CONFIG_SHIELD_KEYCHRON_K5_ULTRA_JIS_V2 || CONFIG_SHIELD_KEYCHRON_K5_ULTRA_ANSI_V2 ||          \
     CONFIG_SHIELD_KEYCHRON_K5_ULTRA_ISO || CONFIG_SHIELD_KEYCHRON_K10_ULTRA_ANSI ||               \
     CONFIG_SHIELD_KEYCHRON_K10_ULTRA_ANSI_V2 || CONFIG_SHIELD_KEYCHRON_K10_ULTRA_ISO ||           \
     CONFIG_SHIELD_KEYCHRON_K10_ULTRA_WB_ISO)
                if (in_gpio->spec.pin != 31)
#endif
                    pos = in_gpio->spec.pin - 9;
            }
#endif
            int active = (port & BIT(pos)) ? 1 : 0;
            // if(col==5 && j == 6)
            // {
            //     LOG_DBG("row:%d,pin:%d,col:%d,active:%d,port:%x",j,in_gpio->spec.pin,col,active,port);
            //     LOG_DBG("changed:%d,counter:%d,pressed:%d",data->matrix_state[index].changed,data->matrix_state[index].counter,data->matrix_state[index].pressed);
            // }

#if CONFIG_LAUNCHER_USER_SET_DEBOUNCE
#if CONFIG_FAST_KSCAN
            zmk_debounce_update(&data->matrix_state[index], active, elapsed_ms,
                                &user_debounce.debounce_config);
#else
            zmk_debounce_update(&data->matrix_state[index], active,
                                user_debounce.debounce_scan_period_ms,
                                &user_debounce.debounce_config);
#endif
#else
            zmk_debounce_update(&data->matrix_state[index], active, config->debounce_scan_period_ms,
                                &config->debounce_config);
#endif
        }
        out_gpio->GPIOx->GPIO_DR &= ~BIT(out_gpio->spec.pin); // set 0

        for (int i = 0; i < 10; i++) {
            port = data->inputs.gpios[0].GPIOx->GPIO_PAD_STATE;
        }
    }
#else
    volatile uint32_t port;
    uint32_t key;
    key = irq_lock();
    // row output 0
    GPIOB->GPIO_DDR |= 0x8001f;
    GPIOB->GPIO_DR &= ~0x8001f;
    irq_unlock(key);

    toggle_debug_pin();
    // note:delay for about 2us;
    for (int i = 0; i < 50; i++) {
        port = data->inputs.gpios[0].GPIOx->GPIO_PAD_STATE;
    }
    toggle_debug_pin();
#if CONFIG_COL_595_AND_GPIOS_OUTPUT
    uint32_t out = 0xfff7ffff;
#endif
    for (int col = 0; col < config->outputs.len; col++) {
        key = irq_lock();
        (col == 0) ? HC595_DATA_LOW : HC595_DATA_HIGH;
        HC595_SHIFT_CLK_HIGH;
        HC595_LATCH_CLK_HIGH;
#if CONFIG_COL_595_AND_GPIOS_OUTPUT
        gpio_pin_set_dt(&c19_gpio, out & BIT(0));
        gpio_pin_set_dt(&c18_gpio, out & BIT(1));
        gpio_pin_set_dt(&c17_gpio, out & BIT(2));
        gpio_pin_set_dt(&c16_gpio, out & BIT(3));
#endif
        irq_unlock(key);

        GPIOB->GPIO_DDR &= ~0x8001f; // set rows input.

        port = data->inputs.gpios[0].GPIOx->GPIO_PAD_STATE;

        // note:delay for about 2us;
        for (int i = 0; i < 50; i++) {
            port = data->inputs.gpios[0].GPIOx->GPIO_PAD_STATE;
        }

        for (int j = 0; j < data->inputs.len; j++) {
            const struct kscan_gpio *in_gpio = &data->inputs.gpios[j];
            const int index = state_index_io(config, in_gpio->index, col);
            int active = (port & BIT(in_gpio->spec.pin)) ? 0 : 1;
            // if(active)
            //     LOG_DBG("row:%d,pin:%d,col:%d,active:%d,port:%x",j,in_gpio->spec.pin,col,active,port);
#if CONFIG_LAUNCHER_USER_SET_DEBOUNCE
            zmk_debounce_update(&data->matrix_state[index], active,
                                user_debounce.debounce_scan_period_ms,
                                &user_debounce.debounce_config);
#else
            zmk_debounce_update(&data->matrix_state[index], active, config->debounce_scan_period_ms,
                                &config->debounce_config);
#endif
        }

        HC595_SHIFT_CLK_LOW;
        HC595_LATCH_CLK_LOW;
#if CONFIG_COL_595_AND_GPIOS_OUTPUT
        gpio_pin_set_dt(&c19_gpio, 1);
        gpio_pin_set_dt(&c18_gpio, 1);
        gpio_pin_set_dt(&c17_gpio, 1);
        gpio_pin_set_dt(&c16_gpio, 1);
        out = (out << 31) | (out >> 1);
#endif
        for (int i = 0; i < 20; i++) {
            port = data->inputs.gpios[0].GPIOx->GPIO_PAD_STATE;
        }
    }
#endif
    // toggle_debug_pin();
    bool continue_scan = false;

    for (int r = 0; r < config->rows; r++) {
        for (int c = 0; c < config->cols; c++) {
            const int index = state_index_rc(config, r, c);
            struct zmk_debounce_state *state = &data->matrix_state[index];

            if (zmk_debounce_get_changed(state)) {
                const bool pressed = zmk_debounce_is_pressed(state);

                if (pressed)
                    data->press_rows |= BIT(r);
                else
                    data->press_rows &= ~BIT(r);
                // LOG_DBG("Sending event at %i,%i state %s", r, c, pressed ? "on" : "off");
                //  DBG_DIRECT("Sending event at %d,%d state %s", r, c, pressed ? "on" : "off");
                if (data->callback)
                    data->callback(dev, r, c, pressed);
            }

            continue_scan = continue_scan || zmk_debounce_is_active(state);
        }
    }

    if (continue_scan) {
#if CONFIG_FAST_KSCAN
        if (get_report_rate() >= 8 || is_ble()) { //|| macro_running){
            // rtk_counter_start(user_debounce.debounce_scan_period_ms*1000);
            kscan_matrix_read_continue(dev);
            // k_work_reschedule(&data->work,K_MSEC(user_debounce.debounce_scan_period_ms));
        } else {
            rtk_counter_start(400);
        }
        scan_idle_count = 0;
#else
        kscan_matrix_read_continue(dev);
#endif
    } else {

#if CONFIG_FAST_KSCAN
        // static uint32_t t =2000;
        if (get_report_rate() >= 8 || is_ble()) { //|| macro_running){
            // t =1000/(user_debounce.debounce_scan_period_ms);
            if (scan_idle_count) {
                scan_idle_count = 0;
                counter_stop(counter_dev);
            }
            kscan_matrix_read_end(dev);
            return 0;
        }
        if (++scan_idle_count > 2000) {
            counter_stop(counter_dev);
            LOG_ERR("scan end:%d", scan_idle_count);
            scan_idle_count = 0;
            kscan_matrix_read_end(dev);
            counter_stop(counter_dev);
            kscan_enabled_dlps = true;
        } else {
            // if(get_report_rate()>=8 || is_ble()||macro_running)
            //     // rtk_counter_start(user_debounce.debounce_scan_period_ms*1000);
            //     k_work_reschedule(&data->work,K_MSEC(user_debounce.debounce_scan_period_ms));
            // else
            rtk_counter_start(400);
        }
#else
        kscan_matrix_read_end(dev);
#endif
    }

    return 0;
}
#else
static int kscan_matrix_read(const struct device *dev) {
    struct kscan_matrix_data *data = dev->data;
    const struct kscan_matrix_config *config = dev->config;
    LOG_DBG("read.");
    // toggle_debug_pin();
    //  Scan the matrix.
    for (int i = 0; i < config->outputs.len; i++) {
        const struct kscan_gpio *out_gpio = &config->outputs.gpios[i];
        // add
        gpio_pin_configure_dt(&out_gpio->spec, GPIO_OUTPUT);
        int err = gpio_pin_set_dt(&out_gpio->spec, 1);

        if (err) {
            LOG_ERR("Failed to set output %i active: %i", out_gpio->index, err);
            return err;
        }

#if CONFIG_ZMK_KSCAN_MATRIX_WAIT_BEFORE_INPUTS > 0
        k_busy_wait(CONFIG_ZMK_KSCAN_MATRIX_WAIT_BEFORE_INPUTS);
#endif
        struct kscan_gpio_port_state state = {0};

        for (int j = 0; j < data->inputs.len; j++) {
            const struct kscan_gpio *in_gpio = &data->inputs.gpios[j];

            const int index = state_index_io(config, in_gpio->index, out_gpio->index);
            const int active = kscan_gpio_pin_get(in_gpio, &state);
            if (active < 0) {
                LOG_ERR("Failed to read port %s: %i", in_gpio->spec.port->name, active);
                return active;
            }
            if (in_gpio->index == 2 && (out_gpio->index == 1 || out_gpio->index == 2)) {
                LOG_WRN("@r:%d,c:%d,active:%d", in_gpio->index, out_gpio->index, active);
            }
            zmk_debounce_update(&data->matrix_state[index], active, config->debounce_scan_period_ms,
                                &config->debounce_config);
            if (data->matrix_state[index].counter) {
                LOG_WRN("r:%d,c:%d,active:%d", in_gpio->index, out_gpio->index, active);
            }
        }

        err = gpio_pin_set_dt(&out_gpio->spec, 0);
        // add
        gpio_pin_configure_dt(&out_gpio->spec, GPIO_INPUT);
        if (err) {
            LOG_ERR("Failed to set output %i inactive: %i", out_gpio->index, err);
            return err;
        }

        // toggle_debug_pin();
#if CONFIG_ZMK_KSCAN_MATRIX_WAIT_BETWEEN_OUTPUTS > 0
        k_busy_wait(CONFIG_ZMK_KSCAN_MATRIX_WAIT_BETWEEN_OUTPUTS);
#endif
    }

    // Process the new state.
    bool continue_scan = false;

    for (int r = 0; r < config->rows; r++) {
        for (int c = 0; c < config->cols; c++) {
            const int index = state_index_rc(config, r, c);
            struct zmk_debounce_state *state = &data->matrix_state[index];

            if (zmk_debounce_get_changed(state)) {
                const bool pressed = zmk_debounce_is_pressed(state);

                LOG_DBG("Sending event at %i,%i state %s", r, c, pressed ? "on" : "off");
                data->callback(dev, r, c, pressed);
            }

            continue_scan = continue_scan || zmk_debounce_is_active(state);
        }
    }
    // toggle_debug_pin();
    if (continue_scan) {
        // At least one key is pressed or the debouncer has not yet decided if
        // it is pressed. Poll quickly until everything is released.
        kscan_matrix_read_continue(dev);
    } else {
        // All keys are released. Return to normal.
        kscan_matrix_read_end(dev);
    }

    return 0;
}
#endif
static void kscan_matrix_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct kscan_matrix_data *data = CONTAINER_OF(dwork, struct kscan_matrix_data, work);
    kscan_matrix_read(data->dev);
}

static int kscan_matrix_configure(const struct device *dev, const kscan_callback_t callback) {
    struct kscan_matrix_data *data = dev->data;

    if (!callback) {
        return -EINVAL;
    }

    data->callback = callback;
    return 0;
}

static int kscan_matrix_enable(const struct device *dev) {
    struct kscan_matrix_data *data = dev->data;
    const struct kscan_matrix_config *config = dev->config;

    data->scan_time = k_uptime_get();

    // force to load flash debouce code to cache !
    zmk_debounce_update(&data->matrix_state[0], 0, 0, &config->debounce_config);

    // Read will automatically start interrupts/polling once done.
    return kscan_matrix_read(dev);
}

static int kscan_matrix_disable(const struct device *dev) {
    struct kscan_matrix_data *data = dev->data;

    k_work_cancel_delayable(&data->work);

#if USE_INTERRUPTS
    return kscan_matrix_interrupt_disable(dev);
#else
    return 0;
#endif
}

static int kscan_matrix_init_input_inst(const struct device *dev, const struct kscan_gpio *gpio) {
    if (!device_is_ready(gpio->spec.port)) {
        LOG_ERR("GPIO is not ready: %s", gpio->spec.port->name);
        return -ENODEV;
    }

    int err = gpio_pin_configure_dt(&gpio->spec, GPIO_INPUT);
    if (err) {
        LOG_ERR("Unable to configure pin %u on %s for input", gpio->spec.pin,
                gpio->spec.port->name);
        return err;
    }

    LOG_DBG("Configured pin %u on %s for input", gpio->spec.pin, gpio->spec.port->name);

#if USE_INTERRUPTS
    struct kscan_matrix_data *data = dev->data;
    struct kscan_matrix_irq_callback *irq = &data->irqs[gpio->index];

    irq->dev = dev;
    gpio_init_callback(&irq->callback, kscan_matrix_irq_callback_handler, BIT(gpio->spec.pin));
    err = gpio_add_callback(gpio->spec.port, &irq->callback);
    if (err) {
        LOG_ERR("Error adding the callback to the input device: %i", err);
        return err;
    }
#endif

    return 0;
}

static int kscan_matrix_init_inputs(const struct device *dev) {
    struct kscan_matrix_data *data = dev->data;

    for (int i = 0; i < data->inputs.len; i++) {
        const struct kscan_gpio *gpio = &data->inputs.gpios[i];
#if RTK_GPIO_FAST
        const struct gpio_rtl87x2g_config *rtk_config =
            (struct gpio_rtl87x2g_config *)gpio->spec.port->config;
        data->inputs.gpios[i].GPIOx = rtk_config->port_base;
        // uint8_t pad =gpio_rtl87x2g_gpio2pad(1,gpio->spec.pin);
        // LOG_ERR("pin:%d,pad:%d",gpio->spec.pin,pad);
        // Pad_SetPullStrength(pad,PAD_PULL_STRONG);

#endif
        int err = kscan_matrix_init_input_inst(dev, gpio);
        if (err) {
            return err;
        }
    }

    return 0;
}

static int kscan_matrix_init_output_inst(const struct device *dev,
                                         const struct gpio_dt_spec *gpio) {
    if (!device_is_ready(gpio->port)) {
        LOG_ERR("GPIO is not ready: %s", gpio->port->name);
        return -ENODEV;
    }

    int err = gpio_pin_configure_dt(gpio, GPIO_OUTPUT);
    if (err) {
        LOG_ERR("Unable to configure pin %u on %s for output", gpio->pin, gpio->port->name);
        return err;
    }

    LOG_DBG("Configured pin %u on %s for output", gpio->pin, gpio->port->name);

    return 0;
}

static int kscan_matrix_init_outputs(const struct device *dev) {
    const struct kscan_matrix_config *config = dev->config;
    struct kscan_matrix_data *data = dev->data;
    for (int i = 0; i < config->outputs.len; i++) {
        const struct gpio_dt_spec *gpio = &config->outputs.gpios[i].spec;
#if RTK_GPIO_FAST
        const struct gpio_rtl87x2g_config *rtk_config =
            (struct gpio_rtl87x2g_config *)data->outputs.gpios[i].spec.port->config;
        data->outputs.gpios[i].GPIOx = rtk_config->port_base;
        LOG_DBG("i:%d,gpiox:%p", i, data->outputs.gpios[i].GPIOx);
#endif
        int err = kscan_matrix_init_output_inst(dev, gpio);
        if (err) {
            return err;
        }
    }

    return 0;
}
const static struct device *p_matrix_dev;
// static uint8_t skip_suspend;
static int kscan_matrix_init(const struct device *dev) {
    LOG_ERR("kscan_matrix_init");
    struct kscan_matrix_data *data = dev->data;

    data->dev = dev;

    p_matrix_dev = dev;

    const struct kscan_matrix_config *config = dev->config;
#if CONFIG_LAUNCHER_USER_SET_DEBOUNCE
    user_debounce.debounce_scan_period_ms = config->debounce_scan_period_ms;
    user_debounce.default_scan_period = config->debounce_scan_period_ms;
    user_debounce.debounce_config.debounce_press_ms = config->debounce_config.debounce_press_ms;
    user_debounce.debounce_config.debounce_release_ms = config->debounce_config.debounce_release_ms;
#endif
    pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
    swd_pin_disable();
    // Sort inputs by port so we can read each port just once per scan.
    // kscan_gpio_list_sort_by_port(&data->inputs);

    kscan_matrix_init_inputs(dev);
    kscan_matrix_init_outputs(dev);
    kscan_matrix_set_all_outputs(dev, 0);

    k_work_init_delayable(&data->work, kscan_matrix_work_handler);

    return 0;
}

static const struct kscan_driver_api kscan_matrix_api = {
    .config = kscan_matrix_configure,
    .enable_callback = kscan_matrix_enable,
    .disable_callback = kscan_matrix_disable,
};
#if IS_ENABLED(CONFIG_PM_DEVICE)
#if 0
static int kscan_matrix_pm_action(const struct device *dev, enum pm_device_action action) {
    const struct kscan_matrix_config *config = dev->config;
    switch (action) {
    case PM_DEVICE_ACTION_SUSPEND:
        // kscan_matrix_disconnect_inputs(dev);
        // kscan_matrix_disconnect_outputs(dev);
        // return kscan_matrix_disable(dev);

        {
            // LOG_ERR("matrix enter suspend,skip:%d",skip_suspend);
            struct kscan_matrix_data *data = dev->data;
            k_work_cancel_delayable(&data->work);
            if(!skip_suspend)
            {
                skip_suspend=0;
                kscan_matrix_interrupt_enable(dev);
            }
            pinctrl_apply_state(config->pcfg, PINCTRL_STATE_SLEEP);
        }
        return 0;
    case PM_DEVICE_ACTION_RESUME:
        // kscan_matrix_setup_pins(dev);
        // return kscan_matrix_enable(dev);
        pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
        return 0;
    default:
        return -ENOTSUP;
    }
}
#else
static int kscan_matrix_pm_action(const struct device *dev, enum pm_device_action action) {
    const struct kscan_matrix_config *config = dev->config;
    struct kscan_matrix_data *data = dev->data;
    int err;
    switch (action) {
    case PM_DEVICE_ACTION_SUSPEND:
        const struct pinctrl_state *state;
        if (!data->press_rows) {
            // LOG_ERR("PM SUSPEND 0");
            err = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_SLEEP);
            if ((err < 0) && (err != -ENOENT)) {
                // DBG_DIRECT("pm 0,err:%d",err);
                return err;
            }

            // DBG_DIRECT("pm 0");

        } else {
            // DBG_DIRECT("pm 1");
            // LOG_DBG("pm1");

            err = pinctrl_lookup_state(config->pcfg, PINCTRL_STATE_SLEEP, &state);
            uint8_t i = 0;
#if !(defined(CONFIG_COL_595_OUTPUT))
            for (i = 0; i < config->rows; i++) // row output, col input ,pins seq = rows cols;
#else
            for (i = config->rows; i < state->pin_cnt; i++) // row input, col output(595)
#endif
            {

                Pad_Config(state->pins[i].pin, PAD_SW_MODE, PAD_IS_PWRON, state->pins[i].pull,
                           state->pins[i].dir, state->pins[i].drive);
            }
#if !(defined(CONFIG_COL_595_OUTPUT))
            for (i = config->rows; i < state->pin_cnt; i++) // row output, col input
#else
            for (i = 0; i < config->rows; i++)              // row input, col output
#endif
            {
                uint8_t wakeup_flag = state->pins[i].wakeup_high || state->pins[i].wakeup_low;
                if (wakeup_flag) {
                    // LOG_ERR("i:%d,pin:%d,wakeup
                    // high:%d,low:%d",i,state->pins[i].pin,state->pins[i].wakeup_high,state->pins[i].wakeup_low);
                    if (data->press_rows & BIT(i)) {
                        if (state->pins[i].wakeup_high) {
                            // LOG_DBG("wake high , row%d,
                            // pin%d",i-config->rows,state->pins[i].pin);
                            Pad_SetControlMode(state->pins[i].pin, PAD_SW_MODE);
                            System_WakeUpPinEnable(state->pins[i].pin, PAD_WAKEUP_POL_LOW, DISABLE);
                            Pad_Config(state->pins[i].pin, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_UP,
                                       state->pins[i].dir, state->pins[i].drive);
                        } else if (state->pins[i].wakeup_low) {
                            // LOG_DBG("wake low , row%d, pin%d",i,state->pins[i].pin);
                            Pad_SetControlMode(state->pins[i].pin, PAD_SW_MODE);
                            System_WakeUpPinEnable(state->pins[i].pin, PAD_WAKEUP_POL_HIGH,
                                                   DISABLE);
                            Pad_Config(state->pins[i].pin, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_DOWN,
                                       state->pins[i].dir, state->pins[i].drive);
                        }
                    } else {
                        // LOG_ERR("xx");
                        if (state->pins[i].wakeup_high) {
                            Pad_SetControlMode(state->pins[i].pin, PAD_SW_MODE);
                            System_WakeUpPinEnable(state->pins[i].pin, PAD_WAKEUP_POL_HIGH,
                                                   DISABLE);
                            Pad_Config(state->pins[i].pin, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_DOWN,
                                       state->pins[i].dir, state->pins[i].drive);
                        } else if (state->pins[i].wakeup_low) {
                            Pad_SetControlMode(state->pins[i].pin, PAD_SW_MODE);
                            System_WakeUpPinEnable(state->pins[i].pin, PAD_WAKEUP_POL_LOW, DISABLE);
                            Pad_Config(state->pins[i].pin, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_UP,
                                       state->pins[i].dir, state->pins[i].drive);
                        }
                    }
                }
            }
        }
        return 0;
    case PM_DEVICE_ACTION_RESUME:
        // DBG_DIRECT("resume");
        // LOG_DBG("resume");
        err = pinctrl_lookup_state(config->pcfg, PINCTRL_STATE_SLEEP, &state);
        if ((err < 0) && (err != -ENOENT)) {

            return err;
        } else {
            /* there are kscan wakeup pins configured, check if they wakeup the system */
#if !(defined(CONFIG_COL_595_OUTPUT))
            for (uint8_t i = config->rows; i < state->pin_cnt; i++)
#else
            for (uint8_t i = 0; i < config->rows; i++)
#endif
            {
                if (state->pins[i].wakeup_low || state->pins[i].wakeup_high) {
                    System_WakeUpPinDisable(state->pins[i].pin);
                    if (System_WakeUpInterruptValue(state->pins[i].pin) == SET) {
                        // DBG_DIRECT("i:%d,pin:%d,wakeup",i-config->rows,state->pins[i].pin);

                        Pad_ClearWakeupINTPendingBit(state->pins[i].pin);
                        // is_app_enabled_dlps =false;
                        /* Set pins to active state */
                        err = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
                        if (err < 0) {
                            return err;
                        }
                    }
                }
            }
        }
        pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
        return 0;
    default:
        return -ENOTSUP;
    }
}
#endif

uint32_t matrix_get_row(uint8_t row);
// void keyboard_matrix_shutdown(void)
// {
//     uint16_t wakeup_pins=0;
//     if(p_matrix_dev==NULL) return;
//     struct kscan_matrix_data *data = p_matrix_dev->data;
//     k_work_cancel_delayable(&data->work);

//     for(int i=0;i<6;i++)
//     {
//         for(int col=0;col<16;col++)
//            wakeup_pins |= (matrix_get_row(i)&(1<<col))?(1<<col):0;
//     }
//     LOG_ERR("wk pins:%x",wakeup_pins);
//     kscan_matrix_set_all_outputs(p_matrix_dev,1);
//     for (int i = 0; i < data->inputs.len; i++) {
//         const struct gpio_dt_spec *gpio = &data->inputs.gpios[i].spec;
//         gpio_pin_interrupt_configure_dt(gpio, (wakeup_pins &
//         (1<<i))?GPIO_INT_LEVEL_INACTIVE:GPIO_INT_LEVEL_ACTIVE);

//     }

//     LOG_ERR("kb shutdown!");
//     skip_suspend=1;
//     is_app_enabled_dlps=true;
// }
#endif // IS_ENABLED(CONFIG_PM_DEVICE)

#if CONFIG_FAST_KSCAN
static struct counter_top_cfg top_cfg;
static void counter_interrupt_fn(const struct device *dev, void *user_data) {
    const struct device *pdev = kscan_dev;
    // kscan_matrix_read(pdev);
    struct kscan_matrix_data *data = pdev->data;
    k_work_reschedule(&data->work, K_NO_WAIT);
}
static void rtk_counter_start(uint32_t usec) {

    if (!device_is_ready(counter_dev)) {
        printk("device not ready.\n");
        return;
    }

    counter_start(counter_dev);

    top_cfg.flags = 0;
    top_cfg.ticks = counter_us_to_ticks(counter_dev, usec);
    top_cfg.callback = counter_interrupt_fn;
    top_cfg.user_data = &top_cfg;

    int err = counter_set_top_value(counter_dev, &top_cfg);
    if (err) {
        LOG_ERR("counter set top err:%d", err);
    }
    kscan_enabled_dlps = false;
}
#endif

#define KSCAN_MATRIX_INIT(n)                                                                       \
    BUILD_ASSERT(INST_DEBOUNCE_PRESS_MS(n) <= DEBOUNCE_COUNTER_MAX,                                \
                 "ZMK_KSCAN_DEBOUNCE_PRESS_MS or debounce-press-ms is too large");                 \
    BUILD_ASSERT(INST_DEBOUNCE_RELEASE_MS(n) <= DEBOUNCE_COUNTER_MAX,                              \
                 "ZMK_KSCAN_DEBOUNCE_RELEASE_MS or debounce-release-ms is too large");             \
                                                                                                   \
    PINCTRL_DT_INST_DEFINE(n);                                                                     \
    static struct kscan_gpio kscan_matrix_rows_##n[] = {                                           \
        LISTIFY(INST_ROWS_LEN(n), KSCAN_GPIO_ROW_CFG_INIT, (, ), n)};                              \
                                                                                                   \
    static struct kscan_gpio kscan_matrix_cols_##n[] = {                                           \
        LISTIFY(INST_COLS_LEN(n), KSCAN_GPIO_COL_CFG_INIT, (, ), n)};                              \
                                                                                                   \
    static struct zmk_debounce_state kscan_matrix_state_##n[INST_MATRIX_LEN(n)];                   \
                                                                                                   \
    COND_INTERRUPTS(                                                                               \
        (static struct kscan_matrix_irq_callback kscan_matrix_irqs_##n[INST_INPUTS_LEN(n)];))      \
                                                                                                   \
    static struct kscan_matrix_data kscan_matrix_data_##n = {                                      \
        .inputs =                                                                                  \
            KSCAN_GPIO_LIST(COND_DIODE_DIR(n, (kscan_matrix_cols_##n), (kscan_matrix_rows_##n))),  \
        .outputs =                                                                                 \
            KSCAN_GPIO_LIST(COND_DIODE_DIR(n, (kscan_matrix_rows_##n), (kscan_matrix_cols_##n))),  \
        .matrix_state = kscan_matrix_state_##n,                                                    \
        COND_INTERRUPTS((.irqs = kscan_matrix_irqs_##n, ))};                                       \
                                                                                                   \
    static struct kscan_matrix_config kscan_matrix_config_##n = {                                  \
        .rows = ARRAY_SIZE(kscan_matrix_rows_##n),                                                 \
        .cols = ARRAY_SIZE(kscan_matrix_cols_##n),                                                 \
        .outputs =                                                                                 \
            KSCAN_GPIO_LIST(COND_DIODE_DIR(n, (kscan_matrix_rows_##n), (kscan_matrix_cols_##n))),  \
        .debounce_config =                                                                         \
            {                                                                                      \
                .debounce_press_ms = INST_DEBOUNCE_PRESS_MS(n),                                    \
                .debounce_release_ms = INST_DEBOUNCE_RELEASE_MS(n),                                \
            },                                                                                     \
        .debounce_scan_period_ms = DT_INST_PROP(n, debounce_scan_period_ms),                       \
        .poll_period_ms = DT_INST_PROP(n, poll_period_ms),                                         \
        .diode_direction = INST_DIODE_DIR(n),                                                      \
        .pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                                 \
    };                                                                                             \
                                                                                                   \
    PM_DEVICE_DT_INST_DEFINE(n, kscan_matrix_pm_action);                                           \
    DEVICE_DT_INST_DEFINE(n, &kscan_matrix_init, PM_DEVICE_DT_INST_GET(n), &kscan_matrix_data_##n, \
                          &kscan_matrix_config_##n, POST_KERNEL, CONFIG_KSCAN_INIT_PRIORITY,       \
                          &kscan_matrix_api);

DT_INST_FOREACH_STATUS_OKAY(KSCAN_MATRIX_INIT);
