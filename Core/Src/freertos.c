/* USER CODE BEGIN Header */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "usb_printf.h"
#include "stm32l4xx_hal_uart.h"
#include "app_lvgl.h"
#include "max3003.h"
#include "max30102.h"
#include "icm20948.h"
#include "usbd_cdc_if.h"
#include "usbd_core.h"
#include "ecg_record_control.h"
#include "app_log.h"
#include "sd_debug_log.h"
#include "multi_sensor_logger.h"
#include "sd_sensor_logger.h"
#include "audio_recorder.h"
#include "imu_record_mode.h"
#include "record_feature_flags.h"
#include "session_manager.h"
#include "session_manager.h"
#include "i2c.h"
#include "ble_state_machine.h"
/* USER CODE END Includes */
/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define ECG_DEBUG_ISOLATE_TASKS RECORD_ISOLATE_AUX_TASKS
#define ECG_DEBUG_ENABLE_LVGL RECORD_ENABLE_LVGL
#define ECG_DEBUG_ENABLE_PPG (RECORD_DIAG_ECG_ONLY_MODE ? 0 : RECORD_ENABLE_PPG)
#define ECG_DEBUG_ENABLE_ICM (RECORD_DIAG_ECG_ONLY_MODE ? 0 : RECORD_ENABLE_ICM)
#define IMU_IRQ_WAIT_TIMEOUT_MS 500U
#define USB_LOG_FLUSH_TIMEOUT_MS 50U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
/* External variables from main.c */
extern volatile uint8_t ecg_streaming;
extern volatile uint32_t ecg_irq_count;
extern volatile uint32_t icm_irq_count;
extern volatile uint32_t ppg_irq_count;
extern volatile uint32_t touch_irq_count;
extern volatile uint8_t touch_int_flag;
extern uint8_t ble_rx_buf[];
extern volatile uint16_t ble_rx_len;
extern volatile uint8_t ble_rx_flag;
extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef hdma_usart1_rx;

TaskHandle_t EcgTaskHandle = NULL;             /* ECG?????????, ???SR????????? */
TaskHandle_t PpgTaskHandle = NULL;             /* PPG????????? */
TaskHandle_t ImuTaskHandle = NULL;             /* IMU????????? */
TaskHandle_t LvglTaskHandle = NULL;             /* LVGL?????????, ???SR?????*/

/* ECG ?????RAM ?????(????????????????????????????? */
#define ECG_BUFFER_SIZE 10240
int16_t ecg_buffer[ECG_BUFFER_SIZE];
volatile uint32_t ecg_buf_idx = 0;

typedef enum {
    SYS_STATE_IDLE = 0,
    SYS_STATE_RECORDING
} SysState_t;

volatile SysState_t g_sys_state = SYS_STATE_IDLE;
/* USB TX ???????????*/
volatile uint32_t usb_tx_ok_count = 0;
volatile uint32_t usb_tx_busy_count = 0;
volatile uint32_t usb_tx_drop_count = 0;
volatile uint32_t g_imu_read_ok_count = 0;
volatile uint32_t g_imu_read_fail_count = 0;
volatile uint32_t g_icm_init_ret = 0;
volatile uint32_t g_ppg_init_ret = 0;
volatile uint32_t g_ppg_int_wakeup_count = 0;
volatile uint32_t g_ppg_timeout_wakeup_count = 0;
volatile uint32_t g_ppg_timeout_drain_count = 0;
volatile uint32_t g_ppg_task_call_count = 0;
volatile uint32_t g_imu_task_call_count = 0;
volatile uint32_t g_ppg_i2c_mutex_wait_ms_total = 0;
volatile uint32_t g_ppg_i2c_mutex_wait_ms_max = 0;
volatile uint32_t g_ppg_i2c_mutex_hold_ms_total = 0;
volatile uint32_t g_ppg_i2c_mutex_hold_ms_max = 0;
volatile uint32_t g_imu_i2c_mutex_wait_ms_total = 0;
volatile uint32_t g_imu_i2c_mutex_wait_ms_max = 0;
volatile uint32_t g_imu_i2c_mutex_hold_ms_total = 0;
volatile uint32_t g_imu_i2c_mutex_hold_ms_max = 0;
volatile uint32_t g_i2c3_rate_iso_error_count = 0;
static volatile uint8_t s_ppg_ready = 0;
static volatile uint8_t s_icm_ready = 0;
#define USB_LOG_PENDING_SIZE 1536U
static char s_usb_log_pending[USB_LOG_PENDING_SIZE];
static uint16_t s_usb_log_pending_len = 0;
static uint32_t s_usb_log_pending_tick = 0;
static uint8_t s_usb_log_flushing = 0;

/* ?????????????????? (bit0=LVGL, bit1=Sensor, bit2=SDWriter, bit3=USBDump, bit4=Audio, bit5=Button, bit6=BLE) */
volatile uint32_t g_task_create_error = 0;

/* === MAX30102 ?????FIFO ??????????(Sensor ?????????, LCD ????? === */
volatile uint8_t g_ppg_ie1      = 0;  /* INTERRUPT_ENABLE1 readback */
volatile uint8_t g_ppg_is1      = 0;  /* INTERRUPT_STATUS1 readback */
volatile uint8_t g_ppg_fifo_wr  = 0;  /* FIFO_WR_POINTER & 0x1F */
volatile uint8_t g_ppg_fifo_rd  = 0;  /* FIFO_RD_POINTER & 0x1F */
volatile uint8_t g_ppg_fifo_ov  = 0;  /* FIFO_OV_COUNTER & 0x1F */
volatile uint8_t g_ppg_mode     = 0;  /* MODE_CONFIGURATION */
volatile uint8_t g_ppg_fifo_cfg = 0;
volatile uint8_t g_ppg_spo2_cfg = 0;
volatile uint8_t g_icm_who = 0;
volatile uint8_t g_icm_pwr1 = 0;
volatile uint8_t g_icm_int_cfg = 0;
volatile uint8_t g_icm_int_en1 = 0;
volatile uint8_t g_icm_accel_cfg = 0;
volatile uint16_t g_icm_accel_div = 0;
volatile uint8_t g_icm_gyro_cfg = 0;
volatile uint8_t g_icm_gyro_div = 0;
volatile uint8_t g_icm_odr_align = 0;
volatile uint8_t g_icm_user_ctrl = 0;
volatile uint8_t g_icm_lp_config = 0;
volatile uint8_t g_icm_pwr2 = 0;
volatile uint8_t g_icm_fifo_en2 = 0;
volatile uint8_t g_icm_fifo_mode = 0;
volatile uint16_t g_icm_fifo_count = 0;

