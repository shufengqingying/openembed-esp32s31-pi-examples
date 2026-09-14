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

#include <stdint.h>
#include <math.h> // 仅用于 fabsf，若不使用数学库，可自行替换为条件判断

// 文件顶部
static lv_indev_t *g_indev_touch1 = NULL;
static lv_indev_t *g_indev_touch2 = NULL;

// ========== 新增：定义触摸 I2C 引脚 ========== //
#define TOUCH_I2C_SCL_GPIO 51
#define TOUCH_I2C_SDA_GPIO 52
#define TOUCH_I2C_ADDR 0x5D // 根据你的实际地址调整（0x5D 或 0x14）

typedef struct
{
    int16_t x;
    int16_t y;
    // 测试数据：触摸强度在10-70之间为正常
    int16_t area;
    bool updated; // true 表示数据已更新，可读取
} touch_point_t;
touch_point_t touch_points[5] = {0};

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

/**
 * @brief 一维弹簧动画状态机 (像素单位制, 质量 m=1)
 * @note  支持负坐标 (int16_t)，无任何除法操作，绝对安全无除零
 */
typedef struct
{
    int16_t current_pos; // 当前帧位置 (像素) —— 由算法自动更新
    int16_t target_pos;  // 目标位置 (像素) —— 由外部触摸事件更新
    float stiffness;     // 弹性系数 (1/s²，调参决定软硬)
    float damping;       // 阻尼系数 (1/s，调参决定阻力)
    float dt;            // 帧间隔时间 (秒，固定如 0.033f)
    float velocity;      // 当前速度 (像素/秒) —— 由算法自动更新
} Spring1D_State_t;
/**
 * @brief   初始化弹簧状态机 (传入全部初始参数)
 * @param   state   - 指向结构体的指针 (不能为 NULL)
 * @param   cur     - 初始当前位置 (像素)
 * @param   tgt     - 初始目标位置 (像素)
 * @param   stiff   - 弹性系数 (建议范围 50~500)
 * @param   damp    - 阻尼系数 (建议范围 1~20)
 * @param   dt      - 固定时间步长 (如 0.033f)
 * @param   vel     - 初始速度 (通常为 0.0f)
 * @note    dt 必须大于 0，否则函数会将其强制设为 0.001f 防止死循环
 */
void Spring1D_Init(Spring1D_State_t *state,
                   int16_t cur,
                   int16_t tgt,
                   float stiff,
                   float damp,
                   float dt,
                   float vel)
{
    if (state == NULL)
        return; // 防御性检查

    state->current_pos = cur;
    state->target_pos = tgt;
    state->stiffness = stiff;
    state->damping = damp;
    state->velocity = vel;

    // 安全保护：如果传入了非法时间步长，自动修正
    if (dt <= 0.0f)
    {
        dt = 0.001f; // 极小值避免物理停滞
    }
    state->dt = dt;
}
/**
 * @brief   执行一步弹性物理模拟，输出下一帧位置
 * @param   state - 指向已初始化的结构体指针
 * @return  int16_t - 计算出的下一帧位置 (像素)
 * @note    1. 自动更新 state->current_pos 和 state->velocity 供下帧使用
 *          2. 本算法不含任何除法，绝不产生除零错误
 *          3. 输出结果自动限幅在 int16_t 范围内 (-32768 ~ 32767)
 */
int16_t Spring1D_Step(Spring1D_State_t *state)
{
    // ============================================================
    // 安全校验（防御性编程）
    // ============================================================
    if (state == NULL)
        return 0;
    if (state->dt <= 0.0f)
        return state->current_pos; // 时间停滞则原地不动

    // ============================================================
    // 类型提升：int16_t -> float (防止整数截断)
    // ============================================================
    float cur = (float)(state->current_pos);
    float tgt = (float)(state->target_pos);
    float v = state->velocity;
    float k = state->stiffness;
    float c = state->damping;
    float dt = state->dt;

    // ============================================================
    // 第 1 步：弹簧拉力产生的加速度贡献 (胡克定律)
    // 公式: a_spring = k * (目标 - 当前)
    // ============================================================
    float a_spring = k * (tgt - cur);

    // ============================================================
    // 第 2 步：阻尼阻力产生的加速度削减 (粘性阻尼)
    // 公式: a_damping = -c * v
    // ============================================================
    float a_damping = -c * v;

    // ============================================================
    // 第 3 步：合加速度 (质量 m=1，无除法，绝对安全)
    // 公式: a = a_spring + a_damping
    // ============================================================
    float a = a_spring + a_damping;

    // ============================================================
    // 第 4 步：更新速度 (冲量定理)
    // 公式: v_new = v + a * dt
    // ============================================================
    float v_new = v + a * dt;

    // ============================================================
    // 第 5 步：更新位置 (输出下一帧点)
    // 公式: x_new = cur + v_new * dt (使用新速度，半隐式欧拉法更稳定)
    // ============================================================
    float new_pos_f = cur + v_new * dt;

    // ============================================================
    // 第 6 步：状态持久化 (自动写回结构体，供下帧使用)
    // ============================================================
    state->velocity = v_new;                 // 保存新速度
    state->current_pos = (int16_t)new_pos_f; // 自动更新当前位置

    // ============================================================
    // 第 7 步：安全限幅并返回 (防止超出 int16_t 范围)
    // ============================================================
    if (new_pos_f < -32768.0f)
        return -32768;
    if (new_pos_f > 32767.0f)
        return 32767;

    return (int16_t)new_pos_f;
}

#include <stdint.h>
#include <math.h>

/**
 * @brief 二维矢量运动状态机（带惯性减速，各向同性摩擦）
 * @note  位置和速度均为有符号浮点数，减速过程保持方向不变
 *        速度衰减公式：a = friction + viscous * speed，速率线性/指数混合衰减
 */
typedef struct
{
    float pos_x;    // 当前位置 X (像素)
    float pos_y;    // 当前位置 Y (像素)
    float vel_x;    // 当前速度 X (像素/秒，有符号)
    float vel_y;    // 当前速度 Y (像素/秒，有符号)
    float friction; // 恒摩擦减速度 (像素/秒²，可设为0)
    float viscous;  // 粘性阻尼系数 (1/秒，可设为0)
    float dt;       // 固定时间步长 (秒)
} VectorMotion2D_t;

/**
 * @brief 初始化二维矢量运动状态机
 * @param state      - 指向结构体的指针
 * @param pos_x, pos_y - 初始位置
 * @param vel_x, vel_y - 初始速度（有符号）
 * @param friction   - 恒摩擦减速度（≥0，允许为0）
 * @param viscous    - 粘性阻尼系数（≥0，允许为0）
 * @param dt         - 固定时间步长（>0）
 */
