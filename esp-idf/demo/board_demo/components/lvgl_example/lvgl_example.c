/**
 * @file lvgl_example.c
 * @brief LVGL graphics library example implementation
 */

#include "lvgl_example.h"
#include "sdkconfig.h"

#ifdef CONFIG_LVGL_EXAMPLE_ENABLE

#include <stdio.h>
#include <unistd.h>
#include <sys/lock.h>
#include <sys/param.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "example_rgb_lcd_panel.h"

#include "ui.h"
#include "screens.h"
#include "driver/gpio.h"

#include <string.h>
#include "driver/i2c_master.h"
#include "esp_lcd_touch_gt911.h"

#include "demos/lv_demos.h"

// ========== 新增：定义触摸 I2C 引脚 ========== //
#define TOUCH_I2C_SCL_GPIO 51
#define TOUCH_I2C_SDA_GPIO 52
#define TOUCH_I2C_ADDR 0x5D // 根据你的实际地址调整（0x5D 或 0x14）

// ========================================== //

static const char *TAG = "lvgl_example";

// LVGL draw buffers must use the same pixel format as the RGB panel output.
#if CONFIG_EXAMPLE_LCD_DATA_LINES_16
#define EXAMPLE_LV_COLOR_FORMAT LV_COLOR_FORMAT_RGB565
#elif CONFIG_EXAMPLE_LCD_DATA_LINES_24
#define EXAMPLE_LV_COLOR_FORMAT LV_COLOR_FORMAT_RGB888
#else
#error "Unsupported LVGL color format"
#endif

// Number of display lines in each LVGL draw buffer
#define EXAMPLE_LVGL_DRAW_BUF_LINES 50

// LVGL timer tick period in milliseconds
#define EXAMPLE_LVGL_TICK_PERIOD_MS 2

// LVGL task stack size in bytes (converted from KB config)
#define EXAMPLE_LVGL_TASK_STACK_SIZE (CONFIG_LVGL_EXAMPLE_TASK_STACK_SIZE * 1024)

// LVGL task priority
#define EXAMPLE_LVGL_TASK_PRIORITY CONFIG_LVGL_EXAMPLE_TASK_PRIORITY

// Maximum sleep time (ms) between LVGL task iterations (upper bound)
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500

// Minimum sleep time (ms) between LVGL task iterations (lower bound)
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS (1000 / CONFIG_FREERTOS_HZ)

// LVGL is not thread-safe. In this example both app_main() and the LVGL task touch
// the LVGL object tree, so guard every LVGL call with the same lock.
static _lock_t lvgl_api_lock;
static TaskHandle_t lvgl_task_handle;

extern void example_lvgl_demo_ui(lv_display_t *disp);

#if CONFIG_EXAMPLE_USE_DOUBLE_FB
static bool example_on_frame_buf_complete(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *event_data, void *user_ctx)
{
    (void)panel;
    (void)event_data;
    (void)user_ctx;
    BaseType_t need_yield = pdFALSE;

    if (lvgl_task_handle)
    {
        vTaskNotifyGiveFromISR(lvgl_task_handle, &need_yield);
    }
    return need_yield == pdTRUE;
}

static void example_lvgl_flush_wait_cb(lv_display_t *disp)
{
    // In direct-mode double buffering, lv_display_flush_cb() only tells the driver
    // which frame buffer should be displayed next. LVGL must then wait until the
    // driver finishes switching buffers before it renders the next frame.
    if (lv_display_flush_is_last(disp))
    {
        // Wait until the previous frame buffer is no longer referenced by DMA.
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    lv_display_flush_ready(disp);
}
#else
static bool example_notify_lvgl_flush_ready(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *event_data, void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}
#endif

static void example_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
#if CONFIG_EXAMPLE_USE_DOUBLE_FB
    if (!lv_display_flush_is_last(disp))
    {
        // LVGL may split one frame into several dirty rectangles. In direct mode,
        // switch the hardware frame buffer only after the last rectangle is done.
        lv_display_flush_ready(disp);
        return;
    }
    offsetx1 = 0;
    offsety1 = 0;
    offsetx2 = EXAMPLE_LCD_H_RES - 1;
    offsety2 = EXAMPLE_LCD_V_RES - 1;
    // Clear any stale completion from the previous frame before waiting for this frame.
    ulTaskNotifyTake(pdTRUE, 0);
#endif
    // pass the draw buffer to the driver
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
}

static void example_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

