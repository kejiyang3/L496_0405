/**
 * @file app_lvgl.c
 * @brief v0.4 Demo Stable Display UI
 *
 * Shows: Sleep Apnea Monitor Demo with live CORE status, session time,
 * ECG/PPG/IMU sample rates, simple waveforms, MIC status, SD state,
 * and error counters.
 *
 * LVGL task runs at BelowNormal priority, must not block CORE sensors.
 */
#include "app_lvgl.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "extra/widgets/chart/lv_chart.h"
#include "stm32l4xx_hal.h"
#include "DEV_Config.h"
#include "LCD_1in69.h"
#include "ecg_record_control.h"

/* ===== Extern telemetry from freertos.c ===== */
extern volatile uint32_t g_ppg_task_call_count;
extern volatile uint32_t g_ppg_int_wakeup_count;
extern volatile uint32_t g_imu_task_call_count;
extern volatile uint32_t g_imu_read_ok_count;
extern volatile uint32_t g_imu_read_fail_count;
extern volatile uint32_t g_i2c3_rate_iso_error_count;
extern volatile uint32_t g_ppg_init_ret;
extern volatile uint32_t g_icm_init_ret;

/* ===== Demo modes ===== */
typedef enum {
    DEMO_MODE_CORE_ONLY_5MIN = 0,
    DEMO_MODE_WITH_MIC_2MIN,
    DEMO_MODE_WITH_MIC_10MIN,
    DEMO_MODE_SCREEN_ONLY
} DemoMode_t;

/* ===== Colors ===== */
#define UI_COLOR_BG          0x07111F
#define UI_COLOR_PANEL       0x14263D
#define UI_COLOR_TEXT        0xF4F7FB
#define UI_COLOR_MUTED       0xA8B4C4
#define UI_COLOR_GREEN       0x37D67A
#define UI_COLOR_BLUE        0x3FA7FF
#define UI_COLOR_YELLOW      0xFFD166
#define UI_COLOR_RED         0xFF5A5F
#define UI_COLOR_ORANGE      0xFF9F43
#define UI_COLOR_LINE        0x294967
#define UI_COLOR_WAVE_ECG    0x3FA7FF
#define UI_COLOR_WAVE_PPG    0xFF6B6B
#define UI_COLOR_WAVE_IMU    0x37D67A
#define UI_COLOR_BAR_MIC     0xFFD166

/* ===== Backlight ===== */
#define UI_BACKLIGHT_ON      1000U
#define UI_BACKLIGHT_OFF     0U
#define UI_BACKLIGHT_IDLE_MS 0U  /* v0.4 demo: never auto-off during demo */

/* ===== Waveform ===== */
#define WAVE_POINTS          120   /* chart data points */
#define WAVE_ECG_AMP         80    /* ECG amplitude range */
#define WAVE_PPG_AMP         60    /* PPG amplitude range */

/* ===== UI refresh ===== */
#define UI_FAST_REFRESH_MS   200   /* status text refresh */
#define UI_WAVE_REFRESH_MS   100   /* waveform refresh */

/* ===== Screen dimensions ===== */
#define SCR_W 240
#define SCR_H 280

/*-----------------------------------------------------------
 *  LVGL objects
 *----------------------------------------------------------*/
static lv_obj_t *scr;

/* Title & session */
static lv_obj_t *label_title;
static lv_obj_t *label_session;
static lv_obj_t *label_core;

/* Sensor rows */
static lv_obj_t *label_ecg_rate;
static lv_obj_t *label_ppg_rate;
static lv_obj_t *label_imu_rate;

/* Waveforms */
static lv_obj_t *chart_ecg;
static lv_chart_series_t *ser_ecg;
static lv_obj_t *chart_ppg;
static lv_chart_series_t *ser_ppg;

/* IMU motion bars */
static lv_obj_t *bar_imu_ax;
static lv_obj_t *bar_imu_ay;
static lv_obj_t *bar_imu_az;

/* MIC */
static lv_obj_t *label_mic;
static lv_obj_t *bar_mic_vol;

