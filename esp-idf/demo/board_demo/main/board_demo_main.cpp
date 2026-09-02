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
#include "button_input_example.h"
#include "sdmmc_example.h"
#include "usb_msc_example.h"
#include "eth_example.h"

static const char *TAG = "example";

extern "C" void app_main(void)
{
    configure_led();
    button_input_example_init();
    sdmmc_example_init();
    usb_msc_example_init();
    eth_example_init();

    ESP_LOGI(TAG, "ESP32-S31-PI development board example initialized");
    while (1)
    {
        uint64_t time_ms = esp_timer_get_time() / 1000;
        static uint64_t last_time_ms = 0;
        if (time_ms - last_time_ms >= CONFIG_BLINK_PERIOD)
        {
            last_time_ms = time_ms;
            blink_led();
        }

        usb_msc_example_run(); // Monitor and process USB MSC events (connect/disconnect)

        // Feed the task watchdog by yielding CPU to IDLE task
        // Without this, IDLE task cannot feed the watchdog, causing WDT reset
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
