
idf_component_register(
    SRCS 
        "lvgl_example.c" 
        "example_rgb_lcd_panel.c"    
        "lvgl_demo_ui.c"   
    INCLUDE_DIRS "include"
    REQUIRES 
        eez_example                           
    PRIV_REQUIRES 
        esp_lcd 
        esp_timer 
        esp_driver_gpio
    INCLUDE_DIRS 
        "." 
)