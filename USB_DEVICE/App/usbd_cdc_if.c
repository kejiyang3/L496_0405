/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usbd_cdc_if.c
  * @version        : v2.0_Cube
  * @brief          : Usb device for Virtual Com Port.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "usbd_cdc_if.h"

/* USER CODE BEGIN INCLUDE */

/* External variable for USB streaming status */
extern volatile uint8_t is_usb_streaming;

/* For osKernelGetState / osDelay in blocking transmit */
#include "cmsis_os.h"
#include "ecg_record_control.h"
#include <string.h>

/* USER CODE END INCLUDE */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

/* USER CODE END PV */

/** @addtogroup STM32_USB_OTG_DEVICE_LIBRARY
  * @brief Usb device library.
  * @{
  */

/** @addtogroup USBD_CDC_IF
  * @{
  */

/** @defgroup USBD_CDC_IF_Private_TypesDefinitions USBD_CDC_IF_Private_TypesDefinitions
  * @brief Private types.
  * @{
  */

/* USER CODE BEGIN PRIVATE_TYPES */

/* USER CODE END PRIVATE_TYPES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_Defines USBD_CDC_IF_Private_Defines
  * @brief Private defines.
  * @{
  */

/* USER CODE BEGIN PRIVATE_DEFINES */
/* USER CODE END PRIVATE_DEFINES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_Macros USBD_CDC_IF_Private_Macros
  * @brief Private macros.
  * @{
  */

/* USER CODE BEGIN PRIVATE_MACRO */

/* USER CODE END PRIVATE_MACRO */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_Variables USBD_CDC_IF_Private_Variables
  * @brief Private variables.
  * @{
  */
/* Create buffer for reception and transmission           */
/* It's up to user to redefine and/or remove those define */
/** Received data over USB are stored in this buffer      */
uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];

/** Data to send over USB CDC are stored in this buffer   */
uint8_t UserTxBufferFS[APP_TX_DATA_SIZE];

/* USER CODE BEGIN PRIVATE_VARIABLES */
static uint8_t CdcTxWorkBuffer[APP_TX_DATA_SIZE];
static volatile uint32_t CdcTxStartTick = 0U;
volatile uint32_t g_cdc_rx_count = 0U;
volatile uint32_t g_cdc_rx_last_len = 0U;
volatile uint8_t g_cdc_rx_last_cmd[16];
#define CDC_TX_STUCK_TIMEOUT_MS 1000U

/* USER CODE END PRIVATE_VARIABLES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Exported_Variables USBD_CDC_IF_Exported_Variables
  * @brief Public variables.
  * @{
  */

extern USBD_HandleTypeDef hUsbDeviceFS;
extern PCD_HandleTypeDef hpcd_USB_OTG_FS;

/* USER CODE BEGIN EXPORTED_VARIABLES */

/* USER CODE END EXPORTED_VARIABLES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_FunctionPrototypes USBD_CDC_IF_Private_FunctionPrototypes
  * @brief Private functions declaration.
  * @{
  */

static int8_t CDC_Init_FS(void);
static int8_t CDC_DeInit_FS(void);
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length);
static int8_t CDC_Receive_FS(uint8_t* pbuf, uint32_t *Len);
static int8_t CDC_TransmitCplt_FS(uint8_t *pbuf, uint32_t *Len, uint8_t epnum);

/* USER CODE BEGIN PRIVATE_FUNCTIONS_DECLARATION */
static uint8_t CDC_CommandMatches(const uint8_t *buf, uint32_t len, const char *cmd);
static uint8_t CDC_IsReady(void);
static USBD_CDC_HandleTypeDef *CDC_GetHandle(void);
static void CDC_RecoverStuckTx(void);

/* USER CODE END PRIVATE_FUNCTIONS_DECLARATION */

/**
  * @}
  */

USBD_CDC_ItfTypeDef USBD_Interface_fops_FS =
{
  CDC_Init_FS,
  CDC_DeInit_FS,
  CDC_Control_FS,
  CDC_Receive_FS,
  CDC_TransmitCplt_FS
};

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  Initializes the CDC media low layer over the FS USB IP
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Init_FS(void)
{
  /* USER CODE BEGIN 3 */
  /* Set Application Buffers */
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, 0);
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
  (void)USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  return (USBD_OK);
  /* USER CODE END 3 */
}

/**
  * @brief  DeInitializes the CDC media low layer
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_DeInit_FS(void)
{
  /* USER CODE BEGIN 4 */
  is_usb_streaming = 0;  /* Clear USB streaming flag on USB disconnect */
  return (USBD_OK);
  /* USER CODE END 4 */
}

