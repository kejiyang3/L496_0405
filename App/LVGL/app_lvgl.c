/**
 * @file app_lvgl.c
 * @brief Minimal acceptance UI for manual recording control.
 */
#include "app_lvgl.h"
#include "app_lvgl_acceptance_ui.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "stm32l4xx_hal.h"
#include "DEV_Config.h"
#include "LCD_1in69.h"
#include "ecg_record_control.h"

#define UI_COLOR_BG        0x07111F
#define UI_COLOR_PANEL     0x14263D
#define UI_COLOR_TEXT      0xF4F7FB
#define UI_COLOR_MUTED     0xA8B4C4
#define UI_COLOR_GREEN     0x37D67A
#define UI_COLOR_BLUE      0x3FA7FF
#define UI_COLOR_YELLOW    0xFFD166
#define UI_COLOR_RED       0xFF5A5F
#define UI_COLOR_LINE      0x294967
#define UI_BACKLIGHT_ON    1000U
#define UI_BACKLIGHT_OFF   0U
#define UI_BACKLIGHT_IDLE_MS 30000U

static lv_obj_t *ui_title;
static lv_obj_t *ui_state;
static lv_obj_t *ui_button;
static lv_obj_t *ui_button_label;
static lv_obj_t *ui_file;
static lv_obj_t *ui_hint;
static lv_obj_t *ui_live;

static lv_style_t style_screen;
static lv_style_t style_title;
static lv_style_t style_state;
static lv_style_t style_file;
static lv_style_t style_hint;
static lv_style_t style_button_start;
static lv_style_t style_button_stop;
static lv_style_t style_button_disabled;

static ECG_RecordState_t s_state_last = (ECG_RecordState_t)0xff;
static uint8_t s_button_mode_last = 0xffU;
static uint32_t s_seq_last = 0xffffffffUL;
static uint32_t s_live_last = 0xffffffffUL;
static uint32_t s_last_touch_tick = 0;
static uint8_t s_backlight_on = 1U;

static uint32_t display_seq(void)
{
    if (g_ecg_rec.state == ECG_REC_IDLE ||
        g_ecg_rec.state == ECG_REC_RECORDING ||
        g_ecg_rec.state == ECG_REC_STOPPING ||
        g_ecg_rec.file_seq == 0U) {
        return g_ecg_rec.file_seq;
    }

    return g_ecg_rec.file_seq - 1U;
}

static uint32_t next_record_seq(void)
{
    if (g_ecg_rec.state == ECG_REC_IDLE ||
        g_ecg_rec.state == ECG_REC_STOPPED ||
        g_ecg_rec.state == ECG_REC_ERROR) {
        return g_ecg_rec.file_seq;
    }

    return display_seq();
}

static const char *state_text(void)
{
    switch (g_ecg_rec.state) {
    case ECG_REC_IDLE:      return APP_LVGL_READY_TEXT;
    case ECG_REC_RECORDING: return "RECORDING";
    case ECG_REC_STOPPING:  return APP_LVGL_SAVING_TEXT;
    case ECG_REC_STOPPED:   return "SAVED";
    case ECG_REC_ERROR:     return "ERROR";
    default:                return "BOOT";
    }
}

static lv_color_t state_color(void)
{
    switch (g_ecg_rec.state) {
    case ECG_REC_RECORDING: return lv_color_hex(UI_COLOR_GREEN);
    case ECG_REC_STOPPING:  return lv_color_hex(UI_COLOR_YELLOW);
    case ECG_REC_STOPPED:   return lv_color_hex(UI_COLOR_BLUE);
    case ECG_REC_ERROR:     return lv_color_hex(UI_COLOR_RED);
    default:                return lv_color_hex(UI_COLOR_MUTED);
    }
}

static void reset_ui_cache(void)
{
    s_state_last = (ECG_RecordState_t)0xff;
    s_button_mode_last = 0xffU;
    s_seq_last = 0xffffffffUL;
    s_live_last = 0xffffffffUL;
}

static void set_backlight_state(uint8_t on)
{
    if (on == s_backlight_on) {
        return;
    }

    s_backlight_on = on;
    LCD_1IN69_SetBackLight(on ? UI_BACKLIGHT_ON : UI_BACKLIGHT_OFF);
    if (on) {
        reset_ui_cache();
    }
}

uint8_t APP_LVGL_NotifyTouchActivity(void)
{
    uint8_t wake_only = (s_backlight_on == 0U) ? 1U : 0U;

    s_last_touch_tick = HAL_GetTick();
    set_backlight_state(1U);

    return wake_only;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                            lv_style_t *style, lv_align_t align,
                            lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_add_style(label, style, 0);
    lv_obj_align(label, align, x, y);
    return label;
}