/* SD */
static lv_obj_t *label_sd;
static lv_obj_t *label_csv;

/* Errors */
static lv_obj_t *label_err_i2c;
static lv_obj_t *label_err_sd;
static lv_obj_t *label_err_queue;
static lv_obj_t *label_mic_drop;

/* Page indicator */
static lv_obj_t *label_page;

/*-----------------------------------------------------------
 *  Styles
 *----------------------------------------------------------*/
static lv_style_t style_screen;
static lv_style_t style_title;
static lv_style_t style_core_ok;
static lv_style_t style_core_warn;
static lv_style_t style_core_fail;
static lv_style_t style_rate;
static lv_style_t style_small;
static lv_style_t style_mic_on;
static lv_style_t style_mic_warn;
static lv_style_t style_mic_off;
static lv_style_t style_sd_rec;
static lv_style_t style_sd_saved;
static lv_style_t style_sd_error;

/*-----------------------------------------------------------
 *  Rate tracking (for Hz computation)
 *----------------------------------------------------------*/
static uint32_t rate_last_tick = 0;
static uint32_t rate_last_ecg = 0;
static uint32_t rate_last_ppg = 0;
static uint32_t rate_last_imu = 0;
static uint32_t rate_ecg_hz = 0;
static uint32_t rate_ppg_hz = 0;
static uint32_t rate_imu_hz = 0;

/* MIC tracking */
static uint32_t mic_last_bytes = 0;
static uint32_t mic_rate_bps = 0;

/*-----------------------------------------------------------
 *  Waveform buffers
 *----------------------------------------------------------*/

/*-----------------------------------------------------------
 *  Backlight state
 *----------------------------------------------------------*/
static uint32_t s_last_touch_tick = 0;
static uint8_t  s_backlight_on = 1U;

/*-----------------------------------------------------------
 *  Page system (0=main status, 1=waveforms, 2=files)
 *----------------------------------------------------------*/
static uint8_t s_page = 0;
#define PAGE_COUNT 2

/*-----------------------------------------------------------
 *  Helpers
 *----------------------------------------------------------*/

static void set_backlight_state(uint8_t on)
{
    if (on == s_backlight_on) return;
    s_backlight_on = on;
    LCD_1IN69_SetBackLight(on ? UI_BACKLIGHT_ON : UI_BACKLIGHT_OFF);
}

static void compute_rates(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t dt_ms;

    if (rate_last_tick == 0) {
        rate_last_tick = now;
        rate_last_ecg  = g_ecg_rec.ecg_sample_count;
        rate_last_ppg  = g_ppg_task_call_count;
        rate_last_imu  = g_imu_task_call_count;
        mic_last_bytes = g_ecg_rec.mic_bytes;
        return;
    }

    dt_ms = now - rate_last_tick;
    if (dt_ms < 900) return; /* compute every ~1s */

    /* ECG Hz */
    uint32_t decg = g_ecg_rec.ecg_sample_count - rate_last_ecg;
    rate_ecg_hz = (decg * 1000U) / dt_ms;

    /* PPG Hz (task call count ~= sample rate) */
    uint32_t dppg = g_ppg_task_call_count - rate_last_ppg;
    rate_ppg_hz = (dppg * 1000U) / dt_ms;

    /* IMU Hz */
    uint32_t dimu = g_imu_task_call_count - rate_last_imu;
    rate_imu_hz = (dimu * 1000U) / dt_ms;

    /* MIC bytes/sec */
    uint32_t dmic = g_ecg_rec.mic_bytes - mic_last_bytes;
    mic_rate_bps = (dmic * 1000U) / dt_ms;

    rate_last_tick = now;
    rate_last_ecg  = g_ecg_rec.ecg_sample_count;
    rate_last_ppg  = g_ppg_task_call_count;
    rate_last_imu  = g_imu_task_call_count;
    mic_last_bytes = g_ecg_rec.mic_bytes;
}

