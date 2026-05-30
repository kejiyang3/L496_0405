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

TaskHandle_t EcgTaskHandle = NULL;             /* ECG任务句柄, 供ISR直接通知 */
TaskHandle_t PpgTaskHandle = NULL;             /* PPG任务句柄 */
TaskHandle_t ImuTaskHandle = NULL;             /* IMU任务句柄 */
TaskHandle_t LvglTaskHandle = NULL;             /* LVGL任务句柄, 供ISR通知 */

/* ECG 临时 RAM 缓存 (短期观察用，不用作长期存�? */
#define ECG_BUFFER_SIZE 10240
int16_t ecg_buffer[ECG_BUFFER_SIZE];
volatile uint32_t ecg_buf_idx = 0;

typedef enum {
    SYS_STATE_IDLE = 0,
    SYS_STATE_RECORDING
} SysState_t;

volatile SysState_t g_sys_state = SYS_STATE_IDLE;
/* USB TX 统计计数�?*/
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
static volatile uint8_t s_ppg_ready = 0;
static volatile uint8_t s_icm_ready = 0;
#define USB_LOG_PENDING_SIZE 1536U
static char s_usb_log_pending[USB_LOG_PENDING_SIZE];
static uint16_t s_usb_log_pending_len = 0;
static uint32_t s_usb_log_pending_tick = 0;
static uint8_t s_usb_log_flushing = 0;

/* 任务创建错误掩码 (bit0=LVGL, bit1=Sensor, bit2=SDWriter, bit3=USBDump, bit4=Audio, bit5=Button, bit6=BLE) */
volatile uint32_t g_task_create_error = 0;

/* === MAX30102 中断/FIFO 诊断变量 (Sensor 任务写入, LCD 读取) === */
volatile uint8_t g_ppg_ie1      = 0;  /* INTERRUPT_ENABLE1 readback */
volatile uint8_t g_ppg_is1      = 0;  /* INTERRUPT_STATUS1 readback */
volatile uint8_t g_ppg_fifo_wr  = 0;  /* FIFO_WR_POINTER & 0x1F */
volatile uint8_t g_ppg_fifo_rd  = 0;  /* FIFO_RD_POINTER & 0x1F */
volatile uint8_t g_ppg_fifo_ov  = 0;  /* FIFO_OV_COUNTER & 0x1F */
volatile uint8_t g_ppg_mode     = 0;  /* MODE_CONFIGURATION */

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

  /* PPG 采集任务 */
  Task_PPGHandle = ECG_DEBUG_ENABLE_PPG ? osThreadNew(StartTask_PPG, NULL, &Task_PPG_attributes) : NULL;

  /* IMU 采集任务 */
  Task_IMUHandle = ECG_DEBUG_ENABLE_ICM ? osThreadNew(StartTask_IMU, NULL, &Task_IMU_attributes) : NULL;

  /* USER CODE BEGIN RTOS_THREADS */
  if (ECG_DEBUG_ENABLE_LVGL && Task_LVGLHandle == NULL) g_task_create_error |= (1UL << 0);
  if (Task_SensorHandle == NULL) g_task_create_error |= (1UL << 1);
  if (Task_ButtonHandle == NULL) g_task_create_error |= (1UL << 5);
  if (ECG_DEBUG_ENABLE_PPG && Task_PPGHandle == NULL) g_task_create_error |= (1UL << 6);
  if (ECG_DEBUG_ENABLE_ICM && Task_IMUHandle == NULL) g_task_create_error |= (1UL << 7);

  Task_AudioHandle = RECORD_DIAG_AUDIO_TASK_CREATE ? osThreadNew(StartTask_Audio, NULL, &Task_Audio_attributes) : NULL;
  if (Task_AudioHandle == NULL) g_task_create_error |= (1UL << 4);

  /* 多传感器 SD Writer (取代旧的 ECG_SDWriter) */
  Task_MultiSensor_SDWriterHandle = osThreadNew(StartTask_MultiSensor_SDWriter, NULL, &Task_MultiSensor_SDWriter_attributes);
  if (Task_MultiSensor_SDWriterHandle == NULL) g_task_create_error |= (1UL << 2);

  /* PPG INT 诊断 SD Writer */
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

  /* 1. 等待 USB 枚举完成 */
  osDelay(2000);

  /* 2. 启动蓝牙 UART 空闲中断 DMA 接收 */
#if !ECG_DEBUG_ISOLATE_TASKS
  HAL_UARTEx_ReceiveToIdle_DMA(&huart1, ble_rx_buf, BLE_RX_BUF_SIZE);
  __HAL_DMA_DISABLE_IT(&hdma_usart1_rx, DMA_IT_HT);
