#ifndef __LEDS_H__
#define __LEDS_H__
// int leds_init(void);
void leds_start(void);
void leds_stop(void);
void blue_led_set_state(uint8_t led_state);
void keyboad_led_set_onoff(uint8_t led_state);
void led_24G_set_state(uint8_t led_state);
void led_charge_set_state(uint8_t led_state);
void led_recover(void);
void led_bat_display(void);
void led_bat_display_off(void);
uint8_t get_charge_led_state(void);
enum {
	LED_PEER_STATE_DISCONNECTED,
	LED_PEER_STATE_CONNECTED,
	LED_PEER_STATE_PAIR,
	LED_PEER_STATE_RECONN,
	LED_PEER_STATE_FAILE,
	LED_PEER_STATE_POWER_ON,
	LED_PEER_STATE_ON_100MS,
	LED_PEER_STATE_BAT_LOW,
	LED_PEER_STATE_BAT_CHARGING,
	LED_PEER_STATE_BAT_CHARGEDONE,
	LED_PEER_STATE_RECOVER,
};
enum {
	LED_BAT_NONE,
	LED_BAT_CHARGING,
	LED_BAT_CHARGE_DONE,
	LED_BAT_LOW,
	LED_BAT_SHUT_DOWN,
};
#endif 