/* Custom tasks created from USER CODE sections so CubeMX regeneration keeps them. */
osThreadId_t Task_MultiSensor_SDWriterHandle;
const osThreadAttr_t Task_MultiSensor_SDWriter_attributes = {
  .name = "Task_MSWriter",
  .stack_size = 2048 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

osThreadId_t Task_PPGDiagWriterHandle;
const osThreadAttr_t Task_PPGDiagWriter_attributes = {
  .name = "Task_PPGDiagWr",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal7,
};
/* USER CODE END Variables */

/* Definitions for Task_LVGL */
osThreadId_t Task_LVGLHandle;
const osThreadAttr_t Task_LVGL_attributes = {
  .name = "Task_LVGL",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for Task_Sensor */
osThreadId_t Task_SensorHandle;
const osThreadAttr_t Task_Sensor_attributes = {
  .name = "Task_Sensor",
  .stack_size = 2048 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for Task_Audio */
osThreadId_t Task_AudioHandle;
const osThreadAttr_t Task_Audio_attributes = {
  .name = "Task_Audio",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal1,
};
/* Definitions for Task_Button */
osThreadId_t Task_ButtonHandle;
const osThreadAttr_t Task_Button_attributes = {
  .name = "Task_Button",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for Task_BLE */
osThreadId_t Task_BLEHandle;
const osThreadAttr_t Task_BLE_attributes = {
  .name = "Task_BLE",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow7,
};
/* Definitions for Task_PPG */
osThreadId_t Task_PPGHandle;
const osThreadAttr_t Task_PPG_attributes = {
  .name = "Task_PPG",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Task_IMU */
osThreadId_t Task_IMUHandle;
const osThreadAttr_t Task_IMU_attributes = {
  .name = "Task_IMU",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal1,
};
/* Definitions for Mtx_SDCard */
osMutexId_t Mtx_SDCardHandle;
const osMutexAttr_t Mtx_SDCard_attributes = {
  .name = "Mtx_SDCard"
};
osMutexId_t Mtx_I2C3Handle;
const osMutexAttr_t Mtx_I2C3_attributes = {
  .name = "Mtx_I2C3"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void Safe_USB_Printf(const char *format, ...);
static void APP_ICM20948_IntFlagAndPinLevelCheck(void);
static void APP_Log_PPG_INT_Diag_To_SD(uint32_t seq);
static void APP_Report_ECG_Stats(uint8_t force);
static void APP_USB_LogFlush(uint8_t force);
static void APP_I2C3_BusRecover(void);
static uint32_t APP_I2C3_AcquireDiag(uint8_t is_ppg);
static void APP_I2C3_ReleaseDiag(uint8_t is_ppg, uint32_t acquire_tick);
static void APP_Reset_RateIsoDiag(void);
static void APP_Log_RateIsoConfig_To_SD(void);
static void APP_Log_RateIsoDiag_To_SD(void);
/* USER CODE END FunctionPrototypes */

/* USER CODE BEGIN 0 */
void HAL_Delay(uint32_t Delay)
{
  if (osKernelGetState() == osKernelRunning && __get_IPSR() == 0U) {
    osDelay(Delay == 0U ? 1U : Delay);
    return;
  }

  uint32_t tickstart = HAL_GetTick();
  uint32_t wait = Delay;
  if (wait < HAL_MAX_DELAY) {
    wait += 1U;
  }

  while ((HAL_GetTick() - tickstart) < wait) {
  }
}

static void APP_I2C3_BusRecover(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  HAL_I2C_DeInit(&hi2c3);
  __HAL_RCC_GPIOC_CLK_ENABLE();

  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1; /* PC0=SCL, PC1=SDA */
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0 | GPIO_PIN_1, GPIO_PIN_SET);
  HAL_Delay(1);

  for (uint8_t i = 0; i < 9U; i++) {
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, GPIO_PIN_SET);
    HAL_Delay(1);
  }

  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_1, GPIO_PIN_RESET);
  HAL_Delay(1);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, GPIO_PIN_SET);
  HAL_Delay(1);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_1, GPIO_PIN_SET);
  HAL_Delay(1);

  MX_I2C3_Init();
}

static void APP_RequestStopOnModalityError(const char *reason)
{
  if (RECORD_FAIL_FAST_ON_MODALITY_ERROR &&
      g_ecg_rec.state == ECG_REC_RECORDING) {
    SD_DebugLog_WriteLine(reason);
    g_ecg_rec.request_stop = 1;
  }
}

static uint32_t APP_I2C3_AcquireDiag(uint8_t is_ppg)
{
  uint32_t t0 = HAL_GetTick();
  if (Mtx_I2C3Handle != NULL) {
    (void)osMutexAcquire(Mtx_I2C3Handle, osWaitForever);
  }
  {
    uint32_t wait_ms = HAL_GetTick() - t0;
    if (is_ppg) {
      g_ppg_i2c_mutex_wait_ms_total += wait_ms;
      if (wait_ms > g_ppg_i2c_mutex_wait_ms_max) g_ppg_i2c_mutex_wait_ms_max = wait_ms;
    } else {
      g_imu_i2c_mutex_wait_ms_total += wait_ms;
      if (wait_ms > g_imu_i2c_mutex_wait_ms_max) g_imu_i2c_mutex_wait_ms_max = wait_ms;
    }
  }
  return HAL_GetTick();
}

static void APP_I2C3_ReleaseDiag(uint8_t is_ppg, uint32_t acquire_tick)
{
  uint32_t hold_ms = HAL_GetTick() - acquire_tick;
  if (is_ppg) {
    g_ppg_i2c_mutex_hold_ms_total += hold_ms;
    if (hold_ms > g_ppg_i2c_mutex_hold_ms_max) g_ppg_i2c_mutex_hold_ms_max = hold_ms;
  } else {
    g_imu_i2c_mutex_hold_ms_total += hold_ms;
    if (hold_ms > g_imu_i2c_mutex_hold_ms_max) g_imu_i2c_mutex_hold_ms_max = hold_ms;
  }
  if (Mtx_I2C3Handle != NULL) {
    osMutexRelease(Mtx_I2C3Handle);
  }
}

static void APP_Reset_RateIsoDiag(void)
{
  g_ppg_task_call_count = 0;
  g_imu_task_call_count = 0;
  g_ppg_i2c_mutex_wait_ms_total = 0;
  g_ppg_i2c_mutex_wait_ms_max = 0;
  g_ppg_i2c_mutex_hold_ms_total = 0;
  g_ppg_i2c_mutex_hold_ms_max = 0;
  g_imu_i2c_mutex_wait_ms_total = 0;
  g_imu_i2c_mutex_wait_ms_max = 0;
  g_imu_i2c_mutex_hold_ms_total = 0;
  g_imu_i2c_mutex_hold_ms_max = 0;
  g_i2c3_rate_iso_error_count = 0;
}

static void APP_Log_RateIsoConfig_To_SD(void)
{
  uint8_t ppg_ie1 = 0, ppg_fifo = 0, ppg_mode = 0, ppg_spo2 = 0;
  uint8_t icm_who = 0, icm_user_ctrl = 0, icm_lp_config = 0, icm_pwr1 = 0, icm_pwr2 = 0;
  uint8_t icm_int_cfg = 0, icm_int_en1 = 0, icm_fifo_en2 = 0, icm_fifo_mode = 0;
  uint8_t icm_accel_cfg = 0, icm_accel_div_h = 0, icm_accel_div_l = 0;
  uint8_t icm_gyro_cfg = 0, icm_gyro_div = 0, icm_odr_align = 0;
  uint8_t icm_fifo_count_h = 0, icm_fifo_count_l = 0;
  uint32_t t;

  t = APP_I2C3_AcquireDiag(1U);
  if (s_ppg_ready) {
    if (MAX30102_ReadBuffer(INTERRUPT_ENABLE1, &ppg_ie1, 1) != SUCCESS) g_i2c3_rate_iso_error_count++;
    if (MAX30102_ReadBuffer(FIFO_CONFIGURATION, &ppg_fifo, 1) != SUCCESS) g_i2c3_rate_iso_error_count++;
    if (MAX30102_ReadBuffer(MODE_CONFIGURATION, &ppg_mode, 1) != SUCCESS) g_i2c3_rate_iso_error_count++;
    if (MAX30102_ReadBuffer(SPO2_CONFIGURATION, &ppg_spo2, 1) != SUCCESS) g_i2c3_rate_iso_error_count++;
  }
  APP_I2C3_ReleaseDiag(1U, t);

  t = APP_I2C3_AcquireDiag(0U);
  if (s_icm_ready) {
    if (ICM20948_ReadBank0Reg_Checked(REG_WHO_AM_I, &icm_who) != HAL_OK) g_i2c3_rate_iso_error_count++;
    if (ICM20948_ReadBank0Reg_Checked(REG_USER_CTRL, &icm_user_ctrl) != HAL_OK) g_i2c3_rate_iso_error_count++;
    if (ICM20948_ReadBank0Reg_Checked(REG_LP_CONFIG, &icm_lp_config) != HAL_OK) g_i2c3_rate_iso_error_count++;
    if (ICM20948_ReadBank0Reg_Checked(REG_PWR_MGMT_1, &icm_pwr1) != HAL_OK) g_i2c3_rate_iso_error_count++;
    if (ICM20948_ReadBank0Reg_Checked(REG_PWR_MGMT_2, &icm_pwr2) != HAL_OK) g_i2c3_rate_iso_error_count++;
    if (ICM20948_ReadBank0Reg_Checked(REG_INT_PIN_CFG, &icm_int_cfg) != HAL_OK) g_i2c3_rate_iso_error_count++;
    if (ICM20948_ReadBank0Reg_Checked(0x11, &icm_int_en1) != HAL_OK) g_i2c3_rate_iso_error_count++;
    if (ICM20948_ReadBank0Reg_Checked(REG_FIFO_EN_2, &icm_fifo_en2) != HAL_OK) g_i2c3_rate_iso_error_count++;
    if (ICM20948_ReadBank0Reg_Checked(REG_FIFO_MODE, &icm_fifo_mode) != HAL_OK) g_i2c3_rate_iso_error_count++;
    if (ICM20948_ReadBank0Reg_Checked(REG_FIFO_COUNTH, &icm_fifo_count_h) != HAL_OK) g_i2c3_rate_iso_error_count++;
    if (ICM20948_ReadBank0Reg_Checked((uint8_t)(REG_FIFO_COUNTH + 1U), &icm_fifo_count_l) != HAL_OK) g_i2c3_rate_iso_error_count++;
    if (ICM20948_ReadBank2Reg_Checked(0x14, &icm_accel_cfg) != HAL_OK) g_i2c3_rate_iso_error_count++;
    if (ICM20948_ReadBank2Reg_Checked(0x10, &icm_accel_div_h) != HAL_OK) g_i2c3_rate_iso_error_count++;
    if (ICM20948_ReadBank2Reg_Checked(0x11, &icm_accel_div_l) != HAL_OK) g_i2c3_rate_iso_error_count++;
    if (ICM20948_ReadBank2Reg_Checked(0x01, &icm_gyro_cfg) != HAL_OK) g_i2c3_rate_iso_error_count++;
    if (ICM20948_ReadBank2Reg_Checked(0x00, &icm_gyro_div) != HAL_OK) g_i2c3_rate_iso_error_count++;
    if (ICM20948_ReadBank2Reg_Checked(0x09, &icm_odr_align) != HAL_OK) g_i2c3_rate_iso_error_count++;
  }
  APP_I2C3_ReleaseDiag(0U, t);

  {
    char line[512];
    g_ppg_ie1 = ppg_ie1;
    g_ppg_fifo_cfg = ppg_fifo;
    g_ppg_mode = ppg_mode;
    g_ppg_spo2_cfg = ppg_spo2;
    g_icm_who = icm_who;
    g_icm_user_ctrl = icm_user_ctrl;
    g_icm_lp_config = icm_lp_config;
    g_icm_pwr1 = icm_pwr1;
    g_icm_pwr2 = icm_pwr2;
    g_icm_int_cfg = icm_int_cfg;
    g_icm_int_en1 = icm_int_en1;
    g_icm_fifo_en2 = icm_fifo_en2;
    g_icm_fifo_mode = icm_fifo_mode;
    g_icm_fifo_count = (uint16_t)(((uint16_t)icm_fifo_count_h << 8) | icm_fifo_count_l);
    g_icm_accel_cfg = icm_accel_cfg;
    g_icm_accel_div = (uint16_t)(((uint16_t)icm_accel_div_h << 8) | icm_accel_div_l);
    g_icm_gyro_cfg = icm_gyro_cfg;
    g_icm_gyro_div = icm_gyro_div;
    g_icm_odr_align = icm_odr_align;
    int n = snprintf(line, sizeof(line),
      "RATE_ISO_CONFIG,case=%u,label=%s,ecg=%u,ppg=%u,icm=%u,audio=%u,no_sd=%u,ppg_avg1=%u,icm_batch=%u,"
      "ppg_ie1=0x%02X,ppg_fifo_cfg=0x%02X,ppg_mode=0x%02X,ppg_spo2=0x%02X,"
      "icm_who=0x%02X,icm_user_ctrl=0x%02X,icm_lp_config=0x%02X,icm_pwr1=0x%02X,icm_pwr2=0x%02X,"
      "icm_int_cfg=0x%02X,icm_int_en1=0x%02X,icm_fifo_en2=0x%02X,icm_fifo_mode=0x%02X,icm_fifo_count=%u,"
      "icm_accel_cfg=0x%02X,icm_accel_div=%u,icm_gyro_cfg=0x%02X,icm_gyro_div=%u,icm_odr_align=0x%02X",
      (unsigned)RECORD_RATE_ISO_CASE, RECORD_RATE_ISO_LABEL,
      (unsigned)RECORD_RATE_ISO_ENABLE_ECG, (unsigned)RECORD_ENABLE_PPG,
      (unsigned)RECORD_ENABLE_ICM, (unsigned)RECORD_ENABLE_AUDIO,
      (unsigned)RECORD_RATE_ISO_NO_SD, (unsigned)RECORD_RATE_ISO_PPG_AVG1,
      (unsigned)RECORD_RATE_ISO_ICM_BATCH,
      ppg_ie1, ppg_fifo, ppg_mode, ppg_spo2,
      icm_who, icm_user_ctrl, icm_lp_config, icm_pwr1, icm_pwr2,
      icm_int_cfg, icm_int_en1, icm_fifo_en2, icm_fifo_mode, (unsigned)g_icm_fifo_count,
      icm_accel_cfg, (unsigned)g_icm_accel_div,
      icm_gyro_cfg, (unsigned)icm_gyro_div, icm_odr_align);
    if (n > 0 && n < (int)sizeof(line)) {
      SD_DebugLog_WriteLine(line);
    }
  }
}

static void APP_Log_RateIsoDiag_To_SD(void)
{
  char line[320];
  int n = snprintf(line, sizeof(line),
    "RATE_ISO_I2C,ppg_task_calls=%lu,imu_task_calls=%lu,"
    "ppg_mutex_wait_ms=%lu,ppg_mutex_wait_max=%lu,ppg_mutex_hold_ms=%lu,ppg_mutex_hold_max=%lu,"
    "imu_mutex_wait_ms=%lu,imu_mutex_wait_max=%lu,imu_mutex_hold_ms=%lu,imu_mutex_hold_max=%lu,"
    "i2c_errors=%lu",
    (unsigned long)g_ppg_task_call_count,
    (unsigned long)g_imu_task_call_count,
    (unsigned long)g_ppg_i2c_mutex_wait_ms_total,
    (unsigned long)g_ppg_i2c_mutex_wait_ms_max,
    (unsigned long)g_ppg_i2c_mutex_hold_ms_total,
    (unsigned long)g_ppg_i2c_mutex_hold_ms_max,
    (unsigned long)g_imu_i2c_mutex_wait_ms_total,
    (unsigned long)g_imu_i2c_mutex_wait_ms_max,
    (unsigned long)g_imu_i2c_mutex_hold_ms_total,
    (unsigned long)g_imu_i2c_mutex_hold_ms_max,
    (unsigned long)g_i2c3_rate_iso_error_count);
  if (n > 0 && n < (int)sizeof(line)) {
    SD_DebugLog_WriteLine(line);
  }
}

static void APP_I2C3_RecoverRecordingSensors(void)
{
  APP_I2C3_BusRecover();
  if (s_ppg_ready) {
    uint8_t s1, s2;
    (void)MAX30102_ClearInterruptStatus(&s1, &s2);
    (void)MAX30102_EnableFifoAlmostFullInterrupt();
  }
  if (s_icm_ready) {
    ICM20948_ClearInterruptStatus();
    ICM20948_EnableDataReadyInterrupt();
  }
}

static uint8_t APP_EnsureRequiredModalitiesReady(void)
{
#if ECG_DEBUG_ENABLE_ICM
  if (RECORD_REQUIRE_ICM_FOR_FOUR_MODAL && !s_icm_ready) {
    uint8_t icm_ret = 1U;
    for (uint32_t attempt = 0; attempt < RECORD_ICM_INIT_RETRY_COUNT; attempt++) {
      APP_I2C3_BusRecover();
      icm_ret = ICM20948_Init();
      if (icm_ret == 0U) {
        break;
      }
      osDelay(20);
    }
    g_icm_init_ret = icm_ret;
    s_icm_ready = (icm_ret == 0U) ? 1U : 0U;
    if (!s_icm_ready) {
      SD_DebugLog_WriteLine("START_BLOCKED_ICM_NOT_READY");
      Safe_USB_Printf("[REC][ERR] ICM not ready, block four-modal start ret=%u\r\n",
                      (unsigned int)icm_ret);
      return 0U;
    }
  }
#endif

#if ECG_DEBUG_ENABLE_PPG
  if (RECORD_REQUIRE_PPG_FOR_FOUR_MODAL && !s_ppg_ready) {
    MAX30102_InitResult_t ppg_ret = MAX30102_INIT_NOT_FOUND;
    for (uint32_t attempt = 0; attempt < RECORD_PPG_INIT_RETRY_COUNT; attempt++) {
      APP_I2C3_BusRecover();
      ppg_ret = MAX30102_Init();
      if (ppg_ret == MAX30102_INIT_OK) {
        break;
      }
      osDelay(20);
    }
    g_ppg_init_ret = (uint32_t)ppg_ret;
    s_ppg_ready = (ppg_ret == MAX30102_INIT_OK) ? 1U : 0U;
    if (!s_ppg_ready) {
      APP_I2C3_BusRecover();
      SD_DebugLog_WriteLine("START_BLOCKED_PPG_NOT_READY");
      Safe_USB_Printf("[REC][ERR] PPG not ready, block four-modal start ret=%d\r\n",
                      (int)ppg_ret);
      return 0U;
    }
  }
#endif

  return 1U;
}
/* USER CODE END 0 */

void StartTask_LVGL(void *argument);
void StartTask_Sensor(void *argument);
void StartTask_Button(void *argument);
void StartTask_PPG(void *argument);
void StartTask_IMU(void *argument);
void StartTask_Audio(void *argument);

void MX_FREERTOS_Init(void);

/**
  * @brief  FreeRTOS initialization
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* Create the mutex(es) */
  Mtx_SDCardHandle = osMutexNew(&Mtx_SDCard_attributes);
  Mtx_I2C3Handle = RECORD_USE_I2C3_MUTEX ? osMutexNew(&Mtx_I2C3_attributes) : NULL;

  /* USER CODE BEGIN RTOS_MUTEX */
  if (RECORD_USE_I2C3_MUTEX && Mtx_I2C3Handle == NULL) {
    g_task_create_error |= (1UL << 9);
  }
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  MultiSensorLogger_InitQueue();
  PPGDiag_InitQueue();
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of Task_LVGL */
  Task_LVGLHandle = ECG_DEBUG_ENABLE_LVGL ? osThreadNew(StartTask_LVGL, NULL, &Task_LVGL_attributes) : NULL;

  /* creation of Task_Sensor */
  Task_SensorHandle = osThreadNew(StartTask_Sensor, NULL, &Task_Sensor_attributes);

  /* creation of Task_Button */
  Task_ButtonHandle = osThreadNew(StartTask_Button, NULL, &Task_Button_attributes);

  /* PPG ????????? */
  Task_PPGHandle = ECG_DEBUG_ENABLE_PPG ? osThreadNew(StartTask_PPG, NULL, &Task_PPG_attributes) : NULL;

  /* IMU ????????? */
  Task_IMUHandle = ECG_DEBUG_ENABLE_ICM ? osThreadNew(StartTask_IMU, NULL, &Task_IMU_attributes) : NULL;

  /* USER CODE BEGIN RTOS_THREADS */
  if (ECG_DEBUG_ENABLE_LVGL && Task_LVGLHandle == NULL) g_task_create_error |= (1UL << 0);
  if (Task_SensorHandle == NULL) g_task_create_error |= (1UL << 1);
  if (Task_ButtonHandle == NULL) g_task_create_error |= (1UL << 5);
  if (ECG_DEBUG_ENABLE_PPG && Task_PPGHandle == NULL) g_task_create_error |= (1UL << 6);
  if (ECG_DEBUG_ENABLE_ICM && Task_IMUHandle == NULL) g_task_create_error |= (1UL << 7);

  Task_AudioHandle = RECORD_DIAG_AUDIO_TASK_CREATE ? osThreadNew(StartTask_Audio, NULL, &Task_Audio_attributes) : NULL;
  if (RECORD_DIAG_AUDIO_TASK_CREATE && Task_AudioHandle == NULL) g_task_create_error |= (1UL << 4);

  /* ????????? SD Writer (?????????ECG_SDWriter) */
  Task_MultiSensor_SDWriterHandle = osThreadNew(StartTask_MultiSensor_SDWriter, NULL, &Task_MultiSensor_SDWriter_attributes);
  if (Task_MultiSensor_SDWriterHandle == NULL) g_task_create_error |= (1UL << 2);

  /* PPG INT ?????SD Writer */
  Task_PPGDiagWriterHandle = ECG_DEBUG_ISOLATE_TASKS ? NULL : osThreadNew(StartTask_PPGDiagWriter, NULL, &Task_PPGDiagWriter_attributes);
  if (!ECG_DEBUG_ISOLATE_TASKS && Task_PPGDiagWriterHandle == NULL) g_task_create_error |= (1UL << 8);

  /* USER CODE END RTOS_THREADS */
}