void VectorMotion2D_Init(VectorMotion2D_t *state,
                         float pos_x, float pos_y,
                         float vel_x, float vel_y,
                         float friction, float viscous,
                         float dt)
{
    if (state == NULL)
        return;
    state->pos_x = pos_x;
    state->pos_y = pos_y;
    state->vel_x = vel_x;
    state->vel_y = vel_y;
    state->friction = (friction < 0.0f) ? 0.0f : friction;
    state->viscous = (viscous < 0.0f) ? 0.0f : viscous;
    state->dt = (dt <= 0.0f) ? 0.001f : dt;
}

/**
 * @brief 执行一步物理模拟（减速 + 位移），自动更新结构体内部状态
 * @param state - 已初始化的状态机指针
 * @param out_x - 可选输出指针，返回更新后的X坐标（若为NULL则忽略）
 * @param out_y - 可选输出指针，返回更新后的Y坐标（若为NULL则忽略）
 * @note  1. 若摩擦和阻尼均为0，物体匀速直线运动（永不停止）
 *        2. 方向在运动过程中保持绝对不变
 *        3. 速度过零时精确归零，无微振
 */
void VectorMotion2D_Step(VectorMotion2D_t *state, int16_t *out_x, int16_t *out_y)
{
    if (state == NULL)
        return;
    if (state->dt <= 0.0f)
        return;

    float vx = state->vel_x;
    float vy = state->vel_y;
    float dt = state->dt;
    float A = state->friction;
    float c = state->viscous;

    // ============================================================
    // 1. 计算合速率与单位方向
    // ============================================================
    float speed = sqrtf(vx * vx + vy * vy);
    if (speed < 1e-12f)
    {
        // 速度为零，静止不动
        state->vel_x = 0.0f;
        state->vel_y = 0.0f;
        // 位置不变
        if (out_x)
            *out_x = (int16_t)state->pos_x;
        if (out_y)
            *out_y = (int16_t)state->pos_y;
        return;
    }

    float dir_x = vx / speed; // 单位方向 X
    float dir_y = vy / speed; // 单位方向 Y

    // ============================================================
    // 2. 标量速度衰减（核心耗散公式）
    // ============================================================
    float decel = A + c * speed; // 合减速度大小
    float speed_new = speed - decel * dt;

    if (speed_new < 0.0f)
    {
        // 精确停止：速度归零，位置不变（因为这一帧内已经停了）
        state->vel_x = 0.0f;
        state->vel_y = 0.0f;
        // 位置停留在停止点（不更新，因为停止距离已包含在当前位置中）
        if (out_x)
            *out_x = (int16_t)state->pos_x;
        if (out_y)
            *out_y = (int16_t)state->pos_y;
        return;
    }

    // ============================================================
    // 3. 反推新速度矢量（方向不变）
    // ============================================================
    float new_vx = speed_new * dir_x;
    float new_vy = speed_new * dir_y;

    // ============================================================
    // 4. 更新状态
    // ============================================================
    state->vel_x = new_vx;
    state->vel_y = new_vy;
    state->pos_x += new_vx * dt;
    state->pos_y += new_vy * dt;

    // ============================================================
    // 5. 输出（如果需要）
    // ============================================================
    if (out_x)
        *out_x = (int16_t)state->pos_x;
    if (out_y)
        *out_y = (int16_t)state->pos_y;
}

// touch_mode.h 或本文件顶部
typedef enum
{
    TOUCH_MODE_NONE = 0, // 对应索引 0
    TOUCH_MODE_SINGLE,   // 对应索引 1
    TOUCH_MODE_MULTI,    // 对应索引 2
    TOUCH_MODE_GESTURE,  // 对应索引 3
    TOUCH_MODE_MAX
} touch_mode_t;
// 全局变量，记录当前触摸模式
touch_mode_t g_touch_mode = TOUCH_MODE_NONE;

