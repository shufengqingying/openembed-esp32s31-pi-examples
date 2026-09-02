/**
 * @file eth_example.c
 * @brief Ethernet (LAN8720) example with RMII interface and bit-bang MDIO
 *
 * This module demonstrates Ethernet connectivity using the LAN8720 PHY
 * via RMII interface on ESP32-S31.
 *
 * Features:
 * - LAN8720 PHY initialization and configuration
 * - RMII interface with configurable GPIO pins (TX/RX/CRS_DV/REF_CLK)
 * - Bit-bang MDIO for SMI communication (MDC/MDIO)
 * - Hardware PHY reset with configurable GPIO
 * - Automatic PHY scanning and detection (address 0-31)
 * - Link status and MAC address reporting via event callbacks
 * - DHCP client for automatic IP acquisition
 * - Auto-retry on initialization failure (2-second interval)
 * - Configurable clock source: internal (ESP32 output) or external (PHY/oscillator)
 * - Non-blocking design with dedicated initialization task
 *
 * Configuration options (menuconfig):
 * - CONFIG_ETH_EXAMPLE_ENABLE: Enable/disable this component
 * - CONFIG_ETH_EXAMPLE_PHY_ADDR: LAN8720 PHY address (default: 0)
 * - CONFIG_ETH_EXAMPLE_MDC_GPIO: MDC GPIO (default: 16)
 * - CONFIG_ETH_EXAMPLE_MDIO_GPIO: MDIO GPIO (default: 17)
 * - CONFIG_ETH_EXAMPLE_RMII_CLK_GPIO: RMII REF_CLK GPIO (default: 13)
 * - CONFIG_ETH_EXAMPLE_RMII_CLK_INTERNAL: Internal clock output mode
 * - CONFIG_ETH_EXAMPLE_TX_EN_GPIO: TX_EN GPIO (default: 12)
 * - CONFIG_ETH_EXAMPLE_TXD0_GPIO: TXD0 GPIO (default: 8)
 * - CONFIG_ETH_EXAMPLE_TXD1_GPIO: TXD1 GPIO (default: 9)
 * - CONFIG_ETH_EXAMPLE_CRS_DV_GPIO: CRS_DV GPIO (default: 15)
 * - CONFIG_ETH_EXAMPLE_RXD0_GPIO: RXD0 GPIO (default: 19)
 * - CONFIG_ETH_EXAMPLE_RXD1_GPIO: RXD1 GPIO (default: 18)
 * - CONFIG_ETH_EXAMPLE_PHY_RST_GPIO: PHY Reset GPIO (default: 14)
 * - CONFIG_ETH_EXAMPLE_TASK_PRIORITY: Init task priority (default: 4)
 *
 * Pin mapping (default):
 *   PHY_ADDR  : 0
 *   MDC       : GPIO16
 *   MDIO      : GPIO17
 *   REF_CLK   : GPIO13 (external 50MHz)
 *   TX_EN     : GPIO12
 *   TXD0      : GPIO8
 *   TXD1      : GPIO9
 *   CRS_DV    : GPIO15
 *   RXD0      : GPIO19
 *   RXD1      : GPIO18
 *   PHY_RST   : GPIO14
 *
 * Note: This component uses bit-bang MDIO via GPIO pins instead of
 *       hardware EMAC SMI pins. This provides flexible GPIO routing
 *       at the cost of slightly slower MDIO access.
 */

#include "eth_example.h"
#include "sdkconfig.h"

#ifdef CONFIG_ETH_EXAMPLE_ENABLE

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_eth_mac_esp.h"
#include "esp_eth_phy_lan87xx.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_check.h"
#include "soc/cnnt_io_mux_struct.h"

static const char *TAG = "eth_example";

static uint32_t s_gmac_ded_pad_sel;



#define MDIO_BB_HALF_US 2

static void mdio_bb_write_bit(int bit)
{
    gpio_set_level(CONFIG_ETH_EXAMPLE_MDIO_GPIO, bit);
    esp_rom_delay_us(MDIO_BB_HALF_US);
    gpio_set_level(CONFIG_ETH_EXAMPLE_MDC_GPIO, 1);
    esp_rom_delay_us(MDIO_BB_HALF_US);
    gpio_set_level(CONFIG_ETH_EXAMPLE_MDC_GPIO, 0);
}

