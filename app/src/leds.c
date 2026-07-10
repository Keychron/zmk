/*
 * Copyright (c) 2018-2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/led.h>
#include <zephyr/pm/device.h>
#include "led_effect.h"
#include <zmk/leds.h>
#include <zmk/endpoints.h>


// #include <caf/events/power_event.h>
// #include <caf/events/led_event.h>

// #define MODULE leds
// #include <caf/events/module_state_event.h>

#include <zephyr/logging/log.h>
// LOG_MODULE_REGISTER(MODULE, CONFIG_CAF_LEDS_LOG_LEVEL);

static const struct led_effect led_peer_state_effect[] = 
{
	[LED_PEER_STATE_DISCONNECTED]   = LED_EFFECT_LED_OFF(),
	[LED_PEER_STATE_CONNECTED]      = LED_EFFECT_LED_ON_GO_OFF(LED_COLOR(150, 0, 0),3000,100),
	[LED_PEER_STATE_PAIR]    = LED_EFFECT_LED_BLINK(1000, LED_COLOR(150, 0, 0)),
	[LED_PEER_STATE_RECONN] = LED_EFFECT_LED_FLASH(3, 200,LED_COLOR(150, 0, 0)),// LED_EFFECT_LED_BLINK(200, LED_COLOR(150, 0, 0)),
	[LED_PEER_STATE_FAILE] = LED_EFFECT_LED_FLASH(3, 300,LED_COLOR(150, 0, 0)),
	[LED_PEER_STATE_POWER_ON]      = LED_EFFECT_LED_ON_GO_OFF(LED_COLOR(150, 150, 150),2500,100),
	[LED_PEER_STATE_ON_100MS] = LED_EFFECT_LED_ON_GO_OFF(LED_COLOR(100, 100,100),200,100),
	[LED_PEER_STATE_BAT_LOW] = LED_EFFECT_LED_FLASH3(5, LED_COLOR(255, 0,0)),
	[LED_PEER_STATE_BAT_CHARGING] = LED_EFFECT_LED_ON(LED_COLOR(255, 0,0)),
	[LED_PEER_STATE_BAT_CHARGEDONE] = LED_EFFECT_LED_ON(LED_COLOR(0, 255,0)),
	[LED_PEER_STATE_RECOVER] = LED_EFFECT_LED_FLASH2(3, LED_COLOR(0, 0,255)),


};
LOG_MODULE_DECLARE(zmk,4);

// K_SEM_DEFINE(power_on_sem,0,1);
void power_on_finish_cb(struct k_work * work);
K_WORK_DEFINE(power_on_finish,power_on_finish_cb);
static uint8_t led_power_on_status;

#define LED_ID(led) ((led) - &leds[0])

struct led {
	const struct device *dev;
	uint8_t color_count;

	struct led_color color;
	const struct led_effect *effect;
	uint16_t effect_step;
	uint16_t effect_substep;

	// struct k_work_delayable work;
	struct k_timer timer;
};

#ifdef CONFIG_ZMK_LEDS_PWM
  #define DT_DRV_COMPAT pwm_leds
#elif defined(CONFIG_ZMK_LEDS_GPIO)
  #define DT_DRV_COMPAT gpio_leds
#else
  #error "LED driver must be specified in configuration"
#endif

#define _LED_COLOR_ID(_unused) 0,

#define _LED_COLOR_COUNT(id)					\
	static const uint8_t led_colors_##id[] = {		\
		DT_INST_FOREACH_CHILD(id, _LED_COLOR_ID)	\
	};

DT_INST_FOREACH_STATUS_OKAY(_LED_COLOR_COUNT)

#define _LED_INSTANCE_DEF(id)						\
	{								\
		.dev = DEVICE_DT_GET(DT_DRV_INST(id)),			\
		.color_count = ARRAY_SIZE(led_colors_##id),		\
	},
/**/

static struct led leds[] = {
	DT_INST_FOREACH_STATUS_OKAY(_LED_INSTANCE_DEF)
};
static void led_update(struct led *led);