static void record_button_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);

    if (g_ecg_rec.state == ECG_REC_RECORDING && code == LV_EVENT_PRESSED) {
        ECG_RequestStop();
        return;
    }

    if (code != LV_EVENT_CLICKED) {
        return;
    }

    if (g_ecg_rec.state == ECG_REC_IDLE ||
        g_ecg_rec.state == ECG_REC_STOPPED ||
        g_ecg_rec.state == ECG_REC_ERROR) {
        ECG_RequestStart();
    } else if (g_ecg_rec.state == ECG_REC_RECORDING) {
        ECG_RequestStop();
    }
}

static void set_button_mode(uint8_t mode)
{
    if (mode == s_button_mode_last) {
        return;
    }

    s_button_mode_last = mode;
    lv_obj_clear_state(ui_button, LV_STATE_DISABLED);
    lv_obj_remove_style(ui_button, &style_button_start, 0);
    lv_obj_remove_style(ui_button, &style_button_stop, 0);
    lv_obj_remove_style(ui_button, &style_button_disabled, 0);

    if (mode == 0U) {
        lv_label_set_text(ui_button_label, APP_LVGL_STOP_TEXT);
        lv_obj_add_style(ui_button, &style_button_stop, 0);
    } else if (mode == 1U) {
        lv_label_set_text(ui_button_label, APP_LVGL_START_TEXT);
        lv_obj_add_style(ui_button, &style_button_start, 0);
    } else {
        lv_label_set_text(ui_button_label, APP_LVGL_SAVING_TEXT);
        lv_obj_add_style(ui_button, &style_button_disabled, 0);
        lv_obj_add_state(ui_button, LV_STATE_DISABLED);
    }
}

static void ui_update_cb(lv_timer_t *timer)
{
    (void)timer;

    uint32_t now = HAL_GetTick();

    if (s_backlight_on != 0U &&
        s_last_touch_tick != 0U &&
        (now - s_last_touch_tick) >= UI_BACKLIGHT_IDLE_MS) {
        set_backlight_state(0U);
    }

    if (s_backlight_on == 0U) {
        return;
    }

    uint32_t seq = display_seq();
    uint32_t file_seq = (g_ecg_rec.state == ECG_REC_RECORDING ||
                         g_ecg_rec.state == ECG_REC_STOPPING) ? seq : next_record_seq();

    uint8_t state_changed = (g_ecg_rec.state != s_state_last) ? 1U : 0U;
    if (g_ecg_rec.state != s_state_last) {
        s_state_last = g_ecg_rec.state;
        lv_label_set_text(ui_state, state_text());
        lv_obj_set_style_text_color(ui_state, state_color(), 0);
    }

    if (file_seq != s_seq_last) {
        s_seq_last = file_seq;
        lv_label_set_text_fmt(ui_file,
                              "SD %03lu: CSV ECG/PPG/IMU + WAV MIC",
                              (unsigned long)file_seq);
    }

    uint8_t button_mode;
    if (g_ecg_rec.state == ECG_REC_RECORDING) {
        button_mode = 0U;
    } else if (g_ecg_rec.state == ECG_REC_STOPPING) {
        button_mode = 2U;
    } else {
        button_mode = 1U;
    }
    set_button_mode(button_mode);

    if (state_changed) {
        if (g_ecg_rec.state == ECG_REC_STOPPING) {
            lv_label_set_text(ui_hint, "Writing SD summary...");
        } else if (g_ecg_rec.state == ECG_REC_STOPPED) {
            lv_label_set_text(ui_hint, "Saved. Remove SD after idle.");
        } else {
            lv_label_set_text(ui_hint, "Tap START / STOP for acceptance.");
        }
    }

    uint32_t live = now / 1000UL;
    if (live != s_live_last) {
        s_live_last = live;
        lv_label_set_text_fmt(ui_live, "LIVE %04lu", (unsigned long)(live % 10000UL));
    }
}