/* USER CODE BEGIN Header_StartTask_LVGL */
/* USER CODE END Header_StartTask_LVGL */
void StartTask_LVGL(void *argument)
{
  /* USER CODE BEGIN StartTask_LVGL */
  (void)argument;
  LvglTaskHandle = xTaskGetCurrentTaskHandle();

  /* 1. ?????USB ??????????*/
  osDelay(2000);

  /* 2. ????????? UART ????????? DMA ?????*/
#if !ECG_DEBUG_ISOLATE_TASKS
  HAL_UARTEx_ReceiveToIdle_DMA(&huart1, ble_rx_buf, BLE_RX_BUF_SIZE);
  __HAL_DMA_DISABLE_IT(&hdma_usart1_rx, DMA_IT_HT);
#endif

  /* 3. USB CDC ???????? ?????USB ???????*/
  APP_USB_LOG("\r\n[SYS] RTOS Started, USB CDC Ready!\r\n");

  /* 4. ???????LVGL ?????+ V1 ECG ?????????*/
  Safe_USB_Printf("[LVGL] APP_LVGL_Init start\r\n");
  APP_LVGL_Init();
  Safe_USB_Printf("[LVGL] APP_LVGL_Init done\r\n");

  for(;;) {
    {
      static uint32_t _last_touch_print_tick = 0;
      uint32_t _now = HAL_GetTick();
      if (_now - _last_touch_print_tick >= 5000) {
        _last_touch_print_tick = _now;
        Safe_USB_Printf("[TOUCH] irq=%lu flag=%u pin=%u\r\n",
                        (unsigned long)touch_irq_count, (unsigned int)touch_int_flag,
                        (unsigned int)HAL_GPIO_ReadPin(INT_TOUCH_GPIO_Port, INT_TOUCH_Pin));
      }
    }
    APP_LVGL_Process();
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(APP_LVGL_GetProcessDelayMs()));
  }
  /* USER CODE END StartTask_LVGL */
}