#endif

  /* 3. USB CDC 就绪（V1 关闭 USB 日志�?*/
  APP_USB_LOG("\r\n[SYS] RTOS Started, USB CDC Ready!\r\n");

  /* 4. 初始�?LVGL 显示 + V1 ECG 控制界面 */
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

  /* SD debug log �?必须�?RTOS 运行后才能安全调�?f_mount */
  SD_DebugLog_Init();

  APP_USB_LOG("[ECG_V1] Sensor task started\r\n");

  /* I2C3 传感器一次性初始化。先初始�?ICM，避�?PPG/TXS 异常影响已验证的 IMU 基线�?*/
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
      /* PPG 失败时重�?STM32 �?I2C3，避免失败事务残留影响后�?ICM 运行期读数�?*/
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
      /* 轮询探针已完成使命，证明硬件链路通。现在注释掉，避免阻�?RTOS 调度�?
       * 遗留中断由后�?recording start 流程�?INT_STATUS1 释放引脚�?*/
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

  /* ICM20948 中断标志+引脚电平验证 �?仅在 ICM 初始化成功后执行一�?*/
  if (!ECG_DEBUG_ISOLATE_TASKS && ECG_DEBUG_ENABLE_ICM && icm_ret == 0) {
      APP_ICM20948_IntFlagAndPinLevelCheck();
  }

  /* ECG 初始化照旧，不受 PPG/ICM 影响 */
  Safe_USB_Printf("[SENSOR] before MAX30003_Init\r\n");
  MAX30003_Init();
  MAX30003_DiagLog_Init();
  Safe_USB_Printf("[SENSOR] after MAX30003_Init\r\n");
  MAX30003_PollLeadStatus();

  /* Diagnostic: explicit EN_MIC control for Case B/D/E/F */
#if RECORD_DIAG_AUDIO_EN_MIC_ON
  HAL_GPIO_WritePin(EN_MIC_GPIO_Port, EN_MIC_Pin, GPIO_PIN_SET);
  SD_DebugLog_WriteLine("EN_MIC_ON_BY_DIAG");
#else
  HAL_GPIO_WritePin(EN_MIC_GPIO_Port, EN_MIC_Pin, GPIO_PIN_RESET);