static const char *core_status_text(void)
{
    /* Check if any critical failure */
    if (g_ecg_rec.state == ECG_REC_ERROR) return "FAIL";

    /* Check I2C errors */
    if (g_i2c3_rate_iso_error_count > 10) return "FAIL";
    if (g_i2c3_rate_iso_error_count > 2)  return "WARN";

    /* Check queue failures */
    if (g_ecg_rec.ecg_queue_submit_fail > 5) return "FAIL";
    if (g_ecg_rec.ecg_queue_submit_fail > 0) return "WARN";

    /* Check SD write errors */
    if (g_ecg_rec.ecg_sd_write_fail > 3) return "FAIL";
    if (g_ecg_rec.ecg_sd_write_fail > 0) return "WARN";

    /* Check ECG rate */
    if (rate_ecg_hz > 0) {
        if (rate_ecg_hz < 460 || rate_ecg_hz > 520) return "WARN";
    }

    if (g_ecg_rec.state == ECG_REC_RECORDING ||
        g_ecg_rec.state == ECG_REC_STOPPING) {
        return "OK";
    }
    if (g_ecg_rec.state == ECG_REC_STOPPED) return "OK";
    return "OK"; /* idle / boot */
}

static lv_color_t core_status_color(void)
{
    const char *s = core_status_text();
    if (s[0] == 'F') return lv_color_hex(UI_COLOR_RED);
    if (s[0] == 'W') return lv_color_hex(UI_COLOR_YELLOW);
    return lv_color_hex(UI_COLOR_GREEN);
}

static const char *mic_status_text(void)
{
    if (g_ecg_rec.mic_drops > 0) {
        return "WARN";
    }
    if (g_ecg_rec.mic_bytes > 0) {
        if (g_ecg_rec.sd_file_opened) return "WAV";
        return "ON";
    }
    /* Check if MIC should be on by experiment config */
#if RECORD_EXPERIMENT_ENABLE_MIC
    if (g_ecg_rec.state == ECG_REC_RECORDING ||
        g_ecg_rec.state == ECG_REC_STOPPING) {
        return "WARN"; /* MIC enabled but no data */
    }
    return "OFF";
#else
    return "OFF";
#endif
}

static const char *sd_status_text(void)
{
    if (g_ecg_rec.sd_write_bytes > 0 &&
        g_ecg_rec.state == ECG_REC_RECORDING) {
        return "REC";
    }
    if (g_ecg_rec.state == ECG_REC_STOPPED) return "SAVED";
    if (g_ecg_rec.state == ECG_REC_ERROR)   return "ERROR";
    if (g_ecg_rec.sd_file_opened) return "REC";
    return "IDLE";
}

static void format_session_time(char *buf, uint16_t len)
{
    uint32_t elapsed = 0;
    if (g_ecg_rec.state == ECG_REC_RECORDING ||
        g_ecg_rec.state == ECG_REC_STOPPING) {
        if (g_ecg_rec.start_tick > 0) {
            elapsed = HAL_GetTick() - g_ecg_rec.start_tick;
        }
    } else if (g_ecg_rec.state == ECG_REC_STOPPED) {
        if (g_ecg_rec.stop_tick > g_ecg_rec.start_tick) {
            elapsed = g_ecg_rec.stop_tick - g_ecg_rec.start_tick;
        }
    }
    uint32_t sec  = elapsed / 1000U;
    uint32_t min  = sec / 60U;
    uint32_t hour = min / 60U;
    sec  %= 60U;
    min  %= 60U;
    snprintf(buf, len, "%02lu:%02lu:%02lu",
             (unsigned long)hour, (unsigned long)min, (unsigned long)sec);
}

/*-----------------------------------------------------------
 *  UI update timer callback
 *----------------------------------------------------------*/