static void example_lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t time_till_next_ms = 0;
    while (1)
    {
        // lv_timer_handler() runs animations, input handling, and screen refresh scheduling.
        _lock_acquire(&lvgl_api_lock);
        // 先更新 EEZ Flow 的状态机、事件和动画

#ifdef CONFIG_LVGL_EXAMPLE_UI_EEZ
        ui_tick();
#endif

        time_till_next_ms = lv_timer_handler();
        _lock_release(&lvgl_api_lock);
        // in case of task watch dog timeout
        time_till_next_ms = MAX(time_till_next_ms, EXAMPLE_LVGL_TASK_MIN_DELAY_MS);
        // in case of lvgl display not ready yet
        time_till_next_ms = MIN(time_till_next_ms, EXAMPLE_LVGL_TASK_MAX_DELAY_MS);
        usleep(1000 * time_till_next_ms);
    }
}

// ========== LVGL 触摸输入设备回调 ========== //

// ----- ADDED: 触摸点数据结构（若已全局定义则忽略） -----
typedef struct
{
    int16_t x;
    int16_t y;
    int16_t area;
} touch_point_t;

touch_point_t point[2] = {0}; // 用于弹性动画的触摸点数据

// ----- ADDED: 按钮弹性物理状态（静态变量） -----
static float btn_posX = 0, btn_velX = 0;
static float btn_posY = 0, btn_velY = 0;
const float STIFFNESS = 200.0f; // 弹性系数（可调）
const float DAMPING = 3.0f;     // 阻尼系数（可调）

static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)lv_indev_get_user_data(indev);
    esp_lcd_touch_read_data(tp);

    esp_lcd_touch_point_data_t points[5];
    uint8_t touch_cnt = 0;
    esp_lcd_touch_get_data(tp, points, &touch_cnt, 5);

    if (touch_cnt > 0)
    {
        ESP_LOGI("Touch", "Touch count: %d, x: %d, y: %d", touch_cnt, points[0].x, points[0].y);
        data->point.x = points[0].x;
        data->point.y = points[0].y;
        data->state = LV_INDEV_STATE_PRESSED;

        // 假设 objects.ui_MainScreen 是你的 Main 页面对象
        if (lv_screen_active() == objects.main)
        {
            // 当前在 Main 页面，执行弹性动画
            // ----- ADDED: 更新弹性动画的触摸源数据 -----
            point[0].x = points[0].x;
            point[0].y = points[0].y;
            point[0].area = 0; // 若无面积数据可忽略，或计算触摸区域大小

            // ----- ADDED: 对 button_main_1 进行弹性位置更新 -----
            // 获取当前按钮坐标（用于计算偏移，假设按钮锚点为中心）
            lv_obj_t *btn = objects.button_main_1; // 确保 objects 中有该成员
            lv_coord_t btn_w = lv_obj_get_width(btn);
            lv_coord_t btn_h = lv_obj_get_height(btn);

            // 目标位置（使按钮中心对准触摸点）
            float targetX = point[0].x - btn_w / 2.0f;
            float targetY = point[0].y - btn_h / 2.0f;

            // 物理模拟（固定步长 0.033s，与原有动画一致）
            const float dt = 0.033f;
            float forceX = STIFFNESS * (targetX - btn_posX) - DAMPING * btn_velX;
            btn_velX += forceX * dt;
            btn_posX += btn_velX * dt;

            float forceY = STIFFNESS * (targetY - btn_posY) - DAMPING * btn_velY;
            btn_velY += forceY * dt;
            btn_posY += btn_velY * dt;

            // 应用位置
            lv_obj_set_pos(btn, (lv_coord_t)btn_posX, (lv_coord_t)btn_posY);
        }

        // ------------------------------------------------
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED;
        // ----- ADDED: 可选：释放后归位或保留最后位置 -----
        // 可在此处重置速度或让按钮自然停止（当前物理阻尼会使其减速）
    }
}
// ============================================ //

/**
 * @brief Initialize the LVGL example
 */
