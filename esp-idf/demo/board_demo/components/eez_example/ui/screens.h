#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_MAIN = 1,
    SCREEN_ID_TOUCH_TEST = 2,
    SCREEN_ID_PALETTE = 3,
    _SCREEN_ID_LAST = 3
};

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *touch_test;
    lv_obj_t *palette;
    lv_obj_t *ui_button_main_1;
    lv_obj_t *ui_button_main_2;
    lv_obj_t *ui_button_main_3;
    lv_obj_t *ui_slider_main_1;
    lv_obj_t *ui_slider_main_2;
    lv_obj_t *ui_slider_main_3;
    lv_obj_t *ui_button_main_4;
    lv_obj_t *ui_panel_touch_point;
    lv_obj_t *ui_label_touch_1;
    lv_obj_t *ui_label_touch_2;
    lv_obj_t *ui_label_touch_3;
    lv_obj_t *ui_label_touch_4;
    lv_obj_t *ui_label_touch_5;
    lv_obj_t *ui_label_touch_6;
    lv_obj_t *ui_label_touch_7;
    lv_obj_t *ui_label_touch_8;
    lv_obj_t *ui_label_touch_9;
    lv_obj_t *ui_label_touch_10;
    lv_obj_t *ui_label_touch_11;
    lv_obj_t *ui_img_help_1;
    lv_obj_t *obj0;
    lv_obj_t *ui_container_touch_1;
    lv_obj_t *ui_container_touch_2;
    lv_obj_t *ui_container_touch_3;
    lv_obj_t *ui_container_touch_4;
    lv_obj_t *ui_container_touch_5;
    lv_obj_t *ui_img_star_1;
    lv_obj_t *ui_container_touch_6;
    lv_obj_t *ui_dropdown_touch_1;
    lv_obj_t *ui_label_help_1;
    lv_obj_t *ui_line_touch_1;
    lv_obj_t *ui_line_touch_2;
    lv_obj_t *ui_line_touch_3;
    lv_obj_t *ui_line_touch_4;
    lv_obj_t *obj1;
} objects_t;

extern objects_t objects;

void create_screen_main();
void tick_screen_main();

void create_screen_touch_test();
void tick_screen_touch_test();

void create_screen_palette();
void tick_screen_palette();

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/