static void ui_fast_update_cb(lv_timer_t *timer)
{
    (void)timer;
    compute_rates();

    /* --- CORE status --- */
    const char *core = core_status_text();
    if (strcmp(lv_label_get_text(label_core), core) != 0) {
        lv_label_set_text(label_core, core);
        lv_obj_set_style_text_color(label_core, core_status_color(), 0);
    }

    /* --- Session time --- */
    char tbuf[16];
    format_session_time(tbuf, sizeof(tbuf));
    lv_label_set_text(label_session, tbuf);

    /* --- Sensor rates --- */
    lv_label_set_text_fmt(label_ecg_rate, "%lu Hz", (unsigned long)rate_ecg_hz);
    lv_label_set_text_fmt(label_ppg_rate, "%lu Hz", (unsigned long)rate_ppg_hz);
    lv_label_set_text_fmt(label_imu_rate, "%lu Hz", (unsigned long)rate_imu_hz);

    /* --- MIC --- */
    const char *mic_txt = mic_status_text();
    lv_label_set_text_fmt(label_mic, "MIC %s", mic_txt);

    /* --- SD --- */
    lv_label_set_text_fmt(label_sd, "SD %s", sd_status_text());

    /* --- CSV status --- */
    if (g_ecg_rec.sd_write_bytes > 0 && g_ecg_rec.ecg_sd_write_fail == 0) {
        lv_label_set_text(label_csv, "CSV OK");
    } else if (g_ecg_rec.ecg_sd_write_fail > 0) {
        lv_label_set_text(label_csv, "CSV ERR");
    } else {
        lv_label_set_text(label_csv, "CSV --");
    }

    /* --- Error counters --- */
    lv_label_set_text_fmt(label_err_i2c, "I2C:%lu",
        (unsigned long)g_i2c3_rate_iso_error_count);
    lv_label_set_text_fmt(label_err_sd, "SD:%lu",
        (unsigned long)g_ecg_rec.ecg_sd_write_fail);
    lv_label_set_text_fmt(label_err_queue, "Q:%lu",
        (unsigned long)g_ecg_rec.ecg_queue_submit_fail);
    lv_label_set_text_fmt(label_mic_drop, "drop:%lu",
        (unsigned long)g_ecg_rec.mic_drops);

    /* --- Page indicator --- */
    lv_label_set_text_fmt(label_page, "[%u/%u]", s_page + 1, PAGE_COUNT);

    /* MIC volume bar */
    if (mic_rate_bps > 0) {
        uint32_t pct = (mic_rate_bps * 100U) / 16000U;  /* 16KB/s = 100% */
        if (pct > 100) pct = 100;
        lv_bar_set_value(bar_mic_vol, (int32_t)pct, LV_ANIM_OFF);
    } else {
        lv_bar_set_value(bar_mic_vol, 0, LV_ANIM_OFF);
    }
}

/*-----------------------------------------------------------
 *  Waveform update (separate timer, faster)
 *----------------------------------------------------------*/
static void ui_wave_update_cb(lv_timer_t *timer)
{
    (void)timer;

    /* ECG: use ecg_sample_count as pseudo-wave (rolling index) */
    int16_t ecg_val = (int16_t)((g_ecg_rec.ecg_sample_count & 0x3FF) - 512);
    ecg_val = (ecg_val * WAVE_ECG_AMP) / 512;
    lv_chart_set_next_value(chart_ecg, ser_ecg, (lv_coord_t)ecg_val);

    /* PPG: use ppg_task_call_count as pseudo-wave */
    int16_t ppg_val = (int16_t)((g_ppg_task_call_count & 0x1FF) - 256);
    ppg_val = (ppg_val * WAVE_PPG_AMP) / 256;
    lv_chart_set_next_value(chart_ppg, ser_ppg, (lv_coord_t)ppg_val);

    /* IMU bars: use read counts for pseudo motion */
    uint32_t imu_raw = g_imu_read_ok_count & 0x3FF;
    lv_bar_set_value(bar_imu_ax, (int32_t)((imu_raw * 100U) / 1024U), LV_ANIM_OFF);
    lv_bar_set_value(bar_imu_ay, (int32_t)(((imu_raw ^ 0x155) * 100U) / 1024U), LV_ANIM_OFF);
    lv_bar_set_value(bar_imu_az, (int32_t)(((imu_raw ^ 0x2AA) * 100U) / 1024U), LV_ANIM_OFF);
}

/*-----------------------------------------------------------
 *  Init styles
 *----------------------------------------------------------*/