// void test_led_work_cb(struct k_work *work);
// K_WORK_DELAYABLE_DEFINE(test_led_work, test_led_work_cb);
// void test_led_work_cb(struct k_work *work) {
// 		leds[4].effect = &led_peer_state_effect[LED_PEER_STATE_ON_100MS];
// 		led_update(&leds[4]);
// 		k_work_reschedule(&test_led_work, K_MSEC(3000));
// }

// uint8_t get_mode_status(void);
// static uint8_t last_mode;
// void mode_check_cb(struct k_timer *timer);
// K_TIMER_DEFINE(mode_check,mode_check_cb,NULL);
// void mode_check_cb(struct k_timer *timer)
// {
// 	uint8_t mode =get_mode_status();
// 	if(mode!=last_mode)
// 	{
// 		k_sem_give(&power_on_sem);
// 		LOG_INF("mode change:%d",mode);
// 		for (size_t i = 0; i < ARRAY_SIZE(leds); i++) 
// 			k_timer_stop(&leds[i].timer);
// 	}
// 	else
// 		k_timer_start(&mode_check,K_MSEC(100),K_FOREVER);
// }


static int set_color_one_channel(struct led *led, struct led_color *color)
{
	/* For a single color LED convert color to brightness. */
	unsigned int brightness = 0;

	for (size_t i = 0; i < ARRAY_SIZE(color->c); i++) {
		brightness += color->c[i];
	}
	brightness /= ARRAY_SIZE(color->c);

	return led_set_brightness(led->dev, 0, brightness);
}

static int set_color_all_channels(struct led *led, struct led_color *color)
{
	int err = 0;

	for (size_t i = 0; (i < ARRAY_SIZE(color->c)) && !err; i++) {
		err = led_set_brightness(led->dev, i, color->c[i]);
	}

	return err;
}

static void set_color(struct led *led, struct led_color *color)
{
	int err;
	// LOG_INF("color:%d,%d,%d",color->c[0],color->c[1],color->c[1]);
	if (led->color_count == ARRAY_SIZE(color->c)) {
		err = set_color_all_channels(led, color);
	} else {
		err = set_color_one_channel(led, color);
	}

	if (err) {
		LOG_ERR("Cannot set LED brightness (err: %d)", err);
	}
}

static void set_off(struct led *led)
{
	struct led_color nocolor = {0};

	set_color(led, &nocolor);
}

// static void work_handler(struct k_work *work)
static void timer_handler(struct k_timer *timer)
{
	// struct led *led = CONTAINER_OF(work, struct led, work);
	struct led *led = CONTAINER_OF(timer, struct led, timer);
	// LOG_INF("led ptr:%x,time:%d,%d",led,led->effect->steps[0].substep_time,led->effect->steps[1].substep_time);
	const struct led_effect_step *effect_step =
		&led->effect->steps[led->effect_step];

	__ASSERT_NO_MSG(effect_step->substep_count > 0);
	int substeps_left = effect_step->substep_count - led->effect_substep;

	for (size_t i = 0; i < ARRAY_SIZE(led->color.c); i++) {
		int diff = (effect_step->color.c[i] - led->color.c[i]) /
			substeps_left;
		led->color.c[i] += diff;
	}
	set_color(led, &led->color);

	led->effect_substep++;
	if (led->effect_substep == effect_step->substep_count) {
		led->effect_substep = 0;
		led->effect_step++;

		if (led->effect_step == led->effect->step_count) {
			if (led->effect->loop_forever) {
				led->effect_step = 0;
			} else {
				LOG_INF("led:%p effect finish",led);
				if((led == &leds[ARRAY_SIZE(leds)-1]) &&  (led->effect == &led_peer_state_effect[LED_PEER_STATE_POWER_ON]))
				{
					LOG_INF("power_on_finish");
					// k_sem_give(&power_on_sem);
					// k_timer_stop(&mode_check);
					led_power_on_status =0;
					k_work_submit(&power_on_finish);
				}
				
			}
		}
	}

	if (led->effect_step < led->effect->step_count) {
		int32_t next_delay =
			led->effect->steps[led->effect_step].substep_time;
		// LOG_INF("led ptr:%x,led->effect_step:%d,next delay:%d",led,led->effect_step,next_delay);
		// k_work_reschedule(&led->work, K_MSEC(next_delay));
			k_timer_start(&led->timer,K_MSEC(next_delay),K_FOREVER);
	}
}

