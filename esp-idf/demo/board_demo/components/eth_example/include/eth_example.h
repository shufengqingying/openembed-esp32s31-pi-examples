/**
 * @file eth_example.h
 * @brief Ethernet (LAN8720) example component
 *
 * This module demonstrates Ethernet initialization with LAN8720 PHY
 * using RMII interface and bit-bang MDIO.
 */

#ifndef ETH_EXAMPLE_H
#define ETH_EXAMPLE_H

#include <stdint.h>
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_ETH_EXAMPLE_ENABLE

/**
 * @brief Initialize the Ethernet example component
 *
 * Creates a task to initialize and manage Ethernet (LAN8720 PHY).
 * This function returns immediately.
 */
void eth_example_init(void);

static void mdio_bb_gpio_claim(void);
static void mdio_bb_gpio_release(void);
#else

#define eth_example_init() do {} while(0)

#endif /* CONFIG_ETH_EXAMPLE_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* ETH_EXAMPLE_H */