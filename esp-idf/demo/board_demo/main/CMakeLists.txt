idf_component_register(SRCS "board_demo_main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES blink_example button_input_example sdmmc_example
                        usb_msc_example eth_example lvgl_example
                       PRIV_REQUIRES esp_driver_gpio esp_timer
                       )
