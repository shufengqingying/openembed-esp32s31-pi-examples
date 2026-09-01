/**
 * @file button_input_example.c
 * @brief Button input example with interrupt, debounce, short press and long press detection
 *
 * This module demonstrates GPIO input handling with the following features:
 *
 * 1. GPIO Configuration:
 *    - Configures the specified GPIO pin as input with no pull-up/pull-down resistors
 *    - Falling edge interrupt is enabled for button press detection
 *
 * 2. Interrupt Service Routine (ISR):
 *    - Immediately disables the GPIO interrupt to prevent further triggers during debounce
 *    - Records the interrupt timestamp and sets flags to notify the task
 *    - Lightweight ISR design (only sets flags, no heavy processing)
 *
 * 3. Button Test Task (10ms polling):
 *    a. Short Press Handling:
 *       - Detects interrupt flag set by ISR
 *       - Applies 40ms hardware debounce delay
 *       - Reads and confirms the button state
 *       - Logs "Button pressed!" message
 *       - Re-enables the interrupt after debounce
 *
 *    b. Long Press Handling:
 *       - Continuously monitors button state (level == 0 means pressed)
 *       - Calculates press duration from recorded interrupt timestamp
 *       - When duration exceeds CONFIG_BUTTON_INPUT_EXAMPLE_LONG_PRESS_MS threshold:
 *         - Logs "Long press detected!" once
 *       - When button is released after a long press:
 *         - Logs "Button released after long press"
 *         - Logs the total long press duration in milliseconds
 *
 * 4. Configuration Options (via menuconfig):
 *    - CONFIG_BUTTON_INPUT_EXAMPLE_ENABLE: Enable/disable the component
 *    - CONFIG_BUTTON_INPUT_EXAMPLE_GPIO: GPIO number for the button
 *    - CONFIG_BUTTON_INPUT_EXAMPLE_TASK_PRIORITY: Task priority (1-25)
 *    - CONFIG_BUTTON_INPUT_EXAMPLE_TASK_STACK_SIZE: Task stack size in bytes
 *    - CONFIG_BUTTON_INPUT_EXAMPLE_LONG_PRESS_MS: Long press threshold (100-3000ms)
 *
 * 5. Debounce Mechanism:
 *    - Hardware: Interrupt is disabled immediately in ISR, re-enabled after processing
 *    - Software: 40ms delay before confirming button state
 *    - Effective against mechanical switch bouncing
 */

#include "button_input_example.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_intr_alloc.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#ifdef CONFIG_BUTTON_INPUT_EXAMPLE_ENABLE

static const char *TAG = "button_example";

// Button interrupt flag (volatile to prevent compiler optimization)
static volatile bool s_button_interrupt_log = false;
static volatile bool s_button_interrupt_flag = false;
static volatile uint64_t s_button_interrupt_time = 0;

// Task handle
static TaskHandle_t s_button_test_task_handle = NULL;

/**
 * @brief GPIO interrupt service routine
 *
 * Disables the interrupt immediately and sets the flag to notify the task.
 */
static void IRAM_ATTR button_isr_handler(void *arg)
{
    // Disable interrupt to prevent further triggers during debounce
    gpio_intr_disable(CONFIG_BUTTON_INPUT_EXAMPLE_GPIO);

    // Set the interrupt flag
    s_button_interrupt_log = true;
    s_button_interrupt_flag = true;
    s_button_interrupt_time = esp_timer_get_time() / 1000; // Convert to milliseconds
}

/**
 * @brief Button test task
 *
 * Polls the interrupt flag every 10 ms. When the flag is set, it prints
 * a log message and re-enables the interrupt.
 */