#endif
  SD_DebugLog_WriteLine("MAX30003_INIT_DONE");
  Safe_USB_Printf("[SENSOR] after MAX30003_INIT_DONE log\r\n");

  /* 初始不采�?*/
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
        g_ecg_rec.state = ECG_REC_RECORDING;

        MultiSensorLogger_ResetForNewRecording();

        /* 等待 MSWriter 打开文件，最�?3000ms */
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
            /* 调试模式：没�?SD 也继续启动流，以便观测采样率 */
        }

        Session_Create(HAL_GetTick());
        Safe_USB_Printf("[REC] session=%s\r\n", g_session.session_id);
        Safe_USB_Printf("[REC] record start file_opened=%u tick=%lu\r\n",
                        g_ecg_rec.sd_file_opened,
                        (unsigned long)HAL_GetTick());

        ecg_buf_idx = 0;
        g_sys_state = SYS_STATE_RECORDING;
        ECG_ResetStats();
        {
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

        // [DEBUG] 跳过 PPG/ICM I2C 操作以排�?I2C NACK 问题
        // {
        //     uint8_t s1, s2;
        //     MAX30102_ClearInterruptStatus(&s1, &s2);
        // }
        if (Mtx_I2C3Handle != NULL) osMutexAcquire(Mtx_I2C3Handle, osWaitForever);
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
        if (Mtx_I2C3Handle != NULL) osMutexRelease(Mtx_I2C3Handle);

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
        /* AudioTask stays at creation priority Normal1 �� must be below SensorTask AboveNormal */
        (void)Task_AudioHandle;
        Safe_USB_Printf("[REC] before MAX30003_StartStream tick=%lu\r\n",
                        (unsigned long)HAL_GetTick());
        MAX30003_StartStream();
        g_ecg_rec.ecg_stream_start_tick = HAL_GetTick();
        g_ecg_rec.start_tick = g_ecg_rec.ecg_stream_start_tick;
        ecg_streaming = 1;
        if (s_icm_ready) {
          ICM20948_EnableDataReadyInterrupt();
        }
        Safe_USB_Printf("[REC] after MAX30003_StartStream tick=%lu start_tick=%lu\r\n",
                        (unsigned long)HAL_GetTick(),
                        (unsigned long)g_ecg_rec.start_tick);
        g_ecg_rec.auto_stop_ms = g_ecg_rec.requested_record_ms;
        g_ecg_rec.requested_record_ms = 0;
        /* ECG CDC stats are emitted once per second by APP_Report_ECG_Stats. */
      }
    }

    /* 调试：自动停止计�?*/
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
        MAX30003_StopStream();
        g_ecg_rec.ecg_stream_stop_tick = HAL_GetTick();

        if (Mtx_I2C3Handle != NULL) osMutexAcquire(Mtx_I2C3Handle, osWaitForever);
        if (s_ppg_ready) {
          (void)MAX30102_DisableInterrupts();
        }
        if (s_icm_ready) {
          ICM20948_DisableDataReadyInterrupt();
        }
        if (Mtx_I2C3Handle != NULL) osMutexRelease(Mtx_I2C3Handle);

        // [DEBUG] 跳过 I2C 操作避免 NACK 挂死
        // MAX30102_DisableInterrupts();

        MultiSensorLogger_RequestStopAndFlush();
        g_ecg_rec.state = ECG_REC_STOPPING;
        finalize_after_stop = 1;

        MAX30003_DiagLog_Stop();
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
        /* DIAG: checkpoint �� time since last recording loop entry */
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
        MAX30003_Task();
        MAX30003_DiagLog_Run();
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

    /* 低频轮询电极状�?(4Hz)，Idle 也持续检�?*/
    static uint32_t last_lead_poll = 0;
    if (HAL_GetTick() - last_lead_poll >= 250) {
        last_lead_poll = HAL_GetTick();
        MAX30003_PollLeadStatus();

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

    /* PPG INT 诊断 SD 采集 �?�?200ms 记录一条到 ppg_int_diag.csv */
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

      if (Mtx_I2C3Handle != NULL) osMutexAcquire(Mtx_I2C3Handle, osWaitForever);
      uint8_t n = MAX30102_ReadFIFO_Batch(ir_buf, red_buf, RECORD_PPG_FIFO_DRAIN_MAX_SAMPLES);
      if (g_max30102_fifo_read_fail_count != fail_before) {
        ppg_fail_streak++;
        APP_I2C3_RecoverRecordingSensors();
      } else if (n > 0U) {
        ppg_fail_streak = 0;
      }
      if (Mtx_I2C3Handle != NULL) osMutexRelease(Mtx_I2C3Handle);

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

    /* 每秒心跳 (debug_log, 不通过 USB) */
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

    if (Mtx_I2C3Handle != NULL) osMutexAcquire(Mtx_I2C3Handle, osWaitForever);
    uint8_t imu_read = ICM20948_ReadAccelGyroRaw(&ax, &ay, &az, &gx, &gy, &gz);
    if (Mtx_I2C3Handle != NULL) osMutexRelease(Mtx_I2C3Handle);

    if (imu_read == 0) {
      g_imu_read_ok_count++;
      imu_fail_streak = 0;
      MultiSensorLogger_AddIMU(ax, ay, az, gx, gy, gz);
    } else {
      g_imu_read_fail_count++;
      imu_fail_streak++;
      if (Mtx_I2C3Handle != NULL) osMutexAcquire(Mtx_I2C3Handle, osWaitForever);
      APP_I2C3_RecoverRecordingSensors();
      if (Mtx_I2C3Handle != NULL) osMutexRelease(Mtx_I2C3Handle);
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
  (void)argument;
  uint8_t prev = 1;

  for(;;) {
      uint8_t curr = HAL_GPIO_ReadPin(KEY_BTN_GPIO_Port, KEY_BTN_Pin);

      if (prev == 1 && curr == 0) {
          uint8_t wake_only = APP_LVGL_NotifyTouchActivity();
          if (!wake_only) {
              /* 物理按键：toggle start/stop */
              if (g_ecg_rec.state == ECG_REC_IDLE ||
                  g_ecg_rec.state == ECG_REC_STOPPED ||
                  g_ecg_rec.state == ECG_REC_ERROR) {
                  ECG_RequestStart();
              } else if (g_ecg_rec.state == ECG_REC_RECORDING) {
                  ECG_RequestStop();
              }
          }
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

  for(;;) {
    if (ble_rx_flag) {
      if (ble_rx_len < BLE_RX_BUF_SIZE) {
        ble_rx_buf[ble_rx_len] = '\0';
      } else {
        ble_rx_buf[BLE_RX_BUF_SIZE - 1] = '\0';
      }
      ble_rx_flag = 0;
      ble_rx_len = 0;
    }

    osDelay(10);
  }
  /* USER CODE END StartTask_BLE */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/**
  * @brief  ICM20948 中断标志+引脚电平最小验�?�?PIN→ST1→PIN 顺序�?
  * @note   在锁存模式下先读 PH1 电平再读 INT_STATUS_1 再读 PH1 电平�?
  *         避免读状态寄存器意外清除锁存中断后引脚恢复高�?
  *         结果输出�?SD debug_log �?USB CDC (一次�?�?
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

    /* 1. 关闭旧中断并清旧状�?*/
    ICM20948_DisableDataReadyInterrupt();
    ICM20948_ClearInterruptStatus();
    osDelay(20);

    /* 2. 开启锁存式 Data Ready 中断 */
    ICM20948_EnableLatchedDataReadyInterrupt_Debug();
    osDelay(5);

    /* 3. 读回确认锁存配置是否生效 */
    cfg_after_enable = ICM20948_ReadBank0Reg_Debug(0x0F); // INT_PIN_CFG
    en1_after_enable = ICM20948_ReadBank0Reg_Debug(0x11); // INT_ENABLE_1

    snprintf(line, sizeof(line),
             "ICM_CFG_CHECK,CFG=0x%02X,EN1=0x%02X",
             cfg_after_enable,
             en1_after_enable);
    SD_DebugLog_WriteLine(line);
    Safe_USB_Printf("%s\r\n", line);

    /* 4. 记录等待前的引脚和IRQ计数 */
    pin_before_wait = HAL_GPIO_ReadPin(ICM_INT_GPIO_Port, ICM_INT_Pin);
    irq_before = icm_irq_count;

    snprintf(line, sizeof(line),
             "ICM_BEFORE_WAIT,PIN=%u,IRQ=%lu",
             (unsigned)pin_before_wait,
             (unsigned long)irq_before);
    SD_DebugLog_WriteLine(line);
    Safe_USB_Printf("%s\r\n", line);

    /* 5. 等待 Data Ready 产生 (50Hz ODR, 120ms内应有多�? */
    osDelay(120);

    /* 6. 核心验证: 先读PIN, 再读ST1, 再读PIN */
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

    /* 7. 结束后关闭中断并清状�?*/
    ICM20948_DisableDataReadyInterrupt();
    ICM20948_ClearInterruptStatus();

    SD_DebugLog_WriteLine("ICM_INT_FLAG_PIN_CHECK_END");
    Safe_USB_Printf("[ICM_INT_FLAG_PIN_CHECK_END]\r\n");
}

/**
  * @brief  PPG INT 诊断采集 �?SD 队列
  * @note   150ms 周期调用。先读引脚电�?�?�?STATUS1 �?再读引脚电平�?
  *         读取 STATUS1 会清除对应中断状态（仅用于调试）�?
  */
static void APP_Log_PPG_INT_Diag_To_SD(uint32_t seq)
{
    SensorRecord_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.timestamp_ms = HAL_GetTick();
    rec.seq = seq;
    rec.type = SENSOR_REC_PPG_INT_DIAG;

    extern volatile uint32_t ppg_irq_count;

    /* 1. 先读 PPG_INT 引脚电平 */
    GPIO_PinState s = HAL_GPIO_ReadPin(PPG_INT_GPIO_Port, PPG_INT_Pin);
    rec.data.ppg_int_diag.pin_before = (s == GPIO_PIN_SET) ? 1 : 0;
    rec.data.ppg_int_diag.irq_count  = ppg_irq_count;

    /* 2. �?I2C 寄存�?*/
    uint8_t ie1 = 0xEE, status1 = 0xEE, wr = 0xEE, rd = 0xEE, ov = 0xEE;

    MAX30102_ReadBuffer(INTERRUPT_ENABLE1, &ie1, 1);

    /*
     * NOTE: 读取 INTERRUPT_STATUS1 会清除对应中断状态并释放 INT 引脚�?
     * 后续正式中断采集版本中，应避免在非事件处理处频繁读取 STATUS1�?
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

    /* 3. 读取 STATUS1 后，立即再次读引脚电�?*/
    s = HAL_GPIO_ReadPin(PPG_INT_GPIO_Port, PPG_INT_Pin);
    rec.data.ppg_int_diag.pin_after = (s == GPIO_PIN_SET) ? 1 : 0;

    /* 4. �?SD 队列，异步写�?*/
    PPGDiag_Enqueue(&rec);
}

/**
  * @brief  Producer: �?ECG 样本存入 RAM buffer �?SD 队列
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
    /* RAM 缓存（短期观察） */
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

    /* 多传感器 block logger (取代�?ECG_SDLogger_Enqueue) */
    MultiSensorLogger_AddECG(ecg);
  }
}

/**
  * @brief  Safe_USB_Printf �?调试日志打印
  * @note   使用 CDC_Transmit_FS_Blocking 阻塞发送，�?200ms 超时�?
  *         仅在采样停止后使用，采样进行中不要高频调用�?
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

  /* USB 未配置时直接丢弃 */

  /* 格式�?*/
  va_list args;
  va_start(args, format);
  int len = vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);

  if (len <= 0) return;
  if (len >= (int)sizeof(buf)) {
    len = (int)sizeof(buf) - 1;
    buf[len] = '\0';
  }

  /* 阻塞发�?(200ms 超时) */
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

  /* SD 问题定位阶段：每条日志都尽快推出去，避免小包长期留在缓冲里�?*/
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