static void led_update(struct led *led)
{
	// k_work_cancel_delayable(&led->work);
	k_timer_stop(&led->timer);

	led->effect_step = 0;
	led->effect_substep = 0;

	if (!led->effect) {
		LOG_DBG("No effect set");
		return;
	}

	__ASSERT_NO_MSG(led->effect->steps);

	if (led->effect->step_count > 0) {
		int32_t next_delay =
			led->effect->steps[led->effect_step].substep_time;
		LOG_INF("next delay:%d",next_delay);
		// k_work_reschedule(&led->work, K_MSEC(next_delay));
		k_timer_start(&led->timer,K_MSEC(next_delay),K_FOREVER);
	} else {
		LOG_WRN("LED effect with no effect");
	}
}
void print_led_info(const struct device *dev,uint32_t led);
 int leds_init(const struct device *_arg)
{
	int err = 0;

	BUILD_ASSERT(DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) > 0, "No LEDs defined");

	for (size_t i = 0; (i < ARRAY_SIZE(leds)) && !err; i++) {
		struct led *led = &leds[i];
		LOG_INF("led :%d,color count:%d",i,led->color_count);
		// print_led_info(led->dev,i);
		if(led->color_count>3) continue;
		__ASSERT_NO_MSG((led->color_count == 1) || (led->color_count == 3));
		
		if (!device_is_ready(led->dev)) {
			LOG_ERR("Device %s is not ready", led->dev->name);
			err = -ENODEV;
		} else {
			// k_work_init_delayable(&led->work, work_handler);
			k_timer_init(&led->timer,timer_handler,NULL);

			// led_set_state(i,LED_PEER_STATE_POWER_ON);
			// led->effect = &led_peer_state_effect[LED_PEER_STATE_POWER_ON];

			led_update(led);
		}
	}
	// k_work_reschedule(&test_led_work, K_MSEC(1000));
	return err;
}

 void leds_start(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
#if defined(CONFIG_PM_DEVICE) && !defined(CONFIG_ZMK_LEDS_GPIO)
		/* Zephyr power management API is not implemented for GPIO LEDs.
		 * LEDs are turned off by CAF LEDs module.
		 */
		int err = pm_device_action_run(leds[i].dev, PM_DEVICE_ACTION_RESUME);

		if (err) {
			LOG_ERR("Failed to set LED driver into active state (err: %d)", err);
		}
#endif
		led_update(&leds[i]);
	}
}

 void leds_stop(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
		// k_work_cancel_delayable(&leds[i].work);
		k_timer_stop(&leds[i].timer);

		set_off(&leds[i]);

#if defined(CONFIG_PM_DEVICE) && !defined(CONFIG_ZMK_LEDS_GPIO)
		/* Zephyr power management API is not implemented for GPIO LEDs.
		 * LEDs are turned off by CAF LEDs module.
		 */
		int err = pm_device_action_run(leds[i].dev, PM_DEVICE_ACTION_SUSPEND);

		if (err) {
			LOG_ERR("Failed to set LED driver into suspend state (err: %d)", err);
		}
#endif
	}
}



void led_set_state(uint8_t index,uint8_t led_state)
{
	LOG_INF("led_set_state index:%d,state:%d",index,led_state);
	if(index< ARRAY_SIZE(leds))
	{
		//why leds in dts is enable?
		for(int i=0;i<ARRAY_SIZE(leds);i++)
		{
			leds[i].effect = &led_peer_state_effect[LED_PEER_STATE_DISCONNECTED];
			led_update(&leds[i]);
		}
		leds[index].effect = &led_peer_state_effect[led_state];
		led_update(&leds[index]);
	}
}
#define LED_BLUE			0
#define LED_NUMLOCK 	1
#define LED_CAPSLOCK	2
#define LED_24G				3
#define LED_BAT 			4

// static  struct gpio_dt_spec led_num=GPIO_DT_SPEC_GET(DT_NODELABEL(led_numlock),numlock_led);
// static  struct gpio_dt_spec led_caps=GPIO_DT_SPEC_GET(DT_NODELABEL(led_capslock),caps_led);
// static  struct gpio_dt_spec led_blue=GPIO_DT_SPEC_GET(DT_NODELABEL(led_blue),blue_led);
// static  struct gpio_dt_spec led_24g=GPIO_DT_SPEC_GET(DT_NODELABEL(led_24g),led_24g);