/* USER CODE BEGIN Header_StartTask_Sensor */
/* USER CODE END Header_StartTask_Sensor */
void StartTask_Sensor(void *argument)
{
  /* USER CODE BEGIN StartTask_Sensor */
  (void)argument;

  EcgTaskHandle = xTaskGetCurrentTaskHandle();

  osDelay(3000);

  /* SD debug log ?????????RTOS ????????????????????f_mount */
  SD_DebugLog_Init();

  APP_USB_LOG("[ECG_V1] Sensor task started\r\n");

  /* I2C3 ????????????????????????????????ICM???????PPG/TXS ?????????????????? IMU ???????*/
  MAX30102_InitResult_t ppg_ret;
  uint8_t icm_ret;
#if ECG_DEBUG_ENABLE_ICM
  icm_ret = 1;
  for (uint32_t attempt = 0; attempt < RECORD_ICM_INIT_RETRY_COUNT; attempt++) {
      APP_I2C3_BusRecover();
      icm_ret = ICM20948_Init();
      if (icm_ret == 0U) {
          break;
      }
      osDelay(20);
  }
#else
  icm_ret = 1;
#endif
  g_icm_init_ret = icm_ret;
  s_icm_ready = (icm_ret == 0) ? 1U : 0U;

#if ECG_DEBUG_ENABLE_PPG
  ppg_ret = MAX30102_INIT_NOT_FOUND;
  for (uint32_t attempt = 0; attempt < RECORD_PPG_INIT_RETRY_COUNT; attempt++) {
      APP_I2C3_BusRecover();
      ppg_ret = MAX30102_Init();
      if (ppg_ret == MAX30102_INIT_OK) {
          break;
      }
      osDelay(20);
  }
  g_ppg_init_ret = (uint32_t)ppg_ret;
  if (ppg_ret != MAX30102_INIT_OK) {
      /* PPG ???????????STM32 ??I2C3?????????????????????????????ICM ??????????????*/
      APP_I2C3_BusRecover();
  }
#else
  ppg_ret = MAX30102_INIT_NOT_FOUND;
  g_ppg_init_ret = (uint32_t)ppg_ret;
#endif
  s_ppg_ready = (ppg_ret == MAX30102_INIT_OK) ? 1U : 0U;

  Safe_USB_Printf("\r\n[SENSOR_INIT]\r\n");
  if (ppg_ret == MAX30102_INIT_OK) {
      Safe_USB_Printf("[MAX30102] I2C CALL OK, INIT OK\r\n");
      /* ????????????????????????????????????????????????????????????????RTOS ???????
       * ????????????????recording start ???????INT_STATUS1 ???????????*/
      // MAX30102_Debug_Poll_INT_Pin();
  }
  else if (ppg_ret == MAX30102_INIT_NOT_FOUND)
      Safe_USB_Printf("[MAX30102] I2C CALL FAIL, DEVICE NOT FOUND\r\n");
  else if (ppg_ret == MAX30102_INIT_CONFIG_FAILED)
      Safe_USB_Printf("[MAX30102] I2C CALL OK BUT CONFIG FAILED\r\n");
  else
      Safe_USB_Printf("[MAX30102] UNKNOWN INIT RESULT=%d\r\n", (int)ppg_ret);

  if (icm_ret == 0) {
      Safe_USB_Printf("[ICM20948] INIT OK (WHO_AM_I + 10x probe passed)\r\n");
  } else {
      Safe_USB_Printf("[ICM20948] INIT FAILED, RET=%u"
                      " (1=WHO_AM_I,2=PWR,3=Bank2,4=INT_CFG,5=VerifyRd,6=VerifyVal,7=10xWHO)\r\n",
                      (unsigned int)icm_ret);
  }
  Safe_USB_Printf("[/SENSOR_INIT]\r\n");

  if (ppg_ret == MAX30102_INIT_OK)          SD_DebugLog_WriteLine("MAX30102_INIT_OK");
  else if (ppg_ret == MAX30102_INIT_NOT_FOUND) SD_DebugLog_WriteLine("MAX30102_INIT_NOT_FOUND");
  else                                       SD_DebugLog_WriteLine("MAX30102_INIT_CONFIG_FAILED");

  if (icm_ret == 0) {
      SD_DebugLog_WriteLine("ICM20948_INIT_OK");
  } else {
      char icm_err[32];
      snprintf(icm_err, sizeof(icm_err), "ICM20948_INIT_FAILED,RET=%u", (unsigned int)icm_ret);
      SD_DebugLog_WriteLine(icm_err);
  }

  /* ICM20948 ?????????+?????????????????????ICM ???????????????????????*/
  if (!ECG_DEBUG_ISOLATE_TASKS && ECG_DEBUG_ENABLE_ICM && icm_ret == 0) {
      APP_ICM20948_IntFlagAndPinLevelCheck();
  }

  /* ECG ??????????????????PPG/ICM ?????*/
  if (RECORD_RATE_ISO_ENABLE_ECG) {
    Safe_USB_Printf("[SENSOR] before MAX30003_Init\r\n");
    MAX30003_Init();
    MAX30003_DiagLog_Init();
    Safe_USB_Printf("[SENSOR] after MAX30003_Init\r\n");
    MAX30003_PollLeadStatus();
  } else {
    SD_DebugLog_WriteLine("RATE_ISO_ECG_DISABLED");
  }

  /* Diagnostic: explicit EN_MIC control for Case B/D/E/F */
#if RECORD_DIAG_AUDIO_EN_MIC_ON
  HAL_GPIO_WritePin(EN_MIC_GPIO_Port, EN_MIC_Pin, GPIO_PIN_SET);
  SD_DebugLog_WriteLine("EN_MIC_ON_BY_DIAG");
#else
  HAL_GPIO_WritePin(EN_MIC_GPIO_Port, EN_MIC_Pin, GPIO_PIN_RESET);
#endif
  SD_DebugLog_WriteLine("MAX30003_INIT_DONE");
  Safe_USB_Printf("[SENSOR] after MAX30003_INIT_DONE log\r\n");

  /* ???????????*/
  ecg_streaming = 0;
  g_sys_state = SYS_STATE_IDLE;
  uint8_t finalize_after_stop = 0;

  /* UI only stops recording; each boot starts one recording automatically. */
  g_ecg_rec.requested_record_ms = RECORD_DEFAULT_RECORD_MS;
  g_ecg_rec.request_start = 1;

  for (;;) {
    APP_USB_LogFlush(0);

    if (g_ecg_rec.request_start) {
      g_ecg_rec.request_start = 0;

      if (g_ecg_rec.state == ECG_REC_IDLE ||
          g_ecg_rec.state == ECG_REC_STOPPED ||
          g_ecg_rec.state == ECG_REC_ERROR) {

        if (!APP_EnsureRequiredModalitiesReady()) {
          g_ecg_rec.state = ECG_REC_ERROR;
          g_ecg_rec.auto_stop_ms = 0;
          g_sys_state = SYS_STATE_IDLE;
          continue;
        }

        ECG_UpdateFileName();
        SD_DebugLog_StartNewFile(g_ecg_rec.file_seq);
        Session_Reset();
        g_ecg_rec.state = ECG_REC_RECORDING;

        MultiSensorLogger_ResetForNewRecording();

        /* ?????MSWriter ????????????????3000ms */
        uint32_t t0 = HAL_GetTick();
        while (!g_ecg_rec.sd_file_opened && (HAL_GetTick() - t0 < 3000)) {
            osDelay(10);
        }

        if (!g_ecg_rec.sd_file_opened) {
            SD_DebugLog_WriteLine("ERROR_SD_OPEN_TIMEOUT");
            Safe_USB_Printf("[SD_OPEN_FAIL]\r\n");
            g_ecg_rec.state = ECG_REC_ERROR;
            g_ecg_rec.sd_file_closed = 1;
            g_ecg_rec.auto_stop_ms = 0;
            g_sys_state = SYS_STATE_IDLE;
            continue;
            /* ????????????????SD ????????????????????????????????*/
        }

        Session_Create(HAL_GetTick());
        Safe_USB_Printf("[REC] session=%s\r\n", g_session.session_id);
        Safe_USB_Printf("[REC] record start file_opened=%u tick=%lu\r\n",
                        g_ecg_rec.sd_file_opened,
                        (unsigned long)HAL_GetTick());

        ecg_buf_idx = 0;
        g_sys_state = SYS_STATE_RECORDING;
        ECG_ResetStats();
        if (RECORD_DIAG_AUDIO_TASK_CREATE) {
          uint32_t wait0 = HAL_GetTick();
          while (g_ecg_rec.mic_file_open_tick == 0U &&
                 (HAL_GetTick() - wait0) < 3000U &&
                 g_ecg_rec.state == ECG_REC_RECORDING) {
            osDelay(5);
          }
        }

        while (ulTaskNotifyTake(pdTRUE, 0) > 0) {}
        if (PpgTaskHandle != NULL) {
          xTaskNotifyStateClear(PpgTaskHandle);
        }
        if (ImuTaskHandle != NULL) {
          xTaskNotifyStateClear(ImuTaskHandle);
        }
        __HAL_GPIO_EXTI_CLEAR_IT(ECG_INT_Pin);
        __HAL_GPIO_EXTI_CLEAR_IT(PPG_INT_Pin);
        __HAL_GPIO_EXTI_CLEAR_IT(ICM_INT_Pin);

        // [DEBUG] ?????PPG/ICM I2C ???????????I2C NACK ?????
        // {
        //     uint8_t s1, s2;
        //     MAX30102_ClearInterruptStatus(&s1, &s2);
        // }
        uint32_t i2c_start_t = APP_I2C3_AcquireDiag(1U);
        if (s_ppg_ready) {
          uint8_t s1, s2;
          (void)MAX30102_ClearInterruptStatus(&s1, &s2);
        }
        if (s_icm_ready) {
          ICM20948_ClearInterruptStatus();
        }
        if (s_ppg_ready) {
          (void)MAX30102_EnableFifoAlmostFullInterrupt();
        }
        APP_I2C3_ReleaseDiag(1U, i2c_start_t);

        g_imu_read_ok_count = 0;
        g_imu_read_fail_count = 0;
        g_max30102_fifo_read_ok_count = 0;
        g_max30102_fifo_read_fail_count = 0;
        g_max30102_fifo_empty_count = 0;
        g_max30102_fifo_ov_count = 0;
        g_ppg_int_wakeup_count = 0;
        g_ppg_timeout_wakeup_count = 0;
        g_ppg_timeout_drain_count = 0;
        icm_irq_count = 0;
        ppg_irq_count = 0;
        APP_Reset_RateIsoDiag();
        APP_Log_RateIsoConfig_To_SD();
        /* AudioTask stays at creation priority Normal1 ?????must be below SensorTask AboveNormal */
        (void)Task_AudioHandle;
        if (RECORD_RATE_ISO_ENABLE_ECG) {
          Safe_USB_Printf("[REC] before MAX30003_StartStream tick=%lu\r\n",
                          (unsigned long)HAL_GetTick());
          MAX30003_StartStream();
          g_ecg_rec.ecg_stream_start_tick = HAL_GetTick();
          g_ecg_rec.start_tick = g_ecg_rec.ecg_stream_start_tick;
        } else {
          g_ecg_rec.ecg_stream_start_tick = HAL_GetTick();
          g_ecg_rec.start_tick = g_ecg_rec.ecg_stream_start_tick;
          SD_DebugLog_WriteLine("RATE_ISO_SYNTHETIC_STREAM_START");
        }
        ecg_streaming = 1;
        if (s_icm_ready) {
          if (RECORD_RATE_ISO_ICM_BATCH) {
            if (ICM20948_EnableAccelGyroFifo() != 0U) {
              g_i2c3_rate_iso_error_count++;
            }
          } else {
            ICM20948_EnableDataReadyInterrupt();
          }
        }
        APP_Log_RateIsoConfig_To_SD();
        Safe_USB_Printf("[REC] after stream start tick=%lu start_tick=%lu\r\n",
                        (unsigned long)HAL_GetTick(),
                        (unsigned long)g_ecg_rec.start_tick);
        g_ecg_rec.auto_stop_ms = g_ecg_rec.requested_record_ms;
        g_ecg_rec.requested_record_ms = 0;
        /* ECG CDC stats are emitted once per second by APP_Report_ECG_Stats. */
      }
    }

    /* ????????????????????*/
    if (g_ecg_rec.auto_stop_ms > 0 &&
        g_ecg_rec.state == ECG_REC_RECORDING) {
        if (HAL_GetTick() - g_ecg_rec.start_tick >= g_ecg_rec.auto_stop_ms) {
            Safe_USB_Printf("[ECG_AUTO_STOP] t=%lu\r\n",
                            (unsigned long)HAL_GetTick());
            g_ecg_rec.request_stop = 1;
            g_ecg_rec.auto_stop_ms = 0;
        }
    }

    if (g_ecg_rec.request_stop) {
      g_ecg_rec.request_stop = 0;

      if (g_ecg_rec.state == ECG_REC_RECORDING) {
        ecg_streaming = 0;
        if (RECORD_RATE_ISO_ENABLE_ECG) {
          MAX30003_StopStream();
        }
        g_ecg_rec.ecg_stream_stop_tick = HAL_GetTick();

        uint32_t i2c_stop_t = APP_I2C3_AcquireDiag(1U);
        if (s_ppg_ready) {
          (void)MAX30102_DisableInterrupts();
        }
        if (s_icm_ready) {
          ICM20948_DisableDataReadyInterrupt();
        }
        APP_I2C3_ReleaseDiag(1U, i2c_stop_t);

        // [DEBUG] ?????I2C ????????? NACK ?????
        // MAX30102_DisableInterrupts();

        MultiSensorLogger_RequestStopAndFlush();
        g_ecg_rec.state = ECG_REC_STOPPING;
        finalize_after_stop = 1;

        if (RECORD_RATE_ISO_ENABLE_ECG) {
          MAX30003_DiagLog_Stop();
        }
        APP_Log_RateIsoDiag_To_SD();
        Safe_USB_Printf("[REC_STOP]\r\n");
      }
    }

    if (finalize_after_stop &&
        g_ecg_rec.sd_file_closed &&
        !AudioRecorder_IsActive() &&
        (g_ecg_rec.state == ECG_REC_STOPPING ||
         g_ecg_rec.state == ECG_REC_STOPPED)) {
        finalize_after_stop = 0;
        g_ecg_rec.state = ECG_REC_STOPPED;
      g_session.tick_end_ms = HAL_GetTick();
      g_session.duration_ms = g_session.tick_end_ms - g_session.tick_start_ms;
      if (RECORD_RATE_ISO_NO_SD) {
        (void)MultiSensorLogger_WriteRateIsoNoSdCsv();
      }
      Session_WriteMeta();
      Session_WriteDiagSummary();
      Session_WriteModalitySummary();
      g_session.tick_end_ms = HAL_GetTick();
      g_session.duration_ms = g_session.tick_end_ms - g_session.tick_start_ms;
      Session_WriteMeta();
      Session_WriteDiagSummary();
        SD_DebugLog_WriteSnapshot();
        SD_DebugLog_WriteSessionSummary();
        APP_USB_LogFlush(1);
    }

            if (ecg_streaming && g_ecg_rec.state == ECG_REC_RECORDING) {
        /* DIAG: checkpoint ?????time since last recording loop entry */
        {
            static uint32_t _last_rec_entry_tick = 0;
            uint32_t _now_entry = HAL_GetTick();
            if (_last_rec_entry_tick != 0) {
                uint32_t _entry_gap = _now_entry - _last_rec_entry_tick;
                if (_entry_gap > g_ecg_rec.diag_sensor_task_loop_us_max + 20) {
                    /* Gap larger than expected loop time + margin */
                }
            }
            _last_rec_entry_tick = _now_entry;
        }
        uint32_t _t_loop_0 = HAL_GetTick();
        static uint32_t last_loop_log = 0;
        uint32_t now_tick = HAL_GetTick();
        if (0 && now_tick - last_loop_log >= 2000) {
          last_loop_log = now_tick;
          Safe_USB_Printf("[LOOP] tick=%lu\r\n", (unsigned long)now_tick);
        }
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(8)) > 0) {
          g_ecg_rec.diag_notify_wakes++;
        } else {
          g_ecg_rec.diag_notify_timeouts++;
        }
        uint32_t _t_max30003_0 = HAL_GetTick();
        if (RECORD_RATE_ISO_ENABLE_ECG) {
          MAX30003_Task();
          MAX30003_DiagLog_Run();
        }
        {
          uint32_t _elapsed = HAL_GetTick() - _t_max30003_0;
          if (_elapsed > g_ecg_rec.diag_max3003_task_us_max) g_ecg_rec.diag_max3003_task_us_max = _elapsed;
          g_ecg_rec.diag_max3003_task_us_last = _elapsed;
        }
        /* Loop timing */
        {
          uint32_t _t_loop_elapsed = HAL_GetTick() - _t_loop_0;
          if (_t_loop_elapsed > g_ecg_rec.diag_sensor_task_loop_us_max) g_ecg_rec.diag_sensor_task_loop_us_max = _t_loop_elapsed;
          g_ecg_rec.diag_sensor_task_loop_us_last = _t_loop_elapsed;
        }
      } else {
        osDelay(20);
      }

    /* ???????????????????(4Hz)???dle ???????????*/
    static uint32_t last_lead_poll = 0;
    if (HAL_GetTick() - last_lead_poll >= 250) {
        last_lead_poll = HAL_GetTick();
        if (RECORD_RATE_ISO_ENABLE_ECG) {
          MAX30003_PollLeadStatus();
        }

  /* Diagnostic: explicit EN_MIC control for Case B/D/E/F */
#if RECORD_DIAG_AUDIO_EN_MIC_ON
  HAL_GPIO_WritePin(EN_MIC_GPIO_Port, EN_MIC_Pin, GPIO_PIN_SET);
  SD_DebugLog_WriteLine("EN_MIC_ON_BY_DIAG");
#else
  HAL_GPIO_WritePin(EN_MIC_GPIO_Port, EN_MIC_Pin, GPIO_PIN_RESET);
#endif
    }

    if (g_ecg_rec.request_save_info) {
      g_ecg_rec.request_save_info = 0;
      SD_DebugLog_WriteSnapshot();
    }

    if (g_ecg_rec.request_usb_info) {
      g_ecg_rec.request_usb_info = 0;
      APP_Report_ECG_Stats(1);
    }

    APP_USB_LogFlush(0);

    /* PPG INT ?????SD ?????????200ms ???????????? ppg_int_diag.csv */
    static uint32_t last_ppg_diag_tick = 0;
    static uint32_t ppg_diag_seq = 0;
    if (!ECG_DEBUG_ISOLATE_TASKS &&
        g_ecg_rec.state != ECG_REC_RECORDING &&
        HAL_GetTick() - last_ppg_diag_tick >= 200) {
      last_ppg_diag_tick = HAL_GetTick();
      APP_Log_PPG_INT_Diag_To_SD(ppg_diag_seq++);
    }
  }
  /* USER CODE END StartTask_Sensor */
}