static int mdio_bb_read_bit(void)
{
    gpio_set_level(CONFIG_ETH_EXAMPLE_MDC_GPIO, 1);
    esp_rom_delay_us(MDIO_BB_HALF_US);
    int bit = gpio_get_level(CONFIG_ETH_EXAMPLE_MDIO_GPIO);
    gpio_set_level(CONFIG_ETH_EXAMPLE_MDC_GPIO, 0);
    esp_rom_delay_us(MDIO_BB_HALF_US);
    return bit;
}

static uint16_t mdio_bb_read_reg(uint8_t phy_addr, uint8_t reg_addr)
{
    ESP_ERROR_CHECK(gpio_set_direction(CONFIG_ETH_EXAMPLE_MDIO_GPIO, GPIO_MODE_OUTPUT));

    for (int i = 0; i < 32; i++)
    {
        mdio_bb_write_bit(1); // preamble
    }
    mdio_bb_write_bit(0); // ST
    mdio_bb_write_bit(1);
    mdio_bb_write_bit(1); // OP = read
    mdio_bb_write_bit(0);
    for (int i = 4; i >= 0; i--)
    {
        mdio_bb_write_bit((phy_addr >> i) & 1);
    }
    for (int i = 4; i >= 0; i--)
    {
        mdio_bb_write_bit((reg_addr >> i) & 1);
    }

    ESP_ERROR_CHECK(gpio_set_direction(CONFIG_ETH_EXAMPLE_MDIO_GPIO, GPIO_MODE_INPUT));
    mdio_bb_read_bit(); // turnaround, PHY Ӧڴ
    uint16_t val = 0;
    for (int i = 0; i < 16; i++)
    {
        val = (val << 1) | mdio_bb_read_bit();
    }
    mdio_bb_read_bit(); // ֡Уһ֡ǰ PHY ײ
    return val;
}

static void eth_phy_hw_reset(void)
{
    const gpio_config_t rst_cfg = {
        .pin_bit_mask = (1ULL << CONFIG_ETH_EXAMPLE_PHY_RST_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&rst_cfg));
    ESP_ERROR_CHECK(gpio_set_level(CONFIG_ETH_EXAMPLE_PHY_RST_GPIO, 0));
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_ERROR_CHECK(gpio_set_level(CONFIG_ETH_EXAMPLE_PHY_RST_GPIO, 1));
    vTaskDelay(pdMS_TO_TICKS(50));
}

