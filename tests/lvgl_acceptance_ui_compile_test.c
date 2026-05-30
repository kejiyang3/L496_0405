#include "app_lvgl_acceptance_ui.h"

_Static_assert(APP_LVGL_ACCEPTANCE_UI == 1,
               "LCD must use the simple acceptance UI");
_Static_assert(APP_LVGL_STOP_ON_PRESS == 1,
               "STOP must be handled on press, not only after release");
_Static_assert(APP_LVGL_TOUCH_IRQ_FORCES_READ == 1,
               "Latched touch IRQ must force a controller read");
_Static_assert(sizeof(APP_LVGL_TITLE_TEXT) <= 18,
               "Title must stay short on the 240px LCD");
_Static_assert(sizeof(APP_LVGL_START_TEXT) <= 8,
               "START button text must fit the large button");
_Static_assert(sizeof(APP_LVGL_STOP_TEXT) <= 8,
               "STOP button text must fit the large button");
_Static_assert(sizeof(APP_LVGL_SAVING_TEXT) <= 10,
               "SAVING state must fit the button");
_Static_assert(sizeof(APP_LVGL_READY_TEXT) <= 10,
               "READY state must fit the status line");

int lvgl_acceptance_ui_compile_test_anchor;
