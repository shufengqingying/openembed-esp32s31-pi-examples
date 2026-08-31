idf_component_register(SRCS "blink.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES led_strip
                       PRIV_REQUIRES esp_driver_gpio
                       )