static void eth_hw_precheck(void)
{
    mdio_bb_gpio_claim();

    int found = 0;
    for (uint8_t addr = 0; addr < 32; addr++)
    {
        uint16_t id1 = mdio_bb_read_reg(addr, 2);
        uint16_t id2 = mdio_bb_read_reg(addr, 3);
        if (id1 != 0xFFFF && id1 != 0x0000)
        {
            ESP_LOGW(TAG, "bit-bang MDIO: PHY at addr %u, ID1=0x%04X ID2=0x%04X", addr, id1, id2);
            found++;
        }
    }
    if (found == 0)
    {
        ESP_LOGE(TAG, "bit-bang MDIO: no response on any of the 32 addresses (MDC=GPIO%d MDIO=GPIO%d)",
                 CONFIG_ETH_EXAMPLE_MDC_GPIO, CONFIG_ETH_EXAMPLE_MDIO_GPIO);
    }
    mdio_bb_gpio_release();
}
static esp_err_t mdio_bb_mac_read_phy_reg(esp_eth_mac_t *mac, uint32_t phy_addr,
                                          uint32_t phy_reg, uint32_t *reg_value)
{
    (void)mac;
    if (reg_value == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    mdio_bb_gpio_claim();
    *reg_value = mdio_bb_read_reg(phy_addr, phy_reg);
    mdio_bb_gpio_release();
    return ESP_OK;
}

static void mdio_bb_gpio_claim(void)
{
    /* EMAC �õ� GPIO13~19 ʱ������� CNNT pad �е� GMAC ר�ÿ��ƣ�ͬ��� MDC/MDIO Ҳ���д��ߣ�
     * RMII ����Ҫ���λ������ֻ�ڵ��� MDIO �����ڼ����ʱ�ص���*/
    s_gmac_ded_pad_sel = CNNT_PAD_CTRL.ctrl.gmac_pad_pin_ctrl_ded_sel;
    CNNT_PAD_CTRL.ctrl.gmac_pad_pin_ctrl_ded_sel = 0;

    // �ȶϿ����ܲ����������ź�·�ɣ����� gpio_set_level ��������Ч
    ESP_ERROR_CHECK(gpio_reset_pin(CONFIG_ETH_EXAMPLE_MDC_GPIO));
    ESP_ERROR_CHECK(gpio_reset_pin(CONFIG_ETH_EXAMPLE_MDIO_GPIO));

    const gpio_config_t smi_cfg = {
        .pin_bit_mask = (1ULL << CONFIG_ETH_EXAMPLE_MDC_GPIO) | (1ULL << CONFIG_ETH_EXAMPLE_MDIO_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&smi_cfg));
    ESP_ERROR_CHECK(gpio_set_level(CONFIG_ETH_EXAMPLE_MDC_GPIO, 0));
}

static void mdio_bb_write_reg(uint8_t phy_addr, uint8_t reg_addr, uint16_t val)
{
    ESP_ERROR_CHECK(gpio_set_direction(CONFIG_ETH_EXAMPLE_MDIO_GPIO, GPIO_MODE_OUTPUT));

    for (int i = 0; i < 32; i++)
    {
        mdio_bb_write_bit(1); // preamble
    }
    mdio_bb_write_bit(0); // ST
    mdio_bb_write_bit(1);
    mdio_bb_write_bit(0); // OP = write
    mdio_bb_write_bit(1);
    for (int i = 4; i >= 0; i--)
    {
        mdio_bb_write_bit((phy_addr >> i) & 1);
    }
    for (int i = 4; i >= 0; i--)
    {
        mdio_bb_write_bit((reg_addr >> i) & 1);
    }
    mdio_bb_write_bit(1); // turnaround
    mdio_bb_write_bit(0);
    for (int i = 15; i >= 0; i--)
    {
        mdio_bb_write_bit((val >> i) & 1);
    }

    ESP_ERROR_CHECK(gpio_set_direction(CONFIG_ETH_EXAMPLE_MDIO_GPIO, GPIO_MODE_INPUT));
    mdio_bb_read_bit(); // ֡�����
}

static void mdio_bb_gpio_release(void)
{
    ESP_ERROR_CHECK(gpio_reset_pin(CONFIG_ETH_EXAMPLE_MDC_GPIO));
    ESP_ERROR_CHECK(gpio_reset_pin(CONFIG_ETH_EXAMPLE_MDIO_GPIO));
    CNNT_PAD_CTRL.ctrl.gmac_pad_pin_ctrl_ded_sel = s_gmac_ded_pad_sel;
}

static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "ETH Got IP: " IPSTR ", Mask: " IPSTR ", GW: " IPSTR,
             IP2STR(&ip_info->ip), IP2STR(&ip_info->netmask), IP2STR(&ip_info->gw));
}

static esp_err_t mdio_bb_mac_write_phy_reg(esp_eth_mac_t *mac, uint32_t phy_addr,
                                           uint32_t phy_reg, uint32_t reg_value)
{
    (void)mac;
    mdio_bb_gpio_claim();
    mdio_bb_write_reg(phy_addr, phy_reg, reg_value);
    mdio_bb_gpio_release();
    return ESP_OK;
}

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id)
    {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "ETH Link Up, MAC %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2],
                 mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "ETH Link Down");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "ETH Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "ETH Stopped");
        break;
    default:
        break;
    }
}