static void button_test_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Button test task started");

    while (1)
    {
        if (s_button_interrupt_log)
        {

            // Delay 40ms for hardware debounce
            vTaskDelay(pdMS_TO_TICKS(40));

            // Read current pin level to confirm button state
            int level = gpio_get_level(CONFIG_BUTTON_INPUT_EXAMPLE_GPIO);

            // Log a valid button press event
            ESP_LOGI(TAG, "Button pressed! GPIO level: %d", level);

            // Clear the interrupt flag
            s_button_interrupt_log = false;

            // Re-enable the interrupt after logging (debounce)
            gpio_intr_enable(CONFIG_BUTTON_INPUT_EXAMPLE_GPIO);
            ESP_LOGD(TAG, "Interrupt re-enabled after debounce");
        }

        uint64_t current_time = esp_timer_get_time() / 1000; // Convert to milliseconds

        int level = gpio_get_level(CONFIG_BUTTON_INPUT_EXAMPLE_GPIO);

        static bool long_press_detected = false;
        static bool long_press_logged = false;
        if (level == 0 && current_time - s_button_interrupt_time >= CONFIG_BUTTON_INPUT_EXAMPLE_LONG_PRESS_MS)
        {
            // Log long press event only once
            if (long_press_logged == true)
            {
                ESP_LOGI(TAG, "Long press detected!");
                long_press_logged = false; // Reset the log flag after logging
            }
            long_press_detected = true;
        }
        else if (level == 1 && long_press_detected)
        {
            ESP_LOGI(TAG, "Button released after long press");
            long_press_detected = false;
            long_press_logged = true; // Set the log flag to true after logging

            uint64_t long_press_duration = current_time - s_button_interrupt_time;
            ESP_LOGI(TAG, "Long press duration: %llu ms", long_press_duration);
        }

        // Poll every 10 ms
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief Initialize the button input example
 *
 * Configures GPIO, installs ISR service, adds the interrupt handler,
 * enables the interrupt, and creates the test task.
 */
void button_input_example_init(void)
{
    ESP_LOGI(TAG, "Initializing button input example...");

    // Configure GPIO as input with no pull resistors, falling edge interrupt
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_BUTTON_INPUT_EXAMPLE_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&io_conf);
    ESP_LOGI(TAG, "GPIO %d configured as input, no pull, falling edge interrupt",
             CONFIG_BUTTON_INPUT_EXAMPLE_GPIO);

    // Install GPIO ISR service
    gpio_install_isr_service(0);
    ESP_LOGI(TAG, "GPIO ISR service installed");

    // Add GPIO interrupt handler
    gpio_isr_handler_add(CONFIG_BUTTON_INPUT_EXAMPLE_GPIO,
                         button_isr_handler,
                         NULL);
    ESP_LOGI(TAG, "GPIO ISR handler added");

    // Enable interrupt by default
    gpio_intr_enable(CONFIG_BUTTON_INPUT_EXAMPLE_GPIO);
    ESP_LOGI(TAG, "GPIO interrupt enabled");

    // Create the button test task
    xTaskCreate(
        button_test_task,
        "button_test_task",
        CONFIG_BUTTON_INPUT_EXAMPLE_TASK_STACK_SIZE,
        NULL,
        CONFIG_BUTTON_INPUT_EXAMPLE_TASK_PRIORITY,
        &s_button_test_task_handle);

    if (s_button_test_task_handle != NULL)
    {
        ESP_LOGI(TAG, "Button test task created successfully");
        ESP_LOGI(TAG, "  Priority: %d", CONFIG_BUTTON_INPUT_EXAMPLE_TASK_PRIORITY);
        ESP_LOGI(TAG, "  Stack size: %d bytes", CONFIG_BUTTON_INPUT_EXAMPLE_TASK_STACK_SIZE);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to create button test task");
    }

    // Print complete configuration summary
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Button Input Example Configuration:");
    ESP_LOGI(TAG, "  GPIO Pin:            GPIO_%d", CONFIG_BUTTON_INPUT_EXAMPLE_GPIO);
    ESP_LOGI(TAG, "  Pin Mode:            Input");
    ESP_LOGI(TAG, "  Pull-up Resistor:    Disabled");
    ESP_LOGI(TAG, "  Pull-down Resistor:  Disabled");
    ESP_LOGI(TAG, "  Interrupt Type:      Falling Edge");
    ESP_LOGI(TAG, "  Interrupt Priority:  Default (Level 1)");
    ESP_LOGI(TAG, "  Debounce Time:       40 ms");
    ESP_LOGI(TAG, "  Long Press Threshold:%d ms", CONFIG_BUTTON_INPUT_EXAMPLE_LONG_PRESS_MS);
    ESP_LOGI(TAG, "  Task Priority:       %d", CONFIG_BUTTON_INPUT_EXAMPLE_TASK_PRIORITY);
    ESP_LOGI(TAG, "  Task Stack Size:     %d bytes", CONFIG_BUTTON_INPUT_EXAMPLE_TASK_STACK_SIZE);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Usage: Press the BOOT button (GPIO_%d) to test", CONFIG_BUTTON_INPUT_EXAMPLE_GPIO);
    ESP_LOGI(TAG, "       Short press:  Logs 'Button pressed!'");
    ESP_LOGI(TAG, "       Long press:   Logs 'Long press detected!' and duration");
    ESP_LOGI(TAG, "Hint: You can change the GPIO pin number in menuconfig:");
    ESP_LOGI(TAG, "      Component config -> Button Input Example Configuration");
    ESP_LOGI(TAG, "========================================");

    ESP_LOGI(TAG, "Button input example initialized successfully");
}

#endif /* CONFIG_BUTTON_INPUT_EXAMPLE_ENABLE */