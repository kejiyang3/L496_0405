/**
 * @file lv_port_indev.c
 * @brief LVGL input device driver - CST816 touch (interrupt-driven with pin fallback)
 */

#include "lv_port_indev.h"
#include "app_lvgl.h"
#include "lvgl.h"
#include <stdbool.h>
#include "../../User/touch.h"

volatile uint8_t block_touch_flag = 0;
extern volatile uint8_t touch_int_flag;
volatile uint8_t g_touch_gesture = 0;

static void touchpad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    (void)indev_drv;
    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;
    static bool prev_pressed = false;

    /* Check interrupt flag OR pin directly */
    uint8_t irq = touch_int_flag;
    touch_int_flag = 0U;
    uint8_t pin_low = (HAL_GPIO_ReadPin(INT_TOUCH_GPIO_Port, INT_TOUCH_Pin) == GPIO_PIN_RESET);

    if (!irq && !pin_low && !prev_pressed) {
        data->state = LV_INDEV_STATE_REL;
        data->point.x = last_x;
        data->point.y = last_y;
        return;
    }

    uint16_t x, y;
    uint8_t gesture;
    uint8_t finger = CST816_GetAction(&x, &y, &gesture);
    bool is_pressed = (finger > 0);

    if (is_pressed) {
        last_x = x;
        last_y = y;
        data->state = LV_INDEV_STATE_PR;
        data->point.x = x;
        data->point.y = y;
        APP_LVGL_NotifyTouchActivity();
    } else {
        data->state = LV_INDEV_STATE_REL;
        data->point.x = last_x;
        data->point.y = last_y;
        if (prev_pressed && (gesture == 0x03 || gesture == 0x04)) {
            g_touch_gesture = gesture;
        }
    }

    prev_pressed = is_pressed;
}

void lv_port_indev_init(void)
{
    static lv_indev_drv_t indev_drv;
    CST816_Init();
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    lv_indev_drv_register(&indev_drv);
}
