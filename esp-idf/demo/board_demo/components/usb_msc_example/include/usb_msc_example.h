/**
 * @file usb_msc_example.h
 * @brief USB MSC (Mass Storage Class) example component
 *
 * This module demonstrates USB host mode with MSC device (U disk) support.
 */

#ifndef USB_MSC_EXAMPLE_H
#define USB_MSC_EXAMPLE_H

#include <stdint.h>
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_USB_MSC_EXAMPLE_ENABLE

/**
 * @brief Initialize the USB MSC example component
 *
 * Initializes USB host driver, creates the USB task, and configures
 * the BOOT button for exit control. This function returns immediately.
 */
void usb_msc_example_init(void);

/**
 * @brief Run the USB MSC example main loop
 *
 * This function contains the main event loop that processes USB device
 * connect/disconnect events. It should be called from the main loop.
 *
 * @note This function returns immediately if no event is pending.
 *       It should be called periodically to process USB events.
 */
void usb_msc_example_run(void);

#else

/**
 * @brief Empty stub when component is disabled
 */
#define usb_msc_example_init() do {} while(0)

/**
 * @brief Empty stub when component is disabled
 */
#define usb_msc_example_run() do {} while(0)

#endif /* CONFIG_USB_MSC_EXAMPLE_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* USB_MSC_EXAMPLE_H */