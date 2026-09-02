idf_component_register(SRCS "eth_example.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_eth esp_netif esp_event nvs_flash
                       PRIV_REQUIRES esp_driver_gpio esp_timer esp_hw_support)