/* USER CODE BEGIN Header_StartTask_PPG */
/* USER CODE END Header_StartTask_PPG */
void StartTask_PPG(void *argument)
{
  /* USER CODE BEGIN StartTask_PPG */
  (void)argument;
  PpgTaskHandle = xTaskGetCurrentTaskHandle();

  static uint32_t last_heartbeat = 0;
  extern volatile uint32_t ppg_irq_count;
  uint32_t ppg_fail_streak = 0;

  for (;;) {
    uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(RECORD_PPG_FIFO_DRAIN_TIMEOUT_MS));
    g_ppg_task_call_count++;

    if (!s_ppg_ready || !ecg_streaming || g_ecg_rec.state != ECG_REC_RECORDING) {
      if (RECORD_FAIL_STREAK_RESETS_WHEN_IDLE) {
        ppg_fail_streak = 0;
      }
      continue;
    }

    if (notified > 0U) {
      g_ppg_int_wakeup_count++;
    } else {
      g_ppg_timeout_wakeup_count++;
      if (HAL_GPIO_ReadPin(PPG_INT_GPIO_Port, PPG_INT_Pin) != GPIO_PIN_RESET) {
        continue;
      }
      g_ppg_timeout_drain_count++;
    }

    uint8_t total_n = 0;
    uint8_t rounds = (notified > 0U) ? RECORD_PPG_INT_DRAIN_ROUNDS : 1U;
    for (uint8_t round = 0; round < rounds; round++) {
      uint32_t ir_buf[RECORD_PPG_FIFO_DRAIN_MAX_SAMPLES];
      uint32_t red_buf[RECORD_PPG_FIFO_DRAIN_MAX_SAMPLES];
      uint32_t fail_before = g_max30102_fifo_read_fail_count;

      uint32_t ppg_i2c_t = APP_I2C3_AcquireDiag(1U);
      uint8_t n = MAX30102_ReadFIFO_Batch(ir_buf, red_buf, RECORD_PPG_FIFO_DRAIN_MAX_SAMPLES);
      if (g_max30102_fifo_read_fail_count != fail_before) {
        g_i2c3_rate_iso_error_count++;
        ppg_fail_streak++;
        APP_I2C3_RecoverRecordingSensors();
      } else if (n > 0U) {
        ppg_fail_streak = 0;
      }
      APP_I2C3_ReleaseDiag(1U, ppg_i2c_t);

      if (ppg_fail_streak >= RECORD_PPG_FAIL_STREAK_LIMIT) {
        if (RECORD_FAILED_MODALITY_REINITS_NEXT_START) {
          s_ppg_ready = 0U;
        }
        APP_RequestStopOnModalityError("PPG_ERROR_I2C_FAIL_STREAK_REQUEST_STOP");
        break;
      }

      for (uint8_t i = 0; i < n; i++) {
        MultiSensorLogger_AddPPG(ir_buf[i], red_buf[i]);
      }

      total_n = (uint8_t)(total_n + n);
      if (n < RECORD_PPG_FIFO_DRAIN_MAX_SAMPLES ||
          HAL_GPIO_ReadPin(PPG_INT_GPIO_Port, PPG_INT_Pin) != GPIO_PIN_RESET) {
        break;
      }
    }

    /* ????????? (debug_log, ????????USB) */
    if (HAL_GetTick() - last_heartbeat >= 1000) {
      last_heartbeat = HAL_GetTick();
      char hb[128];
      snprintf(hb, sizeof(hb),
               "PPG_ALIVE,irq=%lu,intwake=%lu,towake=%lu,todrain=%lu,last=%u,ok=%lu,fail=%lu,empty=%lu,ov=%lu",
               (unsigned long)ppg_irq_count,
               (unsigned long)g_ppg_int_wakeup_count,
               (unsigned long)g_ppg_timeout_wakeup_count,
               (unsigned long)g_ppg_timeout_drain_count,
               (unsigned int)total_n,
               (unsigned long)g_max30102_fifo_read_ok_count,
               (unsigned long)g_max30102_fifo_read_fail_count,
               (unsigned long)g_max30102_fifo_empty_count,
               (unsigned long)g_max30102_fifo_ov_count);
      SD_DebugLog_WriteLine(hb);
    }
  }
  /* USER CODE END StartTask_PPG */
}

