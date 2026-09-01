/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BUTTON_INPUT_EXAMPLE_H
#define BUTTON_INPUT_EXAMPLE_H

#include <stdint.h>
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_BUTTON_INPUT_EXAMPLE_ENABLE

/**
 * @brief Initialize the button input example component.
 *
 * This function sets up GPIO and interrupt for the boot button.
 */
void button_input_example_init(void);

#else

/**
 * @brief Empty stub when component is disabled
 */
#define button_input_example_init() do {} while(0)

#endif /* CONFIG_BUTTON_INPUT_EXAMPLE_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_INPUT_EXAMPLE_H */