static esp_err_t ethernet_init(void)
{
    static esp_netif_t *s_eth_netif;

    if (s_eth_netif == NULL)
    {
        esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
        s_eth_netif = esp_netif_new(&netif_cfg);
        if (s_eth_netif == NULL)
        {
            return ESP_FAIL;
        }
    }

    eth_esp32_emac_config_t emac_cfg = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    // GPIO16/17 �� CNNT ��EMAC �� SMI �źŹ� GPIO Matrix �Ӳ������������� MDIO
    emac_cfg.smi_gpio.mdc_num = -1;
    emac_cfg.smi_gpio.mdio_num = -1;
    emac_cfg.interface = EMAC_DATA_INTERFACE_RMII;
#ifdef CONFIG_ETH_EXAMPLE_RMII_CLK_INTERNAL
    emac_cfg.clock_config.rmii.clock_mode = EMAC_CLK_OUT;
#else
    emac_cfg.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
#endif
    emac_cfg.clock_config.rmii.clock_gpio = CONFIG_ETH_EXAMPLE_RMII_CLK_GPIO;
    emac_cfg.emac_dataif_gpio.rmii.tx_en_num = CONFIG_ETH_EXAMPLE_TX_EN_GPIO;
    emac_cfg.emac_dataif_gpio.rmii.txd0_num = CONFIG_ETH_EXAMPLE_TXD0_GPIO;
    emac_cfg.emac_dataif_gpio.rmii.txd1_num = CONFIG_ETH_EXAMPLE_TXD1_GPIO;
    emac_cfg.emac_dataif_gpio.rmii.crs_dv_num = CONFIG_ETH_EXAMPLE_CRS_DV_GPIO;
    emac_cfg.emac_dataif_gpio.rmii.rxd0_num = CONFIG_ETH_EXAMPLE_RXD0_GPIO;
    emac_cfg.emac_dataif_gpio.rmii.rxd1_num = CONFIG_ETH_EXAMPLE_RXD1_GPIO;

    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    mac_cfg.sw_reset_timeout_ms = 200;
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_cfg, &mac_cfg);
    if (mac == NULL)
    {
        return ESP_FAIL;
    }
    mac->read_phy_reg = mdio_bb_mac_read_phy_reg;
    mac->write_phy_reg = mdio_bb_mac_write_phy_reg;

    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.phy_addr = CONFIG_ETH_EXAMPLE_PHY_ADDR;
    phy_cfg.reset_gpio_num = -1; // ���� eth_phy_hw_reset() �ֶ���λ������
    phy_cfg.reset_timeout_ms = 500;
    phy_cfg.autonego_timeout_ms = 5000;
    esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_cfg);
    if (phy == NULL)
    {
        mac->del(mac);
        return ESP_FAIL;
    }

    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    esp_err_t ret = esp_eth_driver_install(&eth_cfg, &eth_handle);
    if (ret != ESP_OK)
    {
        phy->del(phy);
        mac->del(mac);
        return ret;
    }

    ESP_RETURN_ON_ERROR(esp_netif_attach(s_eth_netif, esp_eth_new_netif_glue(eth_handle)),
                        TAG, "attach netif failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler, NULL),
                        TAG, "register eth event failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, got_ip_event_handler, NULL),
                        TAG, "register ip event failed");

    return esp_eth_start(eth_handle);
}

static void eth_mdio_dump(const char *when)
{
    mdio_bb_gpio_claim();
    uint16_t bmcr = mdio_bb_read_reg(CONFIG_ETH_EXAMPLE_PHY_ADDR, 0);
    uint16_t bmsr = mdio_bb_read_reg(CONFIG_ETH_EXAMPLE_PHY_ADDR, 1);
    mdio_bb_gpio_release();

    ESP_LOGW(TAG, "bit-bang MDIO %s: addr%d BMCR=0x%04X (power_down=%d) BMSR=0x%04X (link=%d)",
             when, CONFIG_ETH_EXAMPLE_PHY_ADDR, bmcr, (bmcr >> 11) & 1, bmsr, (bmsr >> 2) & 1);
}

static void ethernet_task(void *arg)
{
    eth_phy_hw_reset();
    eth_hw_precheck();

    for (int attempt = 1;; attempt++)
    {
        esp_err_t ret = ethernet_init();
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "Ethernet started (attempt %d)", attempt);
            break;
        }
        ESP_LOGE(TAG, "Ethernet init failed (attempt %d): %s", attempt, esp_err_to_name(ret));
        eth_mdio_dump("after EMAC attempt");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    vTaskDelete(NULL);
}

void eth_example_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    xTaskCreate(ethernet_task, "eth_init", 4096, NULL, 4, NULL);
}

#endif /* CONFIG_ETH_EXAMPLE_ENABLE */