/**
  * @brief  Manage the CDC class requests
  * @param  cmd: Command code
  * @param  pbuf: Buffer containing command data (request parameters)
  * @param  length: Number of data to be sent (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length)
{
  /* USER CODE BEGIN 5 */
  (void)pbuf;
  (void)length;
  switch(cmd)
  {
    case CDC_SEND_ENCAPSULATED_COMMAND:

    break;

    case CDC_GET_ENCAPSULATED_RESPONSE:

    break;

    case CDC_SET_COMM_FEATURE:

    break;

    case CDC_GET_COMM_FEATURE:

    break;

    case CDC_CLEAR_COMM_FEATURE:

    break;

  /*******************************************************************************/
  /* Line Coding Structure                                                       */
  /*-----------------------------------------------------------------------------*/
  /* Offset | Field       | Size | Value  | Description                          */
  /* 0      | dwDTERate   |   4  | Number |Data terminal rate, in bits per second*/
  /* 4      | bCharFormat |   1  | Number | Stop bits                            */
  /*                                        0 - 1 Stop bit                       */
  /*                                        1 - 1.5 Stop bits                    */
  /*                                        2 - 2 Stop bits                      */
  /* 5      | bParityType |  1   | Number | Parity                               */
  /*                                        0 - None                             */
  /*                                        1 - Odd                              */
  /*                                        2 - Even                             */
  /*                                        3 - Mark                             */
  /*                                        4 - Space                            */
  /* 6      | bDataBits  |   1   | Number Data bits (5, 6, 7, 8 or 16).          */
  /*******************************************************************************/
    case CDC_SET_LINE_CODING:

    break;

    case CDC_GET_LINE_CODING:

    break;

    case CDC_SET_CONTROL_LINE_STATE:

    break;

    case CDC_SEND_BREAK:

    break;

  default:
    break;
  }

  return (USBD_OK);
  /* USER CODE END 5 */
}

/**
  * @brief  Data received over USB OUT endpoint are sent over CDC interface
  *         through this function.
  *
  *         @note
  *         This function will issue a NAK packet on any OUT packet received on
  *         USB endpoint until exiting this function. If you exit this function
  *         before transfer is complete on CDC interface (ie. using DMA controller)
  *         it will result in receiving more data while previous ones are still
  *         not sent.
  *
  * @param  Buf: Buffer of data to be received
  * @param  Len: Number of data received (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Receive_FS(uint8_t* Buf, uint32_t *Len)
{
  /* USER CODE BEGIN 6 */
  uint32_t rx_len = (Len != NULL) ? *Len : 0U;

  if (Buf != NULL && rx_len > 0U) {
    g_cdc_rx_count++;
    g_cdc_rx_last_len = rx_len;
    memset((void *)g_cdc_rx_last_cmd, 0, sizeof(g_cdc_rx_last_cmd));
    memcpy((void *)g_cdc_rx_last_cmd, Buf,
           (rx_len < sizeof(g_cdc_rx_last_cmd)) ? rx_len : sizeof(g_cdc_rx_last_cmd));

    if (CDC_CommandMatches(Buf, rx_len, "START")) {
      ECG_RequestStart();
    } else if (CDC_CommandMatches(Buf, rx_len, "STOP")) {
      ECG_RequestStop();
    } else if (CDC_CommandMatches(Buf, rx_len, "INFO")) {
      ECG_RequestUsbInfo();
    } else if (CDC_CommandMatches(Buf, rx_len, "SNAP")) {
      ECG_RequestSaveInfo();
    }
  }

  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &Buf[0]);
  USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  return (USBD_OK);
  /* USER CODE END 6 */
}

/**
  * @brief  CDC_Transmit_FS
  *         Data to send over USB IN endpoint are sent over CDC interface
  *         through this function.
  *         @note
  *
  *
  * @param  Buf: Buffer of data to be sent
  * @param  Len: Number of data to be sent (in bytes)
  * @retval USBD_OK if all operations are OK else USBD_FAIL or USBD_BUSY
  */
uint8_t CDC_Transmit_FS(uint8_t* Buf, uint16_t Len)
{
  uint8_t result = USBD_OK;
  /* USER CODE BEGIN 7 */
  if (Buf == NULL || Len == 0) {
    return USBD_FAIL;
  }

  if (Len > sizeof(CdcTxWorkBuffer) || !CDC_IsReady()) {
    return USBD_FAIL;
  }

  USBD_CDC_HandleTypeDef *hcdc = CDC_GetHandle();
  CDC_RecoverStuckTx();

  if (hcdc == NULL || hcdc->TxState != 0U) {
    return USBD_BUSY;
  }

  memcpy(CdcTxWorkBuffer, Buf, Len);
  CdcTxStartTick = HAL_GetTick();
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, CdcTxWorkBuffer, Len);
  result = USBD_CDC_TransmitPacket(&hUsbDeviceFS);
  if (result != USBD_OK) {
    CdcTxStartTick = 0U;
  }
  /* USER CODE END 7 */
  return result;
}

/**
  * @brief  CDC_TransmitCplt_FS
  *         Data transmitted callback
  *
  *         @note
  *         This function is IN transfer complete callback used to inform user that
  *         the submitted Data is successfully sent over USB.
  *
  * @param  Buf: Buffer of data to be received
  * @param  Len: Number of data received (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_TransmitCplt_FS(uint8_t *Buf, uint32_t *Len, uint8_t epnum)
{
  uint8_t result = USBD_OK;
  /* USER CODE BEGIN 13 */
  UNUSED(Buf);
  UNUSED(Len);
  UNUSED(epnum);
  CdcTxStartTick = 0U;
  USBD_CDC_HandleTypeDef *hcdc = CDC_GetHandle();
  if (hcdc != NULL) {
    hcdc->TxState = 0U;
  }
  /* USER CODE END 13 */
  return result;
}

