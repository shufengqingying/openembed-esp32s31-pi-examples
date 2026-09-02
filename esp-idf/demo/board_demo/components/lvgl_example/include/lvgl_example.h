/**
 * @file lvgl_example.h
 * @brief LVGL graphics library example component
 *
 * This module demonstrates LVGL (Light and Versatile Graphics Library)
 * with display and touch support.
 */

#ifndef LVGL_EXAMPLE_H
#define LVGL_EXAMPLE_H

#include <stdint.h>
#include "sdkconfig.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_LVGL_EXAMPLE_ENABLE

/**
 * @brief Initialize the LVGL example component
 *
 * Initializes LVGL, display driver, and touch driver.
 * This function returns immediately.
 */
void lvgl_example_init(void);

void example_lvgl_demo_ui(lv_display_t *disp);


#else

#define lvgl_example_init() do {} while(0)

#endif /* CONFIG_LVGL_EXAMPLE_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* LVGL_EXAMPLE_H */