/* Board demo main file
 *
 * This file is part of the ESP32-S31-PI development board example.
 *
 * It demonstrates the basic functionality of the board, including LED blinking.
 *
 * The code initializes the board, configures the LED, and enters a loop where it blinks the LED at a specified interval.
 *
 * The blink period can be configured via the CONFIG_BLINK_PERIOD macro in sdkconfig.
 *
 * The example uses FreeRTOS for task management and timing functions.
 *


*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "esp_timer.h"
#include "blink.h"

static const char *TAG = "example";

extern "C" void app_main(void)
{
    configure_led();

    ESP_LOGI(TAG, "ESP32-S31-PI development board example initialized"); // 添加这行
    while (1)
    {

        esp_timer_get_time();
        blink_led();
        vTaskDelay(CONFIG_BLINK_PERIOD / portTICK_PERIOD_MS);
    }
}
