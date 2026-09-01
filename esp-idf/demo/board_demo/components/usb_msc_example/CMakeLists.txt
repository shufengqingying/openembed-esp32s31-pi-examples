idf_component_register(SRCS "usb_msc_example.c"
                       INCLUDE_DIRS "include"
                        PRIV_REQUIRES fatfs esp_driver_gpio esp_timer
                       )