/* USER CODE BEGIN Header_StartTask_IMU */
/* USER CODE END Header_StartTask_IMU */
void StartTask_IMU(void *argument)
{
  /* USER CODE BEGIN StartTask_IMU */
  (void)argument;
  ImuTaskHandle = xTaskGetCurrentTaskHandle();
  uint32_t imu_fail_streak = 0;

  for (;;) {
    g_imu_task_call_count++;
    if (!s_icm_ready || !ecg_streaming || g_ecg_rec.state != ECG_REC_RECORDING) {
      if (RECORD_FAIL_STREAK_RESETS_WHEN_IDLE) {
        imu_fail_streak = 0;
      }
      while (ulTaskNotifyTake(pdTRUE, 0) > 0) {}
      osDelay(50);
      continue;
    }

    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(IMU_IRQ_WAIT_TIMEOUT_MS)) == 0U) {
      continue;
    }

    if (!s_icm_ready || !ecg_streaming || g_ecg_rec.state != ECG_REC_RECORDING) {
      continue;
    }

    int16_t ax, ay, az, gx, gy, gz;

    uint32_t imu_i2c_t = APP_I2C3_AcquireDiag(0U);
    uint8_t imu_read;
    if (RECORD_RATE_ISO_ICM_BATCH) {
      int16_t axb[8], ayb[8], azb[8], gxb[8], gyb[8], gzb[8];
      imu_read = ICM20948_ReadFifoAccelGyroBatch(axb, ayb, azb, gxb, gyb, gzb, 8U);
      APP_I2C3_ReleaseDiag(0U, imu_i2c_t);
      if (imu_read > 0U) {
        g_imu_read_ok_count += imu_read;
        imu_fail_streak = 0;
        for (uint8_t i = 0; i < imu_read; i++) {
          MultiSensorLogger_AddIMU(axb[i], ayb[i], azb[i], gxb[i], gyb[i], gzb[i]);
        }
        continue;
      }
      imu_read = 1U;
    } else {
      imu_read = ICM20948_ReadAccelGyroRaw(&ax, &ay, &az, &gx, &gy, &gz);
      APP_I2C3_ReleaseDiag(0U, imu_i2c_t);
    }

    if (imu_read == 0) {
      g_imu_read_ok_count++;
      imu_fail_streak = 0;
      MultiSensorLogger_AddIMU(ax, ay, az, gx, gy, gz);
    } else {
      g_imu_read_fail_count++;
      g_i2c3_rate_iso_error_count++;
      imu_fail_streak++;
      imu_i2c_t = APP_I2C3_AcquireDiag(0U);
      APP_I2C3_RecoverRecordingSensors();
      APP_I2C3_ReleaseDiag(0U, imu_i2c_t);
      if (imu_fail_streak >= RECORD_IMU_FAIL_STREAK_LIMIT) {
        if (RECORD_FAILED_MODALITY_REINITS_NEXT_START) {
          s_icm_ready = 0U;
        }
        APP_RequestStopOnModalityError("IMU_ERROR_I2C_FAIL_STREAK_REQUEST_STOP");
      }
    }
  }
  /* USER CODE END StartTask_IMU */
}

