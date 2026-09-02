// 项目头文件
#include "blink_example.h"
// 功能头文件
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "led_strip.h"

static const char *TAG = "example";

/* Use project configuration menu (idf.py menuconfig) to choose the GPIO to blink,
   or you can edit the following line and set a number here.
*/
#define BLINK_GPIO CONFIG_BLINK_GPIO


#ifdef CONFIG_BLINK_LED_STRIP

static led_strip_handle_t led_strip;

void blink_led(void)
{
    static int color_idx = 0;
    uint8_t r = 0, g = 0, b = 0;
    switch (color_idx % 3)
    {
    case 0:
        r = 80; // 红色，亮度值80
        break;
    case 1:
        g = 80; // 绿色，亮度值80
        break;
    case 2:
        b = 80; // 蓝色，亮度值80
        break;
    }
    led_strip_set_pixel(led_strip, 0, r, g, b);
    led_strip_refresh(led_strip);
    color_idx++;
    ESP_LOGI(TAG, "LED Strip -> R:%d, G:%d, B:%d", r, g, b);
}

void configure_led(void)
{
    ESP_LOGI(TAG, "Example configured to blink addressable LED!");
    /* LED strip initialization with the GPIO and pixels number*/
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1, // at least one LED on board
    };
#if CONFIG_BLINK_LED_STRIP_BACKEND_RMT
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
#elif CONFIG_BLINK_LED_STRIP_BACKEND_SPI
    led_strip_spi_config_t spi_config = {
        .spi_bus = SPI2_HOST,
        .flags.with_dma = true,
    };
    ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_config, &spi_config, &led_strip));
#else
// #warning "LED strip backend not enabled, skipping configuration"
#endif
    /* Set all LED off to clear all pixels */
    led_strip_clear(led_strip);
}

#elif CONFIG_BLINK_LED_GPIO

static uint8_t s_led_state = 0;


void blink_led(void)
{
    /* Set the GPIO level according to the state (LOW or HIGH)*/
    s_led_state = !s_led_state; // 翻转状态
    gpio_set_level(BLINK_GPIO, s_led_state);
    ESP_LOGI(TAG, "Turning the LED %s!", s_led_state == true ? "ON" : "OFF");
}

void configure_led(void)
{
    ESP_LOGI(TAG, "Example configured to blink GPIO LED!");
    gpio_reset_pin(BLINK_GPIO);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
}

#else
// #warning "LED strip backend not enabled, skipping configuration"
#endif