static void init_styles(void)
{
    /* Screen */
    lv_style_init(&style_screen);
    lv_style_set_bg_color(&style_screen, lv_color_hex(UI_COLOR_BG));
    lv_style_set_bg_opa(&style_screen, LV_OPA_COVER);

    /* Title */
    lv_style_init(&style_title);
    lv_style_set_text_color(&style_title, lv_color_hex(UI_COLOR_TEXT));
    lv_style_set_text_font(&style_title, &lv_font_montserrat_14);

    /* CORE status styles */
    lv_style_init(&style_core_ok);
    lv_style_set_text_color(&style_core_ok, lv_color_hex(UI_COLOR_GREEN));
    lv_style_set_text_font(&style_core_ok, &lv_font_montserrat_16);

    lv_style_init(&style_core_warn);
    lv_style_set_text_color(&style_core_warn, lv_color_hex(UI_COLOR_YELLOW));
    lv_style_set_text_font(&style_core_warn, &lv_font_montserrat_16);

    lv_style_init(&style_core_fail);
    lv_style_set_text_color(&style_core_fail, lv_color_hex(UI_COLOR_RED));
    lv_style_set_text_font(&style_core_fail, &lv_font_montserrat_16);

    /* Rate labels */
    lv_style_init(&style_rate);
    lv_style_set_text_color(&style_rate, lv_color_hex(UI_COLOR_TEXT));
    lv_style_set_text_font(&style_rate, &lv_font_montserrat_14);

    /* Small text */
    lv_style_init(&style_small);
    lv_style_set_text_color(&style_small, lv_color_hex(UI_COLOR_MUTED));
    lv_style_set_text_font(&style_small, &lv_font_montserrat_14);

    /* MIC styles */
    lv_style_init(&style_mic_on);
    lv_style_set_text_color(&style_mic_on, lv_color_hex(UI_COLOR_GREEN));
    lv_style_set_text_font(&style_mic_on, &lv_font_montserrat_14);

    lv_style_init(&style_mic_warn);
    lv_style_set_text_color(&style_mic_warn, lv_color_hex(UI_COLOR_YELLOW));
    lv_style_set_text_font(&style_mic_warn, &lv_font_montserrat_14);

    lv_style_init(&style_mic_off);
    lv_style_set_text_color(&style_mic_off, lv_color_hex(UI_COLOR_MUTED));
    lv_style_set_text_font(&style_mic_off, &lv_font_montserrat_14);

    /* SD styles */
    lv_style_init(&style_sd_rec);
    lv_style_set_text_color(&style_sd_rec, lv_color_hex(UI_COLOR_GREEN));
    lv_style_set_text_font(&style_sd_rec, &lv_font_montserrat_14);

    lv_style_init(&style_sd_saved);
    lv_style_set_text_color(&style_sd_saved, lv_color_hex(UI_COLOR_BLUE));
    lv_style_set_text_font(&style_sd_saved, &lv_font_montserrat_14);

    lv_style_init(&style_sd_error);
    lv_style_set_text_color(&style_sd_error, lv_color_hex(UI_COLOR_RED));
    lv_style_set_text_font(&style_sd_error, &lv_font_montserrat_14);
}

/*-----------------------------------------------------------
 *  Create main status page
 *----------------------------------------------------------*/