void lvgl_example_init(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "LVGL Example");

    // Keep the backlight off while the RGB panel and LVGL are being configured.
    ESP_ERROR_CHECK(example_rgb_lcd_backlight_init());
    ESP_LOGI(TAG, "Initialize LCD backlight");
    ESP_LOGI(TAG, "Turn off LCD backlight");
    example_rgb_lcd_backlight_set(false);

    // Create the RGB panel object from the GPIO/timing configuration in
    // example_rgb_lcd_panel.{h,c}, then reset and start the hardware.
    ESP_LOGI(TAG, "Create RGB LCD panel");
    esp_lcd_panel_handle_t panel_handle = NULL;
    ESP_ERROR_CHECK(example_rgb_lcd_panel_new(&panel_handle));

    ESP_LOGI(TAG, "Initialize RGB LCD panel");
    ESP_ERROR_CHECK(example_rgb_lcd_panel_init(panel_handle));

    ESP_LOGI(TAG, "Turn on LCD backlight");
    example_rgb_lcd_backlight_set(true);

    // Create one LVGL display that uses the RGB panel as its flush target.
    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();
    lv_display_t *display = lv_display_create(EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);
    lv_display_set_user_data(display, panel_handle);
    lv_display_set_color_format(display, EXAMPLE_LV_COLOR_FORMAT);
    void *buf1 = NULL;
    void *buf2 = NULL;
#if CONFIG_EXAMPLE_USE_DOUBLE_FB
    ESP_LOGI(TAG, "Use frame buffers as LVGL draw buffers");
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &buf1, &buf2));
    // Direct mode lets LVGL render straight into the hardware frame buffers.
    lv_display_set_buffers(display, buf1, buf2, EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * EXAMPLE_PIXEL_SIZE, LV_DISPLAY_RENDER_MODE_DIRECT);
#else
    ESP_LOGI(TAG, "Allocate LVGL draw buffers");
    // Partial mode uses a smaller draw buffer and copies only the dirty area to the panel.
    // Allocate this buffer from internal RAM for better DMA and CPU access performance.
    size_t draw_buffer_sz = EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_DRAW_BUF_LINES * EXAMPLE_PIXEL_SIZE;
    buf1 = esp_lcd_rgb_alloc_draw_buffer(panel_handle, draw_buffer_sz, 0);
    assert(buf1);
    lv_display_set_buffers(display, buf1, buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
#endif // CONFIG_EXAMPLE_USE_DOUBLE_FB

    // Connect LVGL's flush path to the RGB panel driver.
    lv_display_set_flush_cb(display, example_lvgl_flush_cb);
#if CONFIG_EXAMPLE_USE_DOUBLE_FB
    lv_display_set_flush_wait_cb(display, example_lvgl_flush_wait_cb);
#endif

    ESP_LOGI(TAG, "Register event callbacks");
    esp_lcd_rgb_panel_event_callbacks_t cbs = {
#if CONFIG_EXAMPLE_USE_DOUBLE_FB
        // Signal LVGL when the panel has switched to the new frame buffer.
        .on_frame_buf_complete = example_on_frame_buf_complete,
#else
        // Signal LVGL when the driver finishes copying the dirty area.
        .on_color_trans_done = example_notify_lvgl_flush_ready,
#endif
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &cbs, display));

    ESP_LOGI(TAG, "Install LVGL tick timer");
    // Feed LVGL with a periodic tick so it can keep time for animations and timers.
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"};
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

    ESP_LOGI(TAG, "Create LVGL task");
    xTaskCreate(example_lvgl_port_task, "LVGL", EXAMPLE_LVGL_TASK_STACK_SIZE, NULL, EXAMPLE_LVGL_TASK_PRIORITY, &lvgl_task_handle);
    // Build the demo UI once the display pipeline is ready.
    ESP_LOGI(TAG, "Display LVGL UI");
    _lock_acquire(&lvgl_api_lock);

#ifdef CONFIG_LVGL_EXAMPLE_UI_EEZ
    ui_init(); 
#elif CONFIG_LVGL_EXAMPLE_UI_IDF
    example_lvgl_demo_ui(display);
#elif CONFIG_LVGL_EXAMPLE_UI_LVGL_DEMO
    #if LV_USE_DEMO_WIDGETS
        lv_demo_widgets();
    #elif LV_USE_DEMO_SMARTWATCH
        lv_demo_smartwatch();
    #elif LV_USE_DEMO_EBIKE
        lv_demo_ebike();
    #elif LV_USE_DEMO_BENCHMARK
        lv_demo_benchmark();
    #elif LV_USE_DEMO_FLEX_LAYOUT
        lv_demo_flex_layout();
    #elif LV_USE_DEMO_MUSIC
        lv_demo_music();
    #else
        example_lvgl_demo_ui(display);
    #endif  /* LV_USE_DEMO_* */