static void touch_dropdown_event_cb(lv_event_t *e)
{
    (void)e; // 防止编译警告

    // 获取下拉框对象
    lv_obj_t *dropdown = objects.ui_dropdown_touch_1;

    // 获取当前选中选项的索引（官方推荐方式）[reference:2]
    uint16_t selected_idx = lv_dropdown_get_selected(dropdown);

    // 获取各个容器对象
    lv_obj_t *cont1 = objects.ui_container_touch_1;
    lv_obj_t *cont2 = objects.ui_container_touch_2;
    lv_obj_t *cont3 = objects.ui_container_touch_3;
    lv_obj_t *cont4 = objects.ui_container_touch_4;
    lv_obj_t *cont5 = objects.ui_container_touch_5;
    lv_obj_t *cont6 = objects.ui_container_touch_6;
    lv_obj_t *cont7 = objects.ui_img_star_1;

    // 根据选中选项的索引执行显示/隐藏
    // 假设下拉列表选项顺序为：0: "Single", 1: "Multi", 2: "Gesture"
    switch (selected_idx)
    {
    case 0: // "None"
        // 隐藏 1~5
        lv_obj_add_flag(cont1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cont2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cont3, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cont4, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cont5, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cont6, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cont7, LV_OBJ_FLAG_HIDDEN);
        g_touch_mode = TOUCH_MODE_NONE;
        break;
    case 1: // "Single"
        // 隐藏 2~5，显示 1
        lv_obj_add_flag(cont2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cont3, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cont4, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cont5, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(cont1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cont6, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cont7, LV_OBJ_FLAG_HIDDEN);
        g_touch_mode = TOUCH_MODE_SINGLE;
        break;

    case 2: // "Multi"
        // 取消隐藏所有容器
        lv_obj_remove_flag(cont1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(cont2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(cont3, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(cont4, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(cont5, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cont6, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cont7, LV_OBJ_FLAG_HIDDEN);
        g_touch_mode = TOUCH_MODE_MULTI;
        break;

    case 3: // "Gesture"
        // 全部隐藏 1~5
        lv_obj_add_flag(cont1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cont2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cont3, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cont4, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cont5, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(cont6, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(cont7, LV_OBJ_FLAG_HIDDEN);
        g_touch_mode = TOUCH_MODE_GESTURE;
        break;

    default:
        g_touch_mode = TOUCH_MODE_NONE;
        break;
    }
}

// 模式枚举：当前激活的物理引擎
typedef enum
{
    MODE_ELASTIC = 0, // 触摸跟随模式（弹簧）
    MODE_INERTIA = 1  // 触摸释放后惯性滑动模式
} AnimMode_t;

static AnimMode_t current_mode = MODE_ELASTIC; // 初始默认弹性

Spring1D_State_t spring_x, spring_y;
// 惯性滑动状态机（X轴和Y轴）
VectorMotion2D_t inertia_2d;

// ============================================================
// 边界碰撞参数 (恢复系数)
// 1.0 = 完全弹性 (永不停止) ，0.8 = 轻微能量损失 ，0.0 = 完全吸收 (黏住)
// ============================================================
#define BOUNDARY_RESTITUTION 1.0f

void physics_init(void)
{
    // 弹性参数：STIFFNESS=200, DAMPING=3, dt=0.033
    Spring1D_Init(&spring_x, 0, 0, 200.0f, 3.0f, 0.033f, 0.0f);
    Spring1D_Init(&spring_y, 0, 0, 200.0f, 3.0f, 0.033f, 0.0f);

    // 2D 惯性状态机初始化
    VectorMotion2D_Init(&inertia_2d,
                        0.0f,   // pos_x
                        0.0f,   // pos_y
                        0.0f,   // vel_x
                        0.0f,   // vel_y
                        500.0f, // friction (恒摩擦减速度)
                        0.0f,   // viscous (粘性阻尼，暂设为0)
                        0.033f  // dt
    );
}

/**
 * @brief 更新UI上显示的触摸点坐标和面积信息
 * @note  依赖全局触摸点数组 touch_points 和 UI 对象 objects
 */
static void update_touch_point_labels(void)
{
    // 更新坐标点的信息
    char touch_info[50];

    // 触摸点1
    snprintf(touch_info, sizeof(touch_info), "1  :  x%d  y%d  s%d",
             touch_points[0].x, touch_points[0].y, touch_points[0].area);
    lv_label_set_text(objects.ui_label_touch_1, touch_info);

    // 触摸点2
    snprintf(touch_info, sizeof(touch_info), "2  :  x%d  y%d  s%d",
             touch_points[1].x, touch_points[1].y, touch_points[1].area);
    lv_label_set_text(objects.ui_label_touch_2, touch_info);

    // 触摸点3
    snprintf(touch_info, sizeof(touch_info), "3  :  x%d  y%d  s%d",
             touch_points[2].x, touch_points[2].y, touch_points[2].area);
    lv_label_set_text(objects.ui_label_touch_3, touch_info);

    // 触摸点4
    snprintf(touch_info, sizeof(touch_info), "4  :  x%d  y%d  s%d",
             touch_points[3].x, touch_points[3].y, touch_points[3].area);
    lv_label_set_text(objects.ui_label_touch_4, touch_info);

    // 触摸点5
    snprintf(touch_info, sizeof(touch_info), "5  :  x%d  y%d  s%d",
             touch_points[4].x, touch_points[4].y, touch_points[4].area);
    lv_label_set_text(objects.ui_label_touch_5, touch_info);
}

/**
 * @brief 多点触摸模式下，将5个容器的位置设置为对应触摸点的坐标（中心对齐）
 * @note  依赖全局触摸点数组 touch_points 和 UI 对象 objects
 *        触摸点坐标有效时（area > 0 且 updated 为真）容器可见并定位，否则隐藏
 */
void set_multi_touch_positions(void)
{
    uint16_t show_count = 0; // 显示多少帧后隐藏

    // 获取5个容器对象（按顺序对应触摸点索引0~4）
    lv_obj_t *containers[5] = {
        objects.ui_container_touch_1,
        objects.ui_container_touch_2,
        objects.ui_container_touch_3,
        objects.ui_container_touch_4,
        objects.ui_container_touch_5};

    for (int i = 0; i < 5; i++)
    {
        // 判断该触摸点是否有效：updated 标记且面积大于0（代表真实触摸）
        if (touch_points[i].updated && touch_points[i].area > 0)
        {
            // 获取容器尺寸，计算中心偏移
            lv_coord_t w = lv_obj_get_width(containers[i]);
            lv_coord_t h = lv_obj_get_height(containers[i]);
            lv_coord_t x = touch_points[i].x - w / 2;
            lv_coord_t y = touch_points[i].y - h / 2;

            // 设置位置，并确保容器可见
            lv_obj_set_pos(containers[i], x, y);
            lv_obj_remove_flag(containers[i], LV_OBJ_FLAG_HIDDEN);

            // 清除本次更新标志，以免下一帧重复处理（下一帧触摸回调会重新置位）
            touch_points[i].updated = false;

            show_count = 45; // 显示一定帧后隐藏
        }
        else
        {
            if (show_count > 0)
            {
                show_count--;
            }
            else
            {
                // 无效触摸点，隐藏容器
                lv_obj_add_flag(containers[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

/**
 * @brief 把手势目标结构体的值应用到星星图片
 * @note  在动画任务中调用，避免在手势回调里直接操作对象
 */
static void gesture_apply_target();

/**
 * @brief 每帧调用的动画更新函数（整合弹性 + 矢量惯性）
 * @note  触摸激活时使用弹性跟随，触摸释放后使用矢量惯性滑动（方向保持）
 */
void animation(void)
{
    // 仅当触摸测试界面激活时处理
    if (lv_screen_active() != objects.touch_test)
    {
        return;
    }

    update_touch_point_labels(); // 更新标签

    // 只处理单点模式（TOUCH_MODE_SINGLE）
    if (g_touch_mode == TOUCH_MODE_SINGLE)
    {

        // ============================================================
        // 获取当前控件指针及尺寸
        // ============================================================
        lv_obj_t *pointer = objects.ui_container_touch_1;
        lv_coord_t btn_w = lv_obj_get_width(pointer);
        lv_coord_t btn_h = lv_obj_get_height(pointer);
        lv_coord_t screen_w = lv_disp_get_hor_res(NULL);
        lv_coord_t screen_h = lv_disp_get_ver_res(NULL);
        lv_coord_t max_x = screen_w - btn_w;
        lv_coord_t max_y = screen_h - btn_h;

        bool touch_active = touch_points[0].updated; // 触摸点0是否有新数据

        // ============================================================
        // 情况 1：触摸激活 → 弹性模式
        // ============================================================
        if (touch_active)
        {
            // 如果当前不是弹性模式，从惯性切换过来（同步数据）
            if (current_mode != MODE_ELASTIC)
            {
                // 将惯性状态复制到弹性结构体
                spring_x.current_pos = (int16_t)inertia_2d.pos_x;
                spring_y.current_pos = (int16_t)inertia_2d.pos_y;
                spring_x.velocity = inertia_2d.vel_x;
                spring_y.velocity = inertia_2d.vel_y;
                current_mode = MODE_ELASTIC;
            }

            // 更新弹性目标点为触摸位置（偏移中心）
            spring_x.target_pos = touch_points[0].x - btn_w / 2;
            spring_y.target_pos = touch_points[0].y - btn_h / 2;

            // 执行弹性步进（双轴独立）
            Spring1D_Step(&spring_x);
            Spring1D_Step(&spring_y);

            // --- 弹性边界碰撞（夹紧 + 速度反向） ---
            // X轴
            if (spring_x.current_pos <= 0)
            {
                spring_x.current_pos = 0;
                spring_x.velocity = -BOUNDARY_RESTITUTION * spring_x.velocity;
            }
            else if (spring_x.current_pos >= max_x)
            {
                spring_x.current_pos = max_x;
                spring_x.velocity = -BOUNDARY_RESTITUTION * spring_x.velocity;
            }
            // Y轴
            if (spring_y.current_pos <= 0)
            {
                spring_y.current_pos = 0;
                spring_y.velocity = -BOUNDARY_RESTITUTION * spring_y.velocity;
            }
            else if (spring_y.current_pos >= max_y)
            {
                spring_y.current_pos = max_y;
                spring_y.velocity = -BOUNDARY_RESTITUTION * spring_y.velocity;
            }

            // 应用位置
            lv_obj_set_pos(pointer, spring_x.current_pos, spring_y.current_pos);

            // 清除触摸更新标志
            touch_points[0].updated = false;
        }

        // ============================================================
        // 情况 2：触摸未激活 → 惯性滑动模式
        // ============================================================
        else
        {
            // 检查当前是否有残余速度（从弹性结构体读取）
            float speed_x, speed_y;
            if (current_mode == MODE_ELASTIC)
            {
                speed_x = spring_x.velocity;
                speed_y = spring_y.velocity;
            }
            else
            {
                speed_x = inertia_2d.vel_x;
                speed_y = inertia_2d.vel_y;
            }
            float speed = sqrtf(speed_x * speed_x + speed_y * speed_y);

            if (speed < 0.001f)
            {
                // 速度几乎为零，不运动，但若当前是惯性模式且速度已归零，可保持静止
                // 但为了确保位置准确，我们仍可保持当前位置不变
                return;
            }

            // 如果有速度，切换到惯性模式（若尚未切换）
            if (current_mode != MODE_INERTIA)
            {
                inertia_2d.pos_x = (float)spring_x.current_pos;
                inertia_2d.pos_y = (float)spring_y.current_pos;
                inertia_2d.vel_x = spring_x.velocity;
                inertia_2d.vel_y = spring_y.velocity;
                current_mode = MODE_INERTIA;
            }

            // 执行惯性步进（自动更新 pos/vel）
            VectorMotion2D_Step(&inertia_2d, NULL, NULL); // 我们直接访问结构体成员

            // --- 惯性边界碰撞（夹紧 + 速度矢量反向） ---
            bool collided = false;
            // X轴边界
            if (inertia_2d.pos_x <= 0.0f)
            {
                inertia_2d.pos_x = 0.0f;
                inertia_2d.vel_x = -BOUNDARY_RESTITUTION * inertia_2d.vel_x;
                collided = true;
            }
            else if (inertia_2d.pos_x >= (float)max_x)
            {
                inertia_2d.pos_x = (float)max_x;
                inertia_2d.vel_x = -BOUNDARY_RESTITUTION * inertia_2d.vel_x;
                collided = true;
            }
            // Y轴边界
            if (inertia_2d.pos_y <= 0.0f)
            {
                inertia_2d.pos_y = 0.0f;
                inertia_2d.vel_y = -BOUNDARY_RESTITUTION * inertia_2d.vel_y;
                collided = true;
            }
            else if (inertia_2d.pos_y >= (float)max_y)
            {
                inertia_2d.pos_y = (float)max_y;
                inertia_2d.vel_y = -BOUNDARY_RESTITUTION * inertia_2d.vel_y;
                collided = true;
            }

            // 如果发生了碰撞，需要重新计算合速度方向？实际上，碰撞后速度方向已经改变，
            // 但下一次调用 VectorMotion2D_Step 时会自动使用新的 vel_x/vel_y 计算方向，所以无需额外操作。

            // 应用位置（转换为 int16_t）
            lv_obj_set_pos(pointer, (int16_t)inertia_2d.pos_x, (int16_t)inertia_2d.pos_y);
        }
    }
    else if (g_touch_mode == TOUCH_MODE_MULTI)
    {
        // 执行多点，不过动画耗费资源，这里直接设置坐标点
        set_multi_touch_positions();
    }
    else if (g_touch_mode == TOUCH_MODE_GESTURE)
    {
        // 手势模式下，应用目标结构体到星星
        gesture_apply_target();
    }
}

// ==== 新增：动画任务配置参数 ====
#define EXAMPLE_LVGL_ANIM_TASK_STACK_SIZE (6 * 1024) // 6KB
#define EXAMPLE_LVGL_ANIM_TASK_PRIORITY 1
#define EXAMPLE_LVGL_ANIM_TASK_PERIOD_MS 33

// ==== 动画任务函数 ====
static void example_lvgl_anim_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL Animation task");
    // 获取当前时间作为基准
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (1)
    {
        // 先获取锁
        _lock_acquire(&lvgl_api_lock);

        // 如果动画开关开启，则执行动画逻辑
        animation();

        // 释放锁
        _lock_release(&lvgl_api_lock);

        // 33ms 周期运行（使用精准延时）
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(EXAMPLE_LVGL_ANIM_TASK_PERIOD_MS));
    }
}

// ==== 创建动画任务函数 ====
void example_lvgl_anim_task_create(void)
{
    // 建议绑定在 CPU 0，与 CPU 1 的 LVGL 主任务并行，避免互相争抢 CPU
    // 如果担心 CPU 0 还有其他外围任务，可以去掉最后一个参数，让系统自动调度
    xTaskCreatePinnedToCore(
        example_lvgl_anim_task, // 任务函数
        "LVGL_Anim",            // 任务名称
        EXAMPLE_LVGL_ANIM_TASK_STACK_SIZE,
        NULL, // 任务参数
        EXAMPLE_LVGL_ANIM_TASK_PRIORITY,
        NULL, // 任务句柄（如不需要可传 NULL）
        1     // 绑定到 CPU 1
    );
}

/**
 * @brief 双指事件共享数据（设备1 写入，设备2 只读）
 */
typedef struct
{
    int16_t base_x1, base_y1;             /**< 双指起始基点（手指1） */
    int16_t base_x2, base_y2;             /**< 双指起始基点（手指2） */
    int16_t cur_x1, cur_y1;               /**< 双指当前坐标（手指1） */
    int16_t cur_x2, cur_y2;               /**< 双指当前坐标（手指2） */
    bool in_double_touch;                 /**< 是否处于双指事件中 */
    lv_indev_gesture_type_t peer_gesture; /**< 设备1 当前手势类型 */
} gesture_double_touch_t;

static gesture_double_touch_t g_double_touch = {
    .base_x1 = 0,
    .base_y1 = 0,
    .base_x2 = 0,
    .base_y2 = 0,
    .cur_x1 = 0,
    .cur_y1 = 0,
    .cur_x2 = 0,
    .cur_y2 = 0,
    .in_double_touch = false,
};

static void lvgl_touch1_read_cb(lv_indev_t *g_indev_touch1, lv_indev_data_t *data)
{
    // 获取注册时存入的底层 ESP LCD Touch 驱动的句柄
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)lv_indev_get_user_data(g_indev_touch1);
    // 向芯片发送读取命令，获取最新触摸数据
    esp_lcd_touch_read_data(tp);
    // 用于储存触摸点数据的数组
    esp_lcd_touch_point_data_t points[5];
    // 获取触摸点数量
    uint8_t touch_cnt = 0;
    // 获取触摸点数据，最多读取5个点
    esp_lcd_touch_get_data(tp, points, &touch_cnt, 5);

    // ===== NEW =====
    // 手势识别固定追踪两个触摸点，每帧都传 2 个条目
    lv_indev_touch_data_t gesture_touches[2];

    // 第 0 个点：touch_cnt >= 1 时存在
    if (touch_cnt - 1 >= 0)
    {
        gesture_touches[0].point.x = points[0].x;
        gesture_touches[0].point.y = points[0].y;
        gesture_touches[0].state = LV_INDEV_STATE_PRESSED;
    }
    else
    {
        gesture_touches[0].state = LV_INDEV_STATE_RELEASED;
    }
    gesture_touches[0].id = 0;

    // 第 1 个点：touch_cnt >= 2 时存在
    if (touch_cnt - 2 >= 0)
    {
        gesture_touches[1].point.x = points[1].x;
        gesture_touches[1].point.y = points[1].y;
        gesture_touches[1].state = LV_INDEV_STATE_PRESSED;
    }
    else
    {
        gesture_touches[1].state = LV_INDEV_STATE_RELEASED;
    }
    gesture_touches[1].id = 1;

    // ===== 双指事件共享数据更新（供设备2使用） =====
    if (touch_cnt >= 2)
    {
        // 首次进入双指事件 → 记录基点
        if (!g_double_touch.in_double_touch)
        {
            g_double_touch.base_x1 = points[0].x;
            g_double_touch.base_y1 = points[0].y;
            g_double_touch.base_x2 = points[1].x;
            g_double_touch.base_y2 = points[1].y;
            g_double_touch.in_double_touch = true;

            // 显示两条辅助线
            lv_obj_remove_flag(objects.ui_line_touch_1, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(objects.ui_line_touch_2, LV_OBJ_FLAG_HIDDEN);
            // 双指基点中点
            int16_t base_mid_x = (g_double_touch.base_x1 + g_double_touch.base_x2) / 2;
            int16_t base_mid_y = (g_double_touch.base_y1 + g_double_touch.base_y2) / 2;

            // 设置到基点中点，各自偏移让线居中
            lv_obj_set_pos(objects.ui_line_touch_1,
                           base_mid_x - 20,
                           base_mid_y);
            lv_obj_set_pos(objects.ui_line_touch_2,
                           base_mid_x,
                           base_mid_y - 20);
        }

        // 每帧持续更新当前坐标
        g_double_touch.cur_x1 = points[0].x;
        g_double_touch.cur_y1 = points[0].y;
        g_double_touch.cur_x2 = points[1].x;
        g_double_touch.cur_y2 = points[1].y;
    }
    else
    {
        // 不足双指 → 退出双指事件
        g_double_touch.in_double_touch = false;

        // 隐藏两条辅助线
        lv_obj_add_flag(objects.ui_line_touch_1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(objects.ui_line_touch_2, LV_OBJ_FLAG_HIDDEN);
    }

    // 始终传入 2 个条目（PRESSED 或 RELEASED 混合）
    lv_indev_gesture_recognizers_update(g_indev_touch1, gesture_touches, 2);
    // 将识别结果（如有手势）填入 lv_indev_data_t
    lv_indev_gesture_recognizers_set_data(g_indev_touch1, data);

    // ===== 截获设备1 循环获取当前手势触发的类型 =====
    lv_indev_gesture_type_t g1_type = LV_INDEV_GESTURE_NONE;
    for (int i = 0; i < LV_INDEV_GESTURE_CNT; i++)
    {
        if (data->gesture_type[i] != LV_INDEV_GESTURE_NONE)
        {
            g1_type = data->gesture_type[i];
            break;
        }
    }
    g_double_touch.peer_gesture = g1_type; // 写进共享结构体

    // ===== END NEW =====

    // 如果获取的触摸点大于0，视为按下状态，否则为释放状态
    if (touch_cnt > 0)
    {
        for (int i = 0; i < touch_cnt && i < 5; i++)
        {
            // 存入全局触摸点
            touch_points[i].x = points[i].x;
            touch_points[i].y = points[i].y;
            touch_points[i].area = points[i].strength; // 使用真实强度数据
            touch_points[i].updated = true;            // 标记数据已更新

            // 打印坐标与强度
            // ESP_LOGI("Touch", "Point %d: x=%d, y=%d, strength=%d", i, touch_points[i].x, touch_points[i].y, touch_points[i].area);
        }

        // 默认使用第0个触摸点传入LVGL设备
        data->point.x = touch_points[0].x;
        data->point.y = touch_points[0].y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// 需要单独识别滑动事件产生时的另一个坐标轴对应的滑动事件
static void lvgl_touch2_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    // 记录多点事件开始和结束状态
    static bool state_in_begin = false;

    // ===== 1. 直接判断设备1 是否处于双指事件 和滑动事件=====
    if (g_double_touch.in_double_touch && g_double_touch.peer_gesture == LV_INDEV_GESTURE_TWO_FINGERS_SWIPE)
    {

        // 如果事件未被记录
        if (!state_in_begin)
        {
            // 基点由设备1 记录，设备2 直接复用
            // 标记状态
            state_in_begin = true;
            // 提示 label
            lv_label_set_text(objects.ui_label_touch_8, "indev2: BEGIN");
        }

        // 原始两指中点（全部来自共享结构体）
        int16_t base_mid_x = (g_double_touch.base_x1 + g_double_touch.base_x2) / 2;
        int16_t base_mid_y = (g_double_touch.base_y1 + g_double_touch.base_y2) / 2;
        int16_t mid_x = (g_double_touch.cur_x1 + g_double_touch.cur_x2) / 2;
        int16_t mid_y = (g_double_touch.cur_y1 + g_double_touch.cur_y2) / 2;

        // 中点相对基点的位移
        int16_t dx = mid_x - base_mid_x;
        int16_t dy = mid_y - base_mid_y;
        int16_t abs_dx = (dx >= 0) ? dx : -dx;
        int16_t abs_dy = (dy >= 0) ? dy : -dy;

        lv_indev_touch_data_t gesture_touches[2];
        if (abs_dx >= abs_dy)
        {
            // 主轴 X，副轴 Y → 虚拟点 = 基点 + (0, dy)
            gesture_touches[0].point.x = g_double_touch.base_x1;
            gesture_touches[0].point.y = g_double_touch.base_y1 + dy;
            gesture_touches[1].point.x = g_double_touch.base_x2;
            gesture_touches[1].point.y = g_double_touch.base_y2 + dy;
        }
        else
        {
            // 主轴 Y，副轴 X → 虚拟点 = 基点 + (dx, 0)
            gesture_touches[0].point.x = g_double_touch.base_x1 + dx;
            gesture_touches[0].point.y = g_double_touch.base_y1;
            gesture_touches[1].point.x = g_double_touch.base_x2 + dx;
            gesture_touches[1].point.y = g_double_touch.base_y2;
        }

        gesture_touches[0].state = LV_INDEV_STATE_PRESSED;
        gesture_touches[1].state = LV_INDEV_STATE_PRESSED;
        gesture_touches[0].id = 0;
        gesture_touches[1].id = 1;

        lv_indev_gesture_recognizers_update(indev, gesture_touches, 2);
        lv_indev_gesture_recognizers_set_data(indev, data);

        // 每 30 帧打印一次两个映射后的点
        static uint32_t map_log_cnt = 0;
        if ((map_log_cnt++ % 30) == 0)
        {
            ESP_LOGI("Touch2", "p0=(%d,%d) p1=(%d,%d)",
                     gesture_touches[0].point.x, gesture_touches[0].point.y,
                     gesture_touches[1].point.x, gesture_touches[1].point.y);
        }

        // ===== 4. 普通触摸：用原始坐标点 0 =====
        data->point.x = g_double_touch.cur_x1;
        data->point.y = g_double_touch.cur_y1;
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else
    {
        // 不满足双点条件 → 两个触摸点视为放开
        lv_indev_touch_data_t gesture_touches[2];
        for (int i = 0; i < 2; i++)
        {
            gesture_touches[i].point.x = 0;
            gesture_touches[i].point.y = 0;
            gesture_touches[i].state = LV_INDEV_STATE_RELEASED;
            gesture_touches[i].id = i;
        }
        lv_indev_gesture_recognizers_update(indev, gesture_touches, 2);
        lv_indev_gesture_recognizers_set_data(indev, data);

        // 重置事件
        state_in_begin = false;

        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/**
 * @brief 手势控制目标：一组"期望状态"
 * @note  这些是"用户希望图片达到的状态"，不是"当前正在显示的状态"
 *        可以由任意源（手势、按钮、网络指令）写入，由应用任务统一应用到图片
 */
typedef struct
{
    int16_t target_x;     // 目标位置 X（相对父容器）
    int16_t target_y;     // 目标位置 Y（相对父容器）
    int16_t base_x;       // 滑动基点 X
    int16_t base_y;       // 滑动基点 Y
    float target_rot_deg; // 目标旋转角度（度，正负皆可，可超 360）
    float base_rot_deg;   // 旋转基点（手势开始时固化）
    float target_scale;   // 目标缩放倍数（0.5 ~ 1.5）
    float base_scale;     // 缩放基点
} gesture_target_t;

// 星星的"期望状态"实例
static gesture_target_t g_star_target;

/**
 * @brief 初始化手势目标结构体
 * @param target       指向待初始化的结构体（不能为 NULL）
 * @param init_x       初始 X 坐标
 * @param init_y       初始 Y 坐标
 * @param init_rot_deg 初始旋转角度（度）
 * @param init_scale   初始缩放倍数
 * @note  同时设置 target 和 base，保证手势第一次进入时行为一致
 *        缩放会限幅到 [0.5, 1.5]，防止传入非法值
 */
static void gesture_target_init(gesture_target_t *target,
                                int16_t init_x,
                                int16_t init_y,
                                float init_rot_deg,
                                float init_scale)
{
    if (target == NULL)
        return;

    // 缩放安全限幅
    if (init_scale < 0.5f)
        init_scale = 0.5f;
    if (init_scale > 1.5f)
        init_scale = 1.5f;

    // 位置：target 和 base 同步
    target->target_x = init_x;
    target->target_y = init_y;
    target->base_x = init_x;
    target->base_y = init_y;

    // 旋转：target 和 base 同步
    target->target_rot_deg = init_rot_deg;
    target->base_rot_deg = init_rot_deg;

    // 缩放：target 和 base 同步
    target->target_scale = init_scale;
    target->base_scale = init_scale;
}

// 动画函数
static void gesture_apply_target(void)
{
    lv_obj_t *star = objects.ui_img_star_1;
    if (star == NULL)
        return;

    // 位置
    lv_obj_set_pos(star, g_star_target.target_x, g_star_target.target_y);

    // 旋转（度 → 0.1 度单位）
    int32_t angle_deg_10 = (int32_t)(g_star_target.target_rot_deg * 10.0f);
    lv_image_set_rotation(star, angle_deg_10);

    // 缩放（倍数 → 256 基数单位）
    int32_t scale_int = (int32_t)(g_star_target.target_scale * 256.0f);
    lv_image_set_scale(star, scale_int);
}

// ===== XY 轴锁：NULL = 空闲，否则指向持有该锁的输入设备 =====
typedef struct
{
    lv_indev_t *holder;    // 谁持有该轴锁（NULL = 空闲）
    lv_indev_t *requester; // 谁正在请求该轴锁（用于协商交换）
    bool need_swap;        // 是否有交换请求待处理
} axis_lock_t;

static axis_lock_t g_lock_x = {NULL, NULL, false};
static axis_lock_t g_lock_y = {NULL, NULL, false};

// ===== 坐标变基辅助变量（手势期间使用） =====
static int16_t g_real_axis_x = 0; /**< 实时坐标轴 X 偏移（手势累积位移） */
static int16_t g_real_axis_y = 0; /**< 实时坐标轴 Y 偏移 */

static void ui_tileview_touch_frame_1_gesture_cb(lv_event_t *e)
{

    lv_obj_t *star = objects.ui_img_star_1;
    if (star == NULL)
        return;

    // 获取手势类型和状态
    lv_indev_gesture_type_t gesture_type = lv_event_get_gesture_type(e);
    lv_indev_gesture_state_t gesture_state = lv_event_get_gesture_state(e, gesture_type);

    // ===== 设备过滤：y 设备只处理滑动事件和 ENDED 状态 =====
    if (lv_event_get_indev(e) == g_indev_touch2)
    {
        bool is_swipe = (gesture_type == LV_INDEV_GESTURE_TWO_FINGERS_SWIPE);
        bool is_ended = (gesture_state == LV_INDEV_GESTURE_STATE_ENDED);
        if (!is_swipe && !is_ended)
        {
            return; // y 设备不处理其它手势
        }
    }

    // 当前任意一个手势结束时触发
    if (gesture_state == LV_INDEV_GESTURE_STATE_ENDED)
    {
        // ===== 手势结束 → 清空所有轴锁 =====
        g_lock_x.holder = NULL;
        g_lock_x.requester = NULL;
        g_lock_x.need_swap = false;

        g_lock_y.holder = NULL;
        g_lock_y.requester = NULL;
        g_lock_y.need_swap = false;

        // 设备1的放开状态（设备2已在上面处理过）
        if (lv_event_get_indev(e) == g_indev_touch1)
        {
            lv_label_set_text(objects.ui_label_touch_7, "indev1: RELEASED");
        }
        else if (lv_event_get_indev(e) == g_indev_touch2)
        {
            lv_label_set_text(objects.ui_label_touch_8, "indev2: RELEASED");
        }
        // label10/11 复位
        lv_label_set_text(objects.ui_label_touch_10, "Msg1:");
        lv_label_set_text(objects.ui_label_touch_11, "Msg2:");

        // 旋转/缩放基点固化
        g_star_target.base_rot_deg = g_star_target.target_rot_deg;
        g_star_target.base_scale = g_star_target.target_scale;

        // 隐藏准星辅助线
        lv_obj_add_flag(objects.ui_line_touch_3, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(objects.ui_line_touch_4, LV_OBJ_FLAG_HIDDEN);

        return;
    }

    // 开始识别手势
    switch (gesture_type)
    {
        // 双指滑动时
    case LV_INDEV_GESTURE_TWO_FINGERS_SWIPE:
    {

        int32_t dist = lv_event_get_two_fingers_swipe_distance(e);
        lv_dir_t dir = lv_event_get_two_fingers_swipe_dir(e);

        // ===== 轴锁抢占 / 双向交换 =====
        lv_indev_t *who = lv_event_get_indev(e); // 获取当前触发设备
        bool is_x_axis = (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT);
        bool is_y_axis = (dir == LV_DIR_TOP || dir == LV_DIR_BOTTOM);
        if (!is_x_axis && !is_y_axis)
            return;

        axis_lock_t *my_lock = is_x_axis ? &g_lock_x : &g_lock_y;    // 记录当前触发轴对应的结构体
        axis_lock_t *other_lock = is_x_axis ? &g_lock_y : &g_lock_x; // 标记另一个轴的结构体

        // 如果当前轴未被抢占或者已经被本设备抢占
        if (my_lock->holder == NULL || my_lock->holder == who)
        {
            // 锁空闲 / 就是自己 → 直接拿下
            my_lock->holder = who;
            my_lock->requester = NULL;
            my_lock->need_swap = false;
        }
        else
        {
            // 被别人占着 → 在对方那里登记交换请求
            my_lock->requester = who;
            my_lock->need_swap = true;

            // 如果对方也在请求我持有的锁 → 双向交换
            if (other_lock->need_swap &&
                other_lock->requester == my_lock->holder &&
                other_lock->holder == who)
            {
                // 交换 holder
                lv_indev_t *tmp = my_lock->holder;
                my_lock->holder = other_lock->holder; // = who
                other_lock->holder = tmp;

                // 清除双方请求标志
                my_lock->need_swap = other_lock->need_swap = false;
                my_lock->requester = other_lock->requester = NULL;
                // 交换成功 → 不 return，继续处理本轴事件
                // 交换成功 → 重置两个 flag，下一帧重新计算 accum_off
            }
            else
            {
                // 交换不成立 → 退出等待
                return;
            }
        }

        // ===== 方向显示（按轴独立显示，两个设备各自更新自己的轴） =====
        {
            const char *dir_str = "NONE";
            int16_t signed_move = 0; // 带符号的移动量

            switch (dir)
            {
            case LV_DIR_LEFT:
                dir_str = "LEFT";
                signed_move = -dist;
                break;
            case LV_DIR_RIGHT:
                dir_str = "RIGHT";
                signed_move = dist;
                break;
            case LV_DIR_TOP:
                dir_str = "TOP";
                signed_move = -dist;
                break;
            case LV_DIR_BOTTOM:
                dir_str = "BOTTOM";
                signed_move = dist;
                break;
            default:
                break;
            }

            // 按方向判断是哪个轴，更新对应 label
            char buf[32];
            if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT)
            {
                // X 轴移动 → 更新 label10
                snprintf(buf, sizeof(buf), "X Move: %d px", signed_move);
                lv_label_set_text(objects.ui_label_touch_10, buf);
            }
            else if (dir == LV_DIR_TOP || dir == LV_DIR_BOTTOM)
            {
                // Y 轴移动 → 更新 label11
                snprintf(buf, sizeof(buf), "Y Move: %d px", signed_move);
                lv_label_set_text(objects.ui_label_touch_11, buf);
            }

            // 同时按来源显示设备方向
            if (lv_event_get_indev(e) == g_indev_touch1)
            {
                snprintf(buf, sizeof(buf), "indev1: %s", dir_str);
                lv_label_set_text(objects.ui_label_touch_7, buf);
            }
            else if (lv_event_get_indev(e) == g_indev_touch2)
            {
                snprintf(buf, sizeof(buf), "indev2: %s", dir_str);
                lv_label_set_text(objects.ui_label_touch_8, buf);
            }
        }

        // ===== 限幅参数准备 =====
        lv_obj_t *trigger = lv_event_get_target(e);
        lv_coord_t trig_x = lv_obj_get_x(trigger);
        lv_coord_t trig_y = lv_obj_get_y(trigger);
        lv_coord_t trig_w = lv_obj_get_width(trigger);
        lv_coord_t trig_h = lv_obj_get_height(trigger);
        lv_coord_t star_w = lv_obj_get_width(star);
        lv_coord_t star_h = lv_obj_get_height(star);

        lv_coord_t min_x = trig_x;
        lv_coord_t min_y = trig_y;
        lv_coord_t max_x = trig_x + trig_w - star_w;
        lv_coord_t max_y = trig_y + trig_h - star_h;
        if (max_x < min_x)
            max_x = min_x;
        if (max_y < min_y)
            max_y = min_y;

        // ===== X 轴事件处理 =====
        if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT)
        {
            // 更新实时坐标轴 X（带符号）
            g_real_axis_x = (dir == LV_DIR_LEFT) ? -dist : dist;
        }
        // ===== Y 轴事件处理 =====
        else if (dir == LV_DIR_TOP || dir == LV_DIR_BOTTOM)
        {
            // 更新实时坐标轴 Y（带符号）
            g_real_axis_y = (dir == LV_DIR_TOP) ? -dist : dist;
        }

        // ===== 更新两条准星辅助线 =====
        // 显示准星辅助线
        lv_obj_remove_flag(objects.ui_line_touch_3, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(objects.ui_line_touch_4, LV_OBJ_FLAG_HIDDEN);
        // 基点中点（双指按下时的中点）
        int16_t base_mid_x = (g_double_touch.base_x1 + g_double_touch.base_x2) / 2;
        int16_t base_mid_y = (g_double_touch.base_y1 + g_double_touch.base_y2) / 2;

        // ui_line_touch_3（Y 轴平移线，水平）：
        //   X 固定 = 基点中点 X
        //   Y = 基点中点 Y + real_axis_y，再 -20 让线居中（线长 40）
        lv_obj_set_pos(objects.ui_line_touch_4,
                       base_mid_x,
                       base_mid_y + g_real_axis_y);

        // ui_line_touch_4（X 轴平移线，垂直）：
        //   X = 基点中点 X + real_axis_x，再 -20 让线居中
        //   Y 固定 = 基点中点 Y
        lv_obj_set_pos(objects.ui_line_touch_3,
                       base_mid_x + g_real_axis_x,
                       base_mid_y);

        // ===== 打印限流：每 30 次输出一次实时轴状态 =====
        // static uint32_t swipe_log_cnt = 0;
        // if (++swipe_log_cnt >= 30)
        // {
        //     swipe_log_cnt = 0;
        // ESP_LOGI("Axis",
        //          "dir=%d dist=%d | real=(%d,%d) mid=(%d,%d) off=(%d,%d) -> target=(%d,%d)",
        //          dir, dist,
        //          g_real_axis_x, g_real_axis_y,
        //          g_mid_axis_x, g_mid_axis_y,
        //          g_accum_off_x, g_accum_off_y,
        //          g_star_target.target_x, g_star_target.target_y);
        // }

        break;
    }

        // 双指旋转
    case LV_INDEV_GESTURE_ROTATE:
    {
        // 基于本次手势起点的累积角度（度）
        float rotation_rad = lv_event_get_rotation(e);
        float cur_deg = rotation_rad * 180.0f / 3.14159265f;

        // 目标 = 基点 + 本次手势的相对角度
        g_star_target.target_rot_deg = g_star_target.base_rot_deg + cur_deg;

        ESP_LOGI("Gesture", "Rotate: cur=%.2f base=%.2f target=%.2f",
                 cur_deg,
                 g_star_target.base_rot_deg,
                 g_star_target.target_rot_deg);

        // 显示旋转角度到 label10
        char buf[32];
        snprintf(buf, sizeof(buf), "Rotate: %.1f deg", cur_deg);
        lv_label_set_text(objects.ui_label_touch_10, buf);

        ESP_LOGI("Gesture", "Rotate: cur=%.2f base=%.2f target=%.2f",
                 cur_deg,
                 g_star_target.base_rot_deg,
                 g_star_target.target_rot_deg);
        break;
    }

    // 双指捏合
    case LV_INDEV_GESTURE_PINCH:
    {
        // 本次手势的相对缩放（1.0 = 手势起点时的两指间距）
        float cur_scale = lv_event_get_pinch_scale(e);

        // 目标 = 基点 × 相对缩放
        float target = g_star_target.base_scale * cur_scale;

        // 限幅 0.5 ~ 1.5
        if (target < 0.5f)
            target = 0.5f;
        if (target > 1.5f)
            target = 1.5f;

        g_star_target.target_scale = target;

        ESP_LOGI("Gesture", "Pinch: cur=%.3f base=%.3f target=%.3f",
                 cur_scale,
                 g_star_target.base_scale,
                 target);

        // 显示缩放比例到 label10
        char buf[32];
        snprintf(buf, sizeof(buf), "Scale: %.2f x", cur_scale);
        lv_label_set_text(objects.ui_label_touch_10, buf);

        ESP_LOGI("Gesture", "Pinch: cur=%.3f base=%.3f target=%.3f",
                 cur_scale,
                 g_star_target.base_scale,
                 target);
        break;
    }

    default:
        break;
    }
}

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
    xTaskCreatePinnedToCore(example_lvgl_port_task, "LVGL", EXAMPLE_LVGL_TASK_STACK_SIZE, NULL, EXAMPLE_LVGL_TASK_PRIORITY, &lvgl_task_handle, 1);
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
#endif /* LV_USE_DEMO_* */
#endif /* CONFIG_LVGL_EXAMPLE_UI_* */

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

    g_indev_touch1 = lv_indev_create();
    lv_indev_set_type(g_indev_touch1, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(g_indev_touch1, lvgl_touch1_read_cb);
    lv_indev_set_user_data(g_indev_touch1, tp);
    // 双指滑动识别复用了单指的阈值 默认 50 像素以上 和 速度3 识别
    lv_indev_set_gesture_min_distance(g_indev_touch1, 15);
    lv_indev_set_gesture_min_velocity(g_indev_touch1, 3);
    lv_indev_set_pinch_up_threshold(g_indev_touch1, 1.2f);   // 捏合放大阈值 默认放大到 1.5  即150% 才识别
    lv_indev_set_pinch_down_threshold(g_indev_touch1, 0.8f); // 捏合缩小阈值 默认缩小到 0.75 即 75% 才识别
    // 设置旋转触发的阈值（弧度），当前0.087约为5°
    lv_indev_set_rotation_rad_threshold(g_indev_touch1, 0.087f);

    g_indev_touch2 = lv_indev_create();
    lv_indev_set_type(g_indev_touch2, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(g_indev_touch2, lvgl_touch2_read_cb);
    // ===== 给 touch2 也配置手势阈值 =====
    lv_indev_set_gesture_min_distance(g_indev_touch2, 15);
    lv_indev_set_gesture_min_velocity(g_indev_touch2, 3);
    lv_indev_set_pinch_up_threshold(g_indev_touch2, 1.2f);
    lv_indev_set_pinch_down_threshold(g_indev_touch2, 0.8f);
    lv_indev_set_rotation_rad_threshold(g_indev_touch2, 0.087f);
    // ======================================== //

    physics_init();

    // ==== 创建动画任务函数 ====
    example_lvgl_anim_task_create();

    // ========== 绑定手势事件 ==========
    lv_obj_add_event_cb(objects.ui_container_touch_6,
                        ui_tileview_touch_frame_1_gesture_cb,
                        LV_EVENT_GESTURE,
                        NULL);

    // 星星初始位置
    int16_t init_star_x = 362;
    int16_t init_star_y = 215;
    // 初始化星星目标结构体
    gesture_target_init(&g_star_target,
                        init_star_x, // 初始 X
                        init_star_y, // 初始 Y
                        0.0f,        // 初始旋转角度
                        1.0f);       // 初始缩放倍数

    // ========== 下拉菜单事件（保持不变） ==========
    lv_obj_add_event_cb(objects.ui_dropdown_touch_1, touch_dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
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