/* USER CODE BEGIN Header_StartTask_Audio */
/* USER CODE END Header_StartTask_Audio */
void StartTask_Audio(void *argument)
{
  AudioRecorder_Task(argument);
}

/* USER CODE BEGIN Header_StartTask_Button */
/* USER CODE END Header_StartTask_Button */
void StartTask_Button(void *argument)
{
  /* USER CODE BEGIN StartTask_Button */
  /* USER CODE BEGIN StartTask_Button */
  (void)argument;
  uint8_t prev = 1;
  extern volatile uint8_t g_demo_page_switch;

  for(;;) {
      uint8_t curr = HAL_GPIO_ReadPin(KEY_BTN_GPIO_Port, KEY_BTN_Pin);

      if (prev == 1 && curr == 0) {
          APP_LVGL_NotifyTouchActivity();
          g_demo_page_switch = 1U;
      }
      prev = curr;
      osDelay(10);
  }
  /* USER CODE END StartTask_Button */
}

/* USER CODE BEGIN Header_StartTask_BLE */
/* USER CODE END Header_StartTask_BLE */
void StartTask_BLE(void *argument)
{
  /* USER CODE BEGIN StartTask_BLE */
  (void)argument;

  BLE_SM_Init();

  for(;;) {
    if (ble_rx_flag) {
      /* ???????????*/
      if (ble_rx_len < BLE_RX_BUF_SIZE) {
        ble_rx_buf[ble_rx_len] = '\0';
      } else {
        ble_rx_buf[BLE_RX_BUF_SIZE - 1] = '\0';
      }

      /* ???????????(??????) */
      uint16_t line_start = 0;
      for (uint16_t i = 0; i < ble_rx_len; i++) {
        if (ble_rx_buf[i] == '\r' || ble_rx_buf[i] == '\n') {
          uint16_t line_len = i - line_start;
          if (line_len > 0) {
            BLE_SM_ProcessLine((const char *)&ble_rx_buf[line_start], line_len);
          }
          line_start = i + 1;
          /* ??? \r\n ???????????*/
          if (ble_rx_buf[i] == '\r' && (i + 1) < ble_rx_len && ble_rx_buf[i + 1] == '\n') {
            i++;
            line_start = i + 1;
          }
        }
      }

      ble_rx_flag = 0;
      ble_rx_len = 0;
    }

    osDelay(10);
  }
  /* USER CODE END StartTask_BLE */
}

/*
 * BLE UART???????- ??ble_state_machine.c ???
 */
