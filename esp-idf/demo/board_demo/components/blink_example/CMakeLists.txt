idf_component_register(SRCS "blink_example.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES led_strip
                       PRIV_REQUIRES esp_driver_gpio
                       )