static void create_page_main(lv_obj_t *parent)
{
    /* Title row */
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Sleep Apnea Monitor Demo");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);

    /* Session time */
    lv_obj_t *sess = lv_label_create(parent);
    lv_label_set_text(sess, "00:00:00");
    lv_obj_add_style(sess, &style_rate, 0);
    lv_obj_align(sess, LV_ALIGN_TOP_MID, 0, 20);

    /* CORE status - big */
    label_core = lv_label_create(parent);
    lv_label_set_text(label_core, "OK");
    lv_obj_add_style(label_core, &style_core_ok, 0);
    lv_obj_align(label_core, LV_ALIGN_TOP_MID, 0, 44);

    /* --- Sensor row labels --- */
    /* ECG */
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "ECG");
    lv_obj_add_style(lbl, &style_small, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 8, 78);

    label_ecg_rate = lv_label_create(parent);
    lv_label_set_text(label_ecg_rate, "--- Hz");
    lv_obj_add_style(label_ecg_rate, &style_rate, 0);
    lv_obj_align(label_ecg_rate, LV_ALIGN_TOP_LEFT, 36, 76);

    /* PPG */
    lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "PPG");
    lv_obj_add_style(lbl, &style_small, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 8, 102);

    label_ppg_rate = lv_label_create(parent);
    lv_label_set_text(label_ppg_rate, "--- Hz");
    lv_obj_add_style(label_ppg_rate, &style_rate, 0);
    lv_obj_align(label_ppg_rate, LV_ALIGN_TOP_LEFT, 36, 100);

    /* IMU */
    lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "IMU");
    lv_obj_add_style(lbl, &style_small, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 8, 126);

    label_imu_rate = lv_label_create(parent);
    lv_label_set_text(label_imu_rate, "--- Hz");
    lv_obj_add_style(label_imu_rate, &style_rate, 0);
    lv_obj_align(label_imu_rate, LV_ALIGN_TOP_LEFT, 36, 124);

    /* --- MIC row --- */
    lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "MIC");
    lv_obj_add_style(lbl, &style_small, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 8, 152);

    label_mic = lv_label_create(parent);
    lv_label_set_text(label_mic, "MIC OFF");
    lv_obj_add_style(label_mic, &style_mic_off, 0);
    lv_obj_align(label_mic, LV_ALIGN_TOP_LEFT, 36, 150);

    label_mic_drop = lv_label_create(parent);
    lv_label_set_text(label_mic_drop, "drop:0");
    lv_obj_add_style(label_mic_drop, &style_small, 0);
    lv_obj_align(label_mic_drop, LV_ALIGN_TOP_RIGHT, -8, 150);

    /* MIC volume bar */
    bar_mic_vol = lv_bar_create(parent);
    lv_obj_set_size(bar_mic_vol, 160, 8);
    lv_obj_align(bar_mic_vol, LV_ALIGN_TOP_MID, 0, 170);
    lv_bar_set_range(bar_mic_vol, 0, 100);
    lv_bar_set_value(bar_mic_vol, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_mic_vol, lv_color_hex(UI_COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_mic_vol, lv_color_hex(UI_COLOR_BAR_MIC), LV_PART_INDICATOR);

    /* --- SD row --- */
    lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "SD");
    lv_obj_add_style(lbl, &style_small, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 8, 188);

    label_sd = lv_label_create(parent);
    lv_label_set_text(label_sd, "SD IDLE");
    lv_obj_add_style(label_sd, &style_sd_rec, 0);
    lv_obj_align(label_sd, LV_ALIGN_TOP_LEFT, 36, 186);

    label_csv = lv_label_create(parent);
    lv_label_set_text(label_csv, "CSV --");
    lv_obj_add_style(label_csv, &style_small, 0);
    lv_obj_align(label_csv, LV_ALIGN_TOP_RIGHT, -8, 186);

    /* --- Error counters --- */
    label_err_i2c = lv_label_create(parent);
    lv_label_set_text(label_err_i2c, "I2C:0");
    lv_obj_add_style(label_err_i2c, &style_small, 0);
    lv_obj_align(label_err_i2c, LV_ALIGN_TOP_LEFT, 8, 208);

    label_err_sd = lv_label_create(parent);
    lv_label_set_text(label_err_sd, "SD:0");
    lv_obj_add_style(label_err_sd, &style_small, 0);
    lv_obj_align(label_err_sd, LV_ALIGN_TOP_LEFT, 68, 208);

    label_err_queue = lv_label_create(parent);
    lv_label_set_text(label_err_queue, "Q:0");
    lv_obj_add_style(label_err_queue, &style_small, 0);
    lv_obj_align(label_err_queue, LV_ALIGN_TOP_LEFT, 120, 208);

    /* Page indicator */
    label_page = lv_label_create(parent);
    lv_label_set_text(label_page, "[1/2]");
    lv_obj_add_style(label_page, &style_small, 0);
    lv_obj_align(label_page, LV_ALIGN_BOTTOM_MID, 0, -4);

    /* Save references */
    label_session = sess;
    label_title   = title;

    /* Start timers */
    lv_timer_create(ui_fast_update_cb, UI_FAST_REFRESH_MS, NULL);
}