/* USER CODE BEGIN PRIVATE_FUNCTIONS_IMPLEMENTATION */

static uint8_t CDC_IsReady(void)
{
    return (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED &&
            hUsbDeviceFS.pClassDataCmsit[hUsbDeviceFS.classId] != NULL) ? 1U : 0U;
}

static USBD_CDC_HandleTypeDef *CDC_GetHandle(void)
{
    if (!CDC_IsReady()) {
        return NULL;
    }
    return (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassDataCmsit[hUsbDeviceFS.classId];
}

static void CDC_RecoverStuckTx(void)
{
    USBD_CDC_HandleTypeDef *hcdc = CDC_GetHandle();

    if (hcdc == NULL || hcdc->TxState == 0U) {
        return;
    }

    if (CdcTxStartTick == 0U) {
        hcdc->TxState = 0U;
        return;
    }

    HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);

    if ((HAL_GetTick() - CdcTxStartTick) > CDC_TX_STUCK_TIMEOUT_MS) {
        hcdc->TxState = 0U;
        CdcTxStartTick = 0U;
    }
}

static uint8_t CDC_CommandMatches(const uint8_t *buf, uint32_t len, const char *cmd)
{
    uint32_t i = 0U;

    while (i < len &&
           (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\r' || buf[i] == '\n')) {
        i++;
    }

    while (*cmd != '\0') {
        uint8_t c;

        if (i >= len) {
            return 0U;
        }

        c = buf[i++];
        if (c >= 'a' && c <= 'z') {
            c = (uint8_t)(c - ('a' - 'A'));
        }

        if (c != (uint8_t)*cmd++) {
            return 0U;
        }
    }

    if (i < len) {
        uint8_t c = buf[i];
        if (c != '\0' && c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            return 0U;
        }
    }

    return 1U;
}

/**
  * @brief  CDC_Transmit_FS_Blocking
  *         阻塞式 USB CDC 发送，带超时。
  * @param  Buf: 数据缓冲区
  * @param  Len: 数据长度（字节）
  * @param  timeout_ms: 超时时间（ms），0 表示无限等待
  * @retval USBD_OK 成功, USBD_BUSY 超时, USBD_FAIL 参数错误/USB未就绪
  */
uint8_t CDC_Transmit_FS_Blocking(uint8_t *Buf, uint16_t Len, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    if (Buf == NULL || Len == 0) {
        return USBD_FAIL;
    }

    /* 等待前一次传输完成（等待 TxState == 0） */
    while (1) {
        if (!CDC_IsReady()) {
            return USBD_FAIL;
        }

        USBD_CDC_HandleTypeDef *hcdc = CDC_GetHandle();

        HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);

        if (hcdc->TxState == 0) {
            break;
        }

        if (timeout_ms != 0 && (HAL_GetTick() - start) >= timeout_ms) {
            hcdc->TxState = 0;
            CdcTxStartTick = 0U;
            break;
        }

        if (osKernelGetState() == osKernelRunning) {
            osDelay(1);
        } else {
            /* 不依赖 HAL_Delay — uwTick 可能不递增，用短忙等代替 */
            for (volatile uint32_t i = 0; i < 5000; i++) {}
        }
    }

    /* 强制复位 TxState，防止前一次发送卡死阻塞后续所有发送 */
    {
        USBD_CDC_HandleTypeDef *hcdc = CDC_GetHandle();
        if (hcdc) {
            /* 如果 TxState 卡在 1 超过 100ms，强制清零 */
            if (hcdc->TxState != 0 && (HAL_GetTick() - start) > 100) {
                hcdc->TxState = 0;
                CdcTxStartTick = 0U;
            }
        }
    }

    /* 发起发送 */
    uint8_t ret = CDC_Transmit_FS(Buf, Len);
    if (ret != USBD_OK) {
        return ret;
    }

    /* 等待发送完成（等待 TxState == 0），最多等 50ms */
    uint32_t tx_wait = HAL_GetTick();
    uint32_t tx_timeout = (timeout_ms > 1000U) ? timeout_ms : 1000U;
    while (1) {
        if (!CDC_IsReady()) {
            return USBD_FAIL;
        }

        USBD_CDC_HandleTypeDef *hcdc = CDC_GetHandle();

        HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);

        if (hcdc->TxState == 0) {
            return USBD_OK;
        }

        if ((HAL_GetTick() - tx_wait) > tx_timeout) {
            /* 超时：强制清零 TxState，不阻塞后续输出 */
            hcdc->TxState = 0;
            CdcTxStartTick = 0U;
            return USBD_BUSY;
        }

        if (osKernelGetState() == osKernelRunning) {
            osDelay(1);
        } else {
            for (volatile uint32_t i = 0; i < 5000; i++) {}
        }
    }
}
/* USER CODE END PRIVATE_FUNCTIONS_IMPLEMENTATION */

/**
  * @}
  */

/**
  * @}
  */