// void keyboard_led_init(void)
// {
// 	if (gpio_is_ready_dt(&led_num)) {
// 			LOG_DBG("led num pin:%d",led_num.spec.pin);		
// 	}
// 	if (gpio_is_ready_dt(&led_caps)) {
// 			LOG_DBG("led caps pin:%d",led_caps.spec.pin);		
// 	}
// 	if (gpio_is_ready_dt(&led_blue)) {
// 			LOG_DBG("led blue pin:%d",led_blue.spec.pin);		
// 	}
// 	if (gpio_is_ready_dt(&led_24g)) {
// 			LOG_DBG("led 24g pin:%d",led_24g.spec.pin);		
// 	}

// }
#include <zephyr/drivers/led.h>
#include <zephyr/drivers/gpio.h>

struct led_gpio_config {
	size_t num_leds;
	const struct gpio_dt_spec *led;
};
void print_led_info(const struct device *dev,uint32_t led)
{
	const struct led_gpio_config *config = dev->config;
	const struct gpio_dt_spec *led_gpio;
	LOG_INF("LED NUM:%d,dev name:%s",config->num_leds,dev->name);

	for(int i=0;i<config->num_leds;i++)
	{
		led_gpio = &config->led[i];
		LOG_INF("LED gpio pin:%d",led_gpio->pin);
	}
	// keyboard_led_init();
}
static uint8_t keyboard_led_state;
static uint8_t charge_led_state;
void keyboad_led_set_onoff(uint8_t led_state)
{
	if(led_power_on_status)
	{
		keyboard_led_state =led_state;
		return;
	}
	uint8_t num =led_state &0x01;
	uint8_t caps= (led_state &0x02);
	struct led_color color=LED_COLOR(0xff,0xff,0xff);
	LOG_DBG("led state:%d,num:%d,caps:%d",led_state,num,caps);
#if !(CONFIG_SHIELD_KEYCHRON_B1)
	num ? set_color(&leds[LED_NUMLOCK],&color):set_off(&leds[LED_NUMLOCK]);
#endif 
	caps ? set_color(&leds[LED_CAPSLOCK],&color):set_off(&leds[LED_CAPSLOCK]);
	keyboard_led_state =led_state;

}
uint8_t keyboard_get_led_state(void)
{
	return keyboard_led_state;
}
uint8_t get_charge_led_state(void)
{
	return charge_led_state;
}
void blue_led_set_state(uint8_t led_state)
{
		if(get_current_transport()!=ZMK_TRANSPORT_BLE) return;
		LOG_DBG("set:%d",led_state);

		if((leds[LED_BLUE].effect !=&led_peer_state_effect[led_state]) || k_timer_remaining_get(&leds[LED_BLUE].timer)==0)// k_work_delayable_remaining_get(&leds[LED_BLUE].work)==0)
		{
			leds[LED_BLUE].effect = &led_peer_state_effect[led_state];
			led_update(&leds[LED_BLUE]);
		}
}
void led_24G_set_state(uint8_t led_state)
{
		if(get_current_transport()!=ZMK_TRANSPORT_24G) return;
		LOG_DBG("set:%d",led_state);
		if((leds[LED_24G].effect !=&led_peer_state_effect[led_state]) || k_timer_remaining_get(&leds[LED_24G].timer)==0)//k_work_delayable_remaining_get(&leds[LED_24G].work)==0)
		{
			LOG_DBG(".");
			leds[LED_24G].effect = &led_peer_state_effect[led_state];
			led_update(&leds[LED_24G]);
		}
}
void led_charge_set_state(uint8_t led_state)
{
	if(led_power_on_status)
	{
		charge_led_state =led_state;
		return;
	}
	if(led_state == LED_BAT_CHARGING)
	{
			leds[LED_BAT].effect = &led_peer_state_effect[LED_PEER_STATE_BAT_CHARGING];
			led_update(&leds[LED_BAT]);
	}
	else if(led_state == LED_BAT_CHARGE_DONE)
	{
			leds[LED_BAT].effect = &led_peer_state_effect[LED_PEER_STATE_BAT_CHARGEDONE];
			led_update(&leds[LED_BAT]);
	}
	else if(led_state == LED_BAT_LOW)
	{
			leds[LED_BAT].effect = &led_peer_state_effect[LED_PEER_STATE_BAT_LOW];
			led_update(&leds[LED_BAT]);
	}
	else if(led_state == LED_BAT_NONE)
	{
			leds[LED_BAT].effect = &led_peer_state_effect[LED_PEER_STATE_DISCONNECTED];
			led_update(&leds[LED_BAT]);
	}
	charge_led_state =led_state;
}
void led_recover(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(leds);i++)
	{
	#if (CONFIG_SHIELD_KEYCHRON_B1)
			if(i== LED_NUMLOCK) continue;
	#endif
			leds[i].effect = &led_peer_state_effect[LED_PEER_STATE_RECOVER];
			led_update(&leds[i]);
	}
}
void power_on_finish_cb(struct k_work * work)
{
	keyboad_led_set_onoff(keyboard_led_state);
	led_charge_set_state(charge_led_state);
}
bool bat_is_shutdown(void);
void led_power_on(void)
{
	if(bat_is_shutdown()) return;
	// k_sem_reset(&power_on_sem);
	led_power_on_status =1;

	for (size_t i = 0; i < ARRAY_SIZE(leds);i++)
	{
	#if (CONFIG_SHIELD_KEYCHRON_B1)
			if(i== LED_NUMLOCK) continue;
	#endif
			if(get_current_transport()==ZMK_TRANSPORT_BLE)
			{
				if(i==LED_24G) continue;
			}
			else if(get_current_transport()==ZMK_TRANSPORT_24G)
			{
				if(i==LED_BLUE) continue;
			}
			leds[i].effect = &led_peer_state_effect[LED_PEER_STATE_POWER_ON];

			led_update(&leds[i]);
	}
	// last_mode = get_mode_status();
	// k_timer_start(&mode_check,K_MSEC(100),K_FOREVER);
	// k_sem_take(&power_on_sem,K_MSEC(3000));
	// LOG_INF("power on finish");
	// led_charge_set_state(charge_led_state);
}
void reset_led_power_on(void)
{
	// k_sem_give(&power_on_sem);
	led_power_on_status=0;
	leds_stop();
}

