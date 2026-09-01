idf_component_register(SRCS "board_demo_main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES blink button_input_example sdmmc_example
                       PRIV_REQUIRES esp_driver_gpio esp_timer
                       )
