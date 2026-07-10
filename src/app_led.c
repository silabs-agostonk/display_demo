#include "app_led.h"

#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define LED_NODE DT_ALIAS(led0)

#if !DT_NODE_EXISTS(LED_NODE)
#error "Define devicetree alias app-led"
#endif

#define BLINK_SLOW_MS 500
#define BLINK_FAST_MS 100

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

static struct k_timer blink_timer;
static bool led_state;

static void blink_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	led_state = !led_state;
	gpio_pin_set_dt(&led, led_state);
}

int app_led_init(void)
{
	if (!gpio_is_ready_dt(&led)) {
		return -ENODEV;
	}

	int ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (ret) {
		return ret;
	}

	k_timer_init(&blink_timer, blink_timer_handler, NULL);
	return 0;
}

int app_led_on(void)
{
	k_timer_stop(&blink_timer);

	led_state = true;
	return gpio_pin_set_dt(&led, 1);
}

int app_led_off(void)
{
	k_timer_stop(&blink_timer);

	led_state = false;
	return gpio_pin_set_dt(&led, 0);
}

static int app_led_blink(uint32_t period_ms)
{
	led_state = false;
	gpio_pin_set_dt(&led, 0);

	k_timer_start(&blink_timer, K_MSEC(period_ms), K_MSEC(period_ms));
	return 0;
}

int app_led_blink_slow(void)
{
	return app_led_blink(BLINK_SLOW_MS);
}

int app_led_blink_fast(void)
{
	return app_led_blink(BLINK_FAST_MS);
}