void BLE_SM_Send(const char *data)
{
  if (data == NULL) return;
  uint16_t len = (uint16_t)strlen(data);
  if (len > 0) {
    HAL_UART_Transmit(&huart1, (uint8_t *)data, len, 100);
  }
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/**
  * @brief  ICM20948 ?????????+?????????????????????PIN???T1???IN ???????
  * @note   ?????????????????? PH1 ????????? INT_STATUS_1 ?????PH1 ???????
  *         ????????????????????????????????????????????????????
  *         ???????????SD debug_log ??USB CDC (??????????
  */
static void APP_ICM20948_IntFlagAndPinLevelCheck(void)
{
    char line[192];

    uint8_t cfg_after_enable = 0;
    uint8_t en1_after_enable = 0;

    GPIO_PinState pin_before_wait;
    GPIO_PinState pin_before_st1;
    GPIO_PinState pin_after_st1;

    uint8_t st1 = 0;

    uint32_t irq_before = 0;
    uint32_t irq_after = 0;

    extern volatile uint32_t icm_irq_count;

    SD_DebugLog_WriteLine("ICM_INT_FLAG_PIN_CHECK_BEGIN");
    Safe_USB_Printf("\r\n[ICM_INT_FLAG_PIN_CHECK_BEGIN]\r\n");

    /* 1. ???????????????????????*/
    ICM20948_DisableDataReadyInterrupt();
    ICM20948_ClearInterruptStatus();
    osDelay(20);

    /* 2. ?????????????Data Ready ?????*/
    ICM20948_EnableLatchedDataReadyInterrupt_Debug();
    osDelay(5);

    /* 3. ??????????????????????????? */
    cfg_after_enable = ICM20948_ReadBank0Reg_Debug(0x0F); // INT_PIN_CFG
    en1_after_enable = ICM20948_ReadBank0Reg_Debug(0x11); // INT_ENABLE_1

    snprintf(line, sizeof(line),
             "ICM_CFG_CHECK,CFG=0x%02X,EN1=0x%02X",
             cfg_after_enable,
             en1_after_enable);
    SD_DebugLog_WriteLine(line);
    Safe_USB_Printf("%s\r\n", line);

    /* 4. ?????????????????????RQ?????*/
    pin_before_wait = HAL_GPIO_ReadPin(ICM_INT_GPIO_Port, ICM_INT_Pin);
    irq_before = icm_irq_count;

    snprintf(line, sizeof(line),
             "ICM_BEFORE_WAIT,PIN=%u,IRQ=%lu",
             (unsigned)pin_before_wait,
             (unsigned long)irq_before);
    SD_DebugLog_WriteLine(line);
    Safe_USB_Printf("%s\r\n", line);

    /* 5. ?????Data Ready ???? (50Hz ODR, 120ms??????????? */
    osDelay(120);

    /* 6. ?????????: ?????IN, ?????T1, ?????IN */
    pin_before_st1 = HAL_GPIO_ReadPin(ICM_INT_GPIO_Port, ICM_INT_Pin);

    st1 = ICM20948_ReadBank0Reg_Debug(0x1A);  // INT_STATUS_1

    pin_after_st1 = HAL_GPIO_ReadPin(ICM_INT_GPIO_Port, ICM_INT_Pin);

    irq_after = icm_irq_count;

    snprintf(line, sizeof(line),
             "ICM_FLAG_PIN_RESULT,PIN_BEFORE_ST1=%u,ST1=0x%02X,PIN_AFTER_ST1=%u,IRQ_BEFORE=%lu,IRQ_AFTER=%lu",
             (unsigned)pin_before_st1,
             st1,
             (unsigned)pin_after_st1,
             (unsigned long)irq_before,
             (unsigned long)irq_after);
    SD_DebugLog_WriteLine(line);
    Safe_USB_Printf("%s\r\n", line);

    /* 7. ?????????????????????????*/
    ICM20948_DisableDataReadyInterrupt();
    ICM20948_ClearInterruptStatus();

    SD_DebugLog_WriteLine("ICM_INT_FLAG_PIN_CHECK_END");
    Safe_USB_Printf("[ICM_INT_FLAG_PIN_CHECK_END]\r\n");
}

/**
  * @brief  PPG INT ????????? ??SD ?????
  * @note   150ms ?????????????????????????????STATUS1 ??????????????????
  *         ?????STATUS1 ??????????????????????????????????????
  */
static void APP_Log_PPG_INT_Diag_To_SD(uint32_t seq)
{
    SensorRecord_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.timestamp_ms = HAL_GetTick();
    rec.seq = seq;
    rec.type = SENSOR_REC_PPG_INT_DIAG;

    extern volatile uint32_t ppg_irq_count;

    /* 1. ?????PPG_INT ????????? */
    GPIO_PinState s = HAL_GPIO_ReadPin(PPG_INT_GPIO_Port, PPG_INT_Pin);
    rec.data.ppg_int_diag.pin_before = (s == GPIO_PIN_SET) ? 1 : 0;
    rec.data.ppg_int_diag.irq_count  = ppg_irq_count;

    /* 2. ??I2C ???????*/
    uint8_t ie1 = 0xEE, status1 = 0xEE, wr = 0xEE, rd = 0xEE, ov = 0xEE;

    MAX30102_ReadBuffer(INTERRUPT_ENABLE1, &ie1, 1);

    /*
     * NOTE: ?????INTERRUPT_STATUS1 ??????????????????????????? INT ???????
     * ???????????????????????????????????????????????????????????STATUS1??
     */
    MAX30102_ReadBuffer(INTERRUPT_STATUS1, &status1, 1);

    MAX30102_ReadBuffer(FIFO_WR_POINTER, &wr, 1);
    MAX30102_ReadBuffer(FIFO_RD_POINTER, &rd, 1);
    MAX30102_ReadBuffer(FIFO_OV_COUNTER, &ov, 1);

    rec.data.ppg_int_diag.status1 = status1;
    rec.data.ppg_int_diag.ie1     = ie1;
    rec.data.ppg_int_diag.fifo_wr = wr & 0x1F;
    rec.data.ppg_int_diag.fifo_rd = rd & 0x1F;
    rec.data.ppg_int_diag.fifo_ov = ov & 0x1F;

    /* 3. ?????STATUS1 ?????????????????????????*/
    s = HAL_GPIO_ReadPin(PPG_INT_GPIO_Port, PPG_INT_Pin);
    rec.data.ppg_int_diag.pin_after = (s == GPIO_PIN_SET) ? 1 : 0;

    /* 4. ??SD ????????????????*/
    PPGDiag_Enqueue(&rec);
}

/**
  * @brief  Producer: ??ECG ??????????RAM buffer ??SD ?????
  */
void Packagedata_AddEcgSample(int16_t ecg)
{
  g_ecg_rec.pack_add_attempt_count++;
  if (g_ecg_rec.state != ECG_REC_RECORDING) {
    g_ecg_rec.pack_add_drop_count++;
    return;
  }

#if RECORD_TEST_ECG_FIFO_ONLY
  g_ecg_rec.pack_add_ok_count++;
  return;
#endif

  if (g_sys_state == SYS_STATE_RECORDING) {
    /* RAM ???????????????????*/
    if (ecg_buf_idx < ECG_BUFFER_SIZE) {
      ecg_buffer[ecg_buf_idx++] = ecg;
    }
    if (ecg_buf_idx > g_ecg_rec.pack_buffer_level_max) {
      g_ecg_rec.pack_buffer_level_max = ecg_buf_idx;
    }
    g_ecg_rec.pack_add_ok_count++;

#if RECORD_TEST_ECG_COUNT_ONLY
    return;
#endif

    /* ????????? block logger (???????ECG_SDLogger_Enqueue) */
    MultiSensorLogger_AddECG(ecg);
  }
}

/**
  * @brief  Safe_USB_Printf ????????????????
  * @note   ?????CDC_Transmit_FS_Blocking ??????????????200ms ???????
  *         ??????????????????????????????????????????????????
  */
static void APP_USB_LogFlush(uint8_t force)
{
  extern USBD_HandleTypeDef hUsbDeviceFS;

  if (s_usb_log_flushing || s_usb_log_pending_len == 0U) {
    return;
  }

  if (!force &&
      s_usb_log_pending_len < 96U &&
      (HAL_GetTick() - s_usb_log_pending_tick) < 100U) {
    return;
  }

  if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED ||
      hUsbDeviceFS.pClassDataCmsit[hUsbDeviceFS.classId] == NULL) {
    return;
  }

  s_usb_log_flushing = 1U;
  uint8_t ret = CDC_Transmit_FS_Blocking((uint8_t*)s_usb_log_pending,
                                         s_usb_log_pending_len,
                                         USB_LOG_FLUSH_TIMEOUT_MS);
  if (ret == USBD_OK) {
    usb_tx_ok_count++;
    s_usb_log_pending_len = 0U;
    s_usb_log_pending_tick = HAL_GetTick();
  } else if (ret == USBD_BUSY) {
    usb_tx_busy_count++;
    s_usb_log_pending_len = 0U;
    s_usb_log_pending_tick = HAL_GetTick();
  } else {
    usb_tx_drop_count++;
  }
  s_usb_log_flushing = 0U;
}

static void APP_Report_ECG_Stats(uint8_t force)
{
  static uint32_t last_tick = 0;
  static uint32_t last_fifo = 0;
  static uint32_t last_valid = 0;
  static uint32_t last_fast = 0;
  static uint32_t last_irq = 0;

  uint32_t now = HAL_GetTick();
  if (!force && (now - last_tick < 1000)) {
    return;
  }

  if (last_tick == 0) {
    last_tick = now;
    last_fifo = g_ecg_rec.fifo_sample_count;
    last_valid = g_ecg_rec.fifo_valid_count;
    last_fast = g_ecg_rec.fifo_fast_count;
    last_irq = ecg_irq_count;
    if (!force) {
      return;
    }
  }

  uint32_t dt = now - last_tick;
  uint32_t dfifo = g_ecg_rec.fifo_sample_count - last_fifo;
  uint32_t dvalid = g_ecg_rec.fifo_valid_count - last_valid;
  uint32_t dfast = g_ecg_rec.fifo_fast_count - last_fast;
  uint32_t dirq = ecg_irq_count - last_irq;
  uint32_t fifo_rate = (dt > 0) ? ((dfifo * 1000UL) / dt) : 0;
  uint32_t valid_rate = (dt > 0) ? ((dvalid * 1000UL) / dt) : 0;
  uint32_t fast_rate = (dt > 0) ? ((dfast * 1000UL) / dt) : 0;
  uint32_t irq_rate = (dt > 0) ? ((dirq * 1000UL) / dt) : 0;
  Safe_USB_Printf("[ECG] r=%lu v=%lu f=%lu i=%lu e=%lu o=%lu u=%lu\r\n",
                  (unsigned long)fifo_rate,
                  (unsigned long)valid_rate,
                  (unsigned long)fast_rate,
                  (unsigned long)irq_rate,
                  (unsigned long)g_ecg_rec.fifo_eovf_count,
                  (unsigned long)g_ecg_rec.fifo_etag_overflow_count,
                  (unsigned long)g_ecg_rec.fifo_unknown_etag_count);

  last_tick = now;
  last_fifo = g_ecg_rec.fifo_sample_count;
  last_valid = g_ecg_rec.fifo_valid_count;
  last_fast = g_ecg_rec.fifo_fast_count;
  last_irq = ecg_irq_count;
}

void Safe_USB_Printf(const char *format, ...)
{
  char buf[384];

  if (format == NULL) return;

  /* USB ?????????????????? */

  /* ???????*/
  va_list args;
  va_start(args, format);
  int len = vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);

  if (len <= 0) return;
  if (len >= (int)sizeof(buf)) {
    len = (int)sizeof(buf) - 1;
    buf[len] = '\0';
  }

  /* ??????????(200ms ????? */
  if ((uint32_t)len > USB_LOG_PENDING_SIZE) {
    usb_tx_drop_count++;
    return;
  }

  if (s_usb_log_pending_len == 0U) {
    s_usb_log_pending_tick = HAL_GetTick();
  }

  if ((uint32_t)len > (USB_LOG_PENDING_SIZE - s_usb_log_pending_len)) {
    APP_USB_LogFlush(1);
  }

  if ((uint32_t)len > (USB_LOG_PENDING_SIZE - s_usb_log_pending_len)) {
    uint32_t keep = USB_LOG_PENDING_SIZE / 2U;
    memmove(s_usb_log_pending,
            s_usb_log_pending + s_usb_log_pending_len - keep,
            keep);
    s_usb_log_pending_len = (uint16_t)keep;
    s_usb_log_pending_tick = HAL_GetTick();
    usb_tx_drop_count++;
  }

  memcpy(s_usb_log_pending + s_usb_log_pending_len, buf, (size_t)len);
  s_usb_log_pending_len = (uint16_t)(s_usb_log_pending_len + len);

  /* SD ?????????????????????????????????????????????????????????????????????*/
  if (s_usb_log_pending_len >= 96U ||
      (HAL_GetTick() - s_usb_log_pending_tick) >= 100U) {
    APP_USB_LogFlush(0);
  }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void)xTask;
  (void)pcTaskName;
  taskDISABLE_INTERRUPTS();
  while (1) { __NOP(); }
}

void vApplicationMallocFailedHook(void)
{
  taskDISABLE_INTERRUPTS();
  while (1) { __NOP(); }
}
/* USER CODE END Application */






