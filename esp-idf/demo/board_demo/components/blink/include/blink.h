#ifndef BLINK_H
#define BLINK_H

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C"
{
#endif

// 如果启用 BLINK 功能，则定义相关函数
#ifdef CONFIG_BLINK_ENABLE
    /**
     * @brief 初始化 LED 闪烁功能（配置 GPIO）
     */
    void configure_led(void);

    /**
     * @brief 切换 LED 亮灭状态
     */
    void blink_led(void);
#else

// 主动定义延时
#define CONFIG_BLINK_PERIOD 1000

// 功能禁用时，将函数调用替换为空操作
#define configure_led() \
    do                  \
    {                   \
    } while (0)
#define blink_led() \
    do              \
    {               \
    } while (0)

#endif

#ifdef __cplusplus
}
#endif

#endif /* BLINK_H */