static const struct led_effect * bat_bak_effect ;
extern uint8_t bt_bas_get_battery_level(void);
void led_bat_display(void)
{
	bat_bak_effect = leds[LED_BAT].effect;
	LOG_DBG(".");
	// led_charge_set_state(LED_BAT_NONE);

	// k_sem_take(&led_semi,K_MSEC(1000));

	uint8_t level =bt_bas_get_battery_level();
	
	if(level >=70)
	{
		struct led_color color=LED_COLOR(0,0xff,0); //green;
		set_color(&leds[LED_BAT],&color);
		LOG_DBG("green,level:%d",level);

	}
	else if(level >=30)
	{
		struct led_color color=LED_COLOR(0,0,0xff); //blue;
		set_color(&leds[LED_BAT],&color);
		LOG_DBG("blue,level:%d",level);
	}
	else
	{
		struct led_color color=LED_COLOR(0xff,0,0); //red
		set_color(&leds[LED_BAT],&color);
		LOG_DBG("red,level:%d",level);
	}
}
void led_bat_display_off(void)
{
	LOG_DBG(".");
	// if(bat_bak_effect)
	// {
	// 	leds[LED_BAT].effect = bat_bak_effect;
	// 	led_update(&leds[LED_BAT]);
	// }
	// else
	if(charge_led_state !=LED_BAT_LOW)
	{
		led_charge_set_state(charge_led_state);
	}
	else
	{
		led_charge_set_state(LED_BAT_NONE);
	}
}
void leds_turnoff(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(leds);i++)
	{
			set_off(&leds[i]);
	}
  led_charge_set_state(LED_BAT_NONE);
  blue_led_set_state( LED_PEER_STATE_DISCONNECTED);
  led_24G_set_state(LED_PEER_STATE_DISCONNECTED);
}

SYS_INIT(leds_init, APPLICATION, CONFIG_ZMK_LED_INIT_PRIORITY);