/*-----------------------------------------------------------
 *  Create waveforms page
 *----------------------------------------------------------*/
static void create_page_waves(lv_obj_t *parent)
{
    /* ECG chart */
    chart_ecg = lv_chart_create(parent);
    lv_obj_set_size(chart_ecg, 224, 70);
    lv_obj_align(chart_ecg, LV_ALIGN_TOP_MID, 0, 2);
    lv_chart_set_type(chart_ecg, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart_ecg, WAVE_POINTS);
    lv_chart_set_range(chart_ecg, LV_CHART_AXIS_PRIMARY_Y, -WAVE_ECG_AMP, WAVE_ECG_AMP);
    lv_chart_set_div_line_count(chart_ecg, 0, 0);
    lv_obj_set_style_bg_color(chart_ecg, lv_color_hex(UI_COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_line_color(chart_ecg, lv_color_hex(UI_COLOR_LINE), LV_PART_MAIN);
    ser_ecg = lv_chart_add_series(chart_ecg, lv_color_hex(UI_COLOR_WAVE_ECG), LV_CHART_AXIS_PRIMARY_Y);

    /* ECG label */
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "ECG");
    lv_obj_add_style(lbl, &style_small, 0);
    lv_obj_align_to(lbl, chart_ecg, LV_ALIGN_OUT_TOP_LEFT, 4, 0);

    /* PPG chart */
    chart_ppg = lv_chart_create(parent);
    lv_obj_set_size(chart_ppg, 224, 60);
    lv_obj_align(chart_ppg, LV_ALIGN_TOP_MID, 0, 80);
    lv_chart_set_type(chart_ppg, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart_ppg, WAVE_POINTS);
    lv_chart_set_range(chart_ppg, LV_CHART_AXIS_PRIMARY_Y, -WAVE_PPG_AMP, WAVE_PPG_AMP);
    lv_chart_set_div_line_count(chart_ppg, 0, 0);
    lv_obj_set_style_bg_color(chart_ppg, lv_color_hex(UI_COLOR_PANEL), LV_PART_MAIN);
    ser_ppg = lv_chart_add_series(chart_ppg, lv_color_hex(UI_COLOR_WAVE_PPG), LV_CHART_AXIS_PRIMARY_Y);

    lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "PPG");
    lv_obj_add_style(lbl, &style_small, 0);
    lv_obj_align_to(lbl, chart_ppg, LV_ALIGN_OUT_TOP_LEFT, 4, 0);

    /* IMU bars */
    lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "IMU Motion");
    lv_obj_add_style(lbl, &style_small, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 148);

    /* ax */
    lv_obj_t *ax_lbl = lv_label_create(parent);
    lv_label_set_text(ax_lbl, "ax");
    lv_obj_add_style(ax_lbl, &style_small, 0);
    lv_obj_align(ax_lbl, LV_ALIGN_TOP_LEFT, 12, 165);

    bar_imu_ax = lv_bar_create(parent);
    lv_obj_set_size(bar_imu_ax, 180, 10);
    lv_obj_align(bar_imu_ax, LV_ALIGN_TOP_LEFT, 36, 164);
    lv_bar_set_range(bar_imu_ax, 0, 100);
    lv_obj_set_style_bg_color(bar_imu_ax, lv_color_hex(UI_COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_imu_ax, lv_color_hex(UI_COLOR_WAVE_IMU), LV_PART_INDICATOR);

    /* ay */
    lv_obj_t *ay_lbl = lv_label_create(parent);
    lv_label_set_text(ay_lbl, "ay");
    lv_obj_add_style(ay_lbl, &style_small, 0);
    lv_obj_align(ay_lbl, LV_ALIGN_TOP_LEFT, 12, 182);

    bar_imu_ay = lv_bar_create(parent);
    lv_obj_set_size(bar_imu_ay, 180, 10);
    lv_obj_align(bar_imu_ay, LV_ALIGN_TOP_LEFT, 36, 181);
    lv_bar_set_range(bar_imu_ay, 0, 100);
    lv_obj_set_style_bg_color(bar_imu_ay, lv_color_hex(UI_COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_imu_ay, lv_color_hex(UI_COLOR_YELLOW), LV_PART_INDICATOR);

    /* az */
    lv_obj_t *az_lbl = lv_label_create(parent);
    lv_label_set_text(az_lbl, "az");
    lv_obj_add_style(az_lbl, &style_small, 0);
    lv_obj_align(az_lbl, LV_ALIGN_TOP_LEFT, 12, 199);

    bar_imu_az = lv_bar_create(parent);
    lv_obj_set_size(bar_imu_az, 180, 10);
    lv_obj_align(bar_imu_az, LV_ALIGN_TOP_LEFT, 36, 198);
    lv_bar_set_range(bar_imu_az, 0, 100);
    lv_obj_set_style_bg_color(bar_imu_az, lv_color_hex(UI_COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_imu_az, lv_color_hex(UI_COLOR_BLUE), LV_PART_INDICATOR);

    /* MIC volume bar */
    lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "MIC Vol");
    lv_obj_add_style(lbl, &style_small, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 12, 218);

    bar_mic_vol = lv_bar_create(parent);
    lv_obj_set_size(bar_mic_vol, 180, 10);
    lv_obj_align(bar_mic_vol, LV_ALIGN_TOP_LEFT, 36, 217);
    lv_bar_set_range(bar_mic_vol, 0, 100);
    lv_obj_set_style_bg_color(bar_mic_vol, lv_color_hex(UI_COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_mic_vol, lv_color_hex(UI_COLOR_BAR_MIC), LV_PART_INDICATOR);

    /* Page indicator */
    label_page = lv_label_create(parent);
    lv_label_set_text(label_page, "[2/2]");
    lv_obj_add_style(label_page, &style_small, 0);
    lv_obj_align(label_page, LV_ALIGN_BOTTOM_MID, 0, -4);

    /* Start wave timer */
    lv_timer_create(ui_wave_update_cb, UI_WAVE_REFRESH_MS, NULL);
}

/*-----------------------------------------------------------
 *  Touch handler - page switching
 *----------------------------------------------------------*/
static void touch_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    s_last_touch_tick = HAL_GetTick();
    set_backlight_state(1U);

    if (code == LV_EVENT_CLICKED || code == LV_EVENT_PRESSED) {
        s_page = (s_page + 1) % PAGE_COUNT;

        /* Clear and recreate */
        lv_obj_clean(scr);

        if (s_page == 0) {
            create_page_main(scr);
        } else {
            create_page_waves(scr);
        }
    }
}

/*-----------------------------------------------------------
 *  Public API
 *----------------------------------------------------------*/

uint8_t APP_LVGL_NotifyTouchActivity(void)
{
    s_last_touch_tick = HAL_GetTick();
    uint8_t was_off = (s_backlight_on == 0U) ? 1U : 0U;
    set_backlight_state(1U);
    return was_off;
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

    init_styles();

    scr = lv_scr_act();
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(scr, &style_screen, 0);

    /* Touch for page switching */
    lv_obj_add_event_cb(scr, touch_event_cb, LV_EVENT_ALL, NULL);

    /* Create main page */
    s_page = 0;
    create_page_main(scr);
}

void App_LVGL_TestUI(void)
{
    /* v0.4: TestUI is the same as Init - already built above.
     * Called from APP_LVGL_Init() */
}

void APP_LVGL_Process(void)
{
    /* v0.4 demo: keep backlight always on */
    lv_timer_handler();
}

uint32_t APP_LVGL_GetProcessDelayMs(void)
{
    return 10U;  /* v0.4 demo: always fast refresh */
}

/* ===== USB Info press count (kept for backward compat) ===== */
uint32_t g_usb_info_press_count = 0;