static void init_styles(void)
{
    lv_style_init(&style_screen);
    lv_style_set_bg_color(&style_screen, lv_color_hex(UI_COLOR_BG));
    lv_style_set_bg_opa(&style_screen, LV_OPA_COVER);

    lv_style_init(&style_title);
    lv_style_set_text_color(&style_title, lv_color_hex(UI_COLOR_TEXT));
    lv_style_set_text_font(&style_title, &lv_font_montserrat_14);

    lv_style_init(&style_state);
    lv_style_set_text_color(&style_state, lv_color_hex(UI_COLOR_MUTED));
    lv_style_set_text_font(&style_state, &lv_font_montserrat_14);

    lv_style_init(&style_file);
    lv_style_set_text_color(&style_file, lv_color_hex(UI_COLOR_TEXT));
    lv_style_set_text_font(&style_file, &lv_font_montserrat_14);

    lv_style_init(&style_hint);
    lv_style_set_text_color(&style_hint, lv_color_hex(UI_COLOR_MUTED));
    lv_style_set_text_font(&style_hint, &lv_font_montserrat_14);

    lv_style_init(&style_button_start);
    lv_style_set_bg_color(&style_button_start, lv_color_hex(UI_COLOR_GREEN));
    lv_style_set_bg_opa(&style_button_start, LV_OPA_COVER);
    lv_style_set_border_width(&style_button_start, 2);
    lv_style_set_border_color(&style_button_start, lv_color_hex(UI_COLOR_LINE));
    lv_style_set_radius(&style_button_start, 8);
    lv_style_set_text_color(&style_button_start, lv_color_hex(UI_COLOR_BG));
    lv_style_set_text_font(&style_button_start, &lv_font_montserrat_14);

    lv_style_init(&style_button_stop);
    lv_style_set_bg_color(&style_button_stop, lv_color_hex(UI_COLOR_RED));
    lv_style_set_bg_opa(&style_button_stop, LV_OPA_COVER);
    lv_style_set_border_width(&style_button_stop, 2);
    lv_style_set_border_color(&style_button_stop, lv_color_hex(UI_COLOR_LINE));
    lv_style_set_radius(&style_button_stop, 8);
    lv_style_set_text_color(&style_button_stop, lv_color_hex(UI_COLOR_TEXT));
    lv_style_set_text_font(&style_button_stop, &lv_font_montserrat_14);

    lv_style_init(&style_button_disabled);
    lv_style_set_bg_color(&style_button_disabled, lv_color_hex(UI_COLOR_PANEL));
    lv_style_set_bg_opa(&style_button_disabled, LV_OPA_COVER);
    lv_style_set_border_width(&style_button_disabled, 2);
    lv_style_set_border_color(&style_button_disabled, lv_color_hex(UI_COLOR_LINE));
    lv_style_set_radius(&style_button_disabled, 8);
    lv_style_set_text_color(&style_button_disabled, lv_color_hex(UI_COLOR_MUTED));
    lv_style_set_text_font(&style_button_disabled, &lv_font_montserrat_14);
}

void APP_LVGL_Init(void)
{
    DEV_Module_Init();
    s_backlight_on = 1U;
    s_last_touch_tick = HAL_GetTick();
    LCD_1IN69_SetBackLight(UI_BACKLIGHT_ON);
    LCD_1IN69_Init(VERTICAL);

    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    lv_obj_clean(lv_scr_act());
    App_LVGL_TestUI();
}

void App_LVGL_TestUI(void)
{
    init_styles();

    lv_obj_t *scr = lv_scr_act();
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(scr, &style_screen, 0);

    ui_title = make_label(scr, APP_LVGL_TITLE_TEXT, &style_title,
                          LV_ALIGN_TOP_MID, 0, 14);

    ui_state = make_label(scr, "BOOT", &style_state,
                          LV_ALIGN_TOP_MID, 0, 46);

    ui_button = lv_btn_create(scr);
    lv_obj_set_size(ui_button, 188, 92);
    lv_obj_align(ui_button, LV_ALIGN_CENTER, 0, -8);
    lv_obj_add_style(ui_button, &style_button_start, 0);
    lv_obj_add_event_cb(ui_button, record_button_event_cb, LV_EVENT_ALL, NULL);
    ui_button_label = lv_label_create(ui_button);
    lv_label_set_text(ui_button_label, APP_LVGL_START_TEXT);
    lv_obj_center(ui_button_label);

    ui_file = make_label(scr, "SD 001: CSV ECG/PPG/IMU + WAV MIC",
                         &style_file, LV_ALIGN_TOP_MID, 0, 196);

    ui_hint = make_label(scr, "Tap START / STOP for acceptance.",
                         &style_hint, LV_ALIGN_TOP_MID, 0, 222);

    ui_live = make_label(scr, "LIVE 0000", &style_hint,
                         LV_ALIGN_BOTTOM_MID, 0, -14);

    reset_ui_cache();
    ui_update_cb(NULL);
    lv_timer_create(ui_update_cb, 500, NULL);
}

void APP_LVGL_Process(void)
{
    lv_timer_handler();
}

uint32_t APP_LVGL_GetProcessDelayMs(void)
{
    return s_backlight_on ? 10U : 50U;
}