#endif  /* CONFIG_LVGL_EXAMPLE_UI_* */



    _lock_release(&lvgl_api_lock);

    // ========== 新增：初始化 I2C 总线和触摸 ========== //
    // 1. 创建 I2C 总线
    i2c_master_bus_config_t i2c_bus_cfg;
    memset(&i2c_bus_cfg, 0, sizeof(i2c_bus_cfg));
    i2c_bus_cfg.i2c_port = I2C_NUM_0;
    i2c_bus_cfg.sda_io_num = (gpio_num_t)TOUCH_I2C_SDA_GPIO;
    i2c_bus_cfg.scl_io_num = (gpio_num_t)TOUCH_I2C_SCL_GPIO;
    i2c_bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_bus_cfg.glitch_ignore_cnt = 7;
    i2c_bus_cfg.flags.enable_internal_pullup = true;
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &bus_handle));

    // 2. 探测 GT911 地址
    uint8_t addr_list[] = {0x5D, 0x14};
    uint8_t address = 0x5D; // 默认
    for (int i = 0; i < 2; i++)
    {
        esp_err_t err = i2c_master_probe(bus_handle, addr_list[i], 100);
        if (err == ESP_OK)
        {
            address = addr_list[i];
            ESP_LOGI(TAG, "GT911 found at address 0x%02X", address);
            break;
        }
    }
    if (address == 0)
    {
        ESP_LOGE(TAG, "GT911 not found!");
        return;
    }

    // 3. 手动初始化 panel IO（完全匹配官方宏）
    esp_lcd_panel_io_i2c_config_t io_config;
    memset(&io_config, 0, sizeof(io_config));
    io_config.dev_addr = address;              // 自动探测到的地址（0x5D/0x14）
    io_config.scl_speed_hz = 100000;           // 官方宏采用 100kHz
    io_config.control_phase_bytes = 1;         // 同宏
    io_config.dc_bit_offset = 0;               // 同宏
    io_config.lcd_cmd_bits = 16;               // ← 关键！之前误写为 8
    io_config.lcd_param_bits = 0;              // 未定义，清零
    io_config.flags.disable_control_phase = 1; // 同宏
    io_config.flags.dc_low_on_data = 0;        // 默认
    io_config.transaction_timeout_ms = 100;
    io_config.on_color_trans_done = NULL;
    io_config.user_ctx = NULL;

    esp_lcd_panel_io_handle_t io_handle;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus_handle, &io_config, &io_handle));

    // 4. 配置触摸参数
    esp_lcd_touch_io_gt911_config_t tp_gt911_config;
    memset(&tp_gt911_config, 0, sizeof(tp_gt911_config));
    tp_gt911_config.dev_addr = address;

    esp_lcd_touch_config_t tp_cfg;
    memset(&tp_cfg, 0, sizeof(tp_cfg));
    tp_cfg.x_max = 800; // 根据实际分辨率修改
    tp_cfg.y_max = 480;
    tp_cfg.rst_gpio_num = GPIO_NUM_NC;
    tp_cfg.int_gpio_num = GPIO_NUM_NC;
    tp_cfg.levels.reset = 0;
    tp_cfg.levels.interrupt = 0;
    tp_cfg.flags.swap_xy = false;
    tp_cfg.flags.mirror_x = false;
    tp_cfg.flags.mirror_y = false;
    tp_cfg.process_coordinates = NULL;
    tp_cfg.driver_data = &tp_gt911_config;

    esp_lcd_touch_handle_t tp;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(io_handle, &tp_cfg, &tp));
    ESP_LOGI(TAG, "Touch initialized");

    // ============================================ //

    // ========== 注册 LVGL 输入设备 ========== //
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvgl_touch_read_cb);
    lv_indev_set_user_data(indev, tp);
    // ======================================== //

    // 删除原先的 xTaskCreate(touch_poll_task, ...)

    // Configure GPIO for LVGL demo indicator
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_LVGL_EXAMPLE_GPIO_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(CONFIG_LVGL_EXAMPLE_GPIO_PIN, 1);
    ESP_LOGI(TAG, "GPIO %d set to HIGH", CONFIG_LVGL_EXAMPLE_GPIO_PIN);

    ESP_LOGI(TAG, "========================================");

    // Placeholder: LVGL initialization will be implemented here

    ESP_LOGI(TAG, "LVGL example initialized successfully");
}

#endif /* CONFIG_LVGL_EXAMPLE_ENABLE */