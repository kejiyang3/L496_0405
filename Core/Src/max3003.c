#include "max3003.h"
#include "spi.h"
#include "usart.h"
#include "usb_printf.h"
#include "app_log.h"
#include "ecg_record_control.h"
#include "sd_debug_log.h"
#include "cmsis_os.h"
extern volatile uint32_t ecg_irq_count;
#include <stdio.h>
#include <string.h>

#define MAX30003_SPI_WAIT_LIMIT 1000000U
#define MAX30003_INIT_USB_VERBOSE 1
#define MAX30003_INIT_SKIP_MNGR_DYN 1

volatile uint32_t g_max30003_init_step = 0;
volatile uint32_t g_max30003_init_reg = 0;
volatile uint32_t g_max30003_init_value = 0;
volatile uint32_t g_max30003_init_status = 0;
volatile uint32_t g_max30003_start_step = 0;
volatile uint32_t g_max30003_start_status1 = 0;
volatile uint32_t g_max30003_start_status2 = 0;
volatile uint32_t g_max30003_start_status3 = 0;

static HAL_StatusTypeDef MAX30003_SPI_WaitSet(uint32_t flag)
{
    uint32_t timeout = MAX30003_SPI_WAIT_LIMIT;
    while ((hspi3.Instance->SR & flag) == 0U) {
        if (--timeout == 0U) {
            return HAL_TIMEOUT;
        }
    }
    return HAL_OK;
}

static HAL_StatusTypeDef MAX30003_SPI_WaitReset(uint32_t flag)
{
    uint32_t timeout = MAX30003_SPI_WAIT_LIMIT;
    while ((hspi3.Instance->SR & flag) != 0U) {
        if (--timeout == 0U) {
            return HAL_TIMEOUT;
        }
    }
    return HAL_OK;
}

static HAL_StatusTypeDef MAX30003_SPI_Transfer(const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    if (tx == NULL || rx == NULL || len == 0U) {
        return HAL_ERROR;
    }

    if ((hspi3.Instance->CR1 & SPI_CR1_SPE) == 0U) {
        __HAL_SPI_ENABLE(&hspi3);
    }

    __HAL_SPI_CLEAR_OVRFLAG(&hspi3);

    for (uint16_t i = 0; i < len; i++) {
        if (MAX30003_SPI_WaitSet(SPI_FLAG_TXE) != HAL_OK) {
            g_ecg_rec.max30003_spi_timeout_count++;
            return HAL_TIMEOUT;
        }

        *(__IO uint8_t *)&hspi3.Instance->DR = tx[i];

        if (MAX30003_SPI_WaitSet(SPI_FLAG_RXNE) != HAL_OK) {
            g_ecg_rec.max30003_spi_timeout_count++;
            return HAL_TIMEOUT;
        }

        rx[i] = *(__IO uint8_t *)&hspi3.Instance->DR;
    }

    if (MAX30003_SPI_WaitSet(SPI_FLAG_TXE) != HAL_OK) {
        return HAL_TIMEOUT;
    }

    if (MAX30003_SPI_WaitReset(SPI_FLAG_BSY) != HAL_OK) {
        return HAL_TIMEOUT;
    }

    return HAL_OK;
}

/* 澶栭儴寮曠敤 �?Packagedata_AddEcgSample 寮卞疄鐜?(鍙澶栭儴瑕嗙�? */
__attribute__((weak)) void Packagedata_AddEcgSample(int16_t ecg)
{
    (void)ecg;
    /* 榛樿绌哄疄�? 鐢卞閮ㄦā鍧?(�?edf_storage.c) 瑕嗙�?*/
}

/* 瀹夊叏寤舵椂锛歰sDelay 涓嶄緷璧?HAL tick锛屽唴鏍歌繍琛屼腑涓撶敤 */
static void SAFE_Delay(uint32_t ms)
{
    if (ms > 0) osDelay(ms);
}

/* DC Lead-Off 鐘舵€佺紦�?*/
static MAX30003_LeadStatus_t g_lead_status = {
    .state = MAX30003_LEAD_UNKNOWN,
    .p_off = 0,
    .n_off = 0,
    .dc_loff = 0,
    .raw_status = 0,
    .last_update_ms = 0
};

/**
  * @brief  CS 寮曡剼鍒濆�?
  */
void MAX30003_CS_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    HAL_GPIO_WritePin(ECG_CS_GPIO_Port, ECG_CS_Pin, GPIO_PIN_SET);

    GPIO_InitStruct.Pin = ECG_CS_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(ECG_CS_GPIO_Port, &GPIO_InitStruct);
}

/**
  * @brief  鍐欏瘎瀛樺櫒骞惰鍥為獙璇?
  */
static int MAX30003_WriteVerify(uint8_t reg, uint32_t expected, const char *name)
{
    uint32_t readback = 0;
    g_max30003_init_reg = reg;
    g_max30003_init_value = expected;
    g_max30003_init_step = 0x100U | reg;
    if (MAX30003_WriteReg(reg, expected) != HAL_OK) {
        g_max30003_init_step = 0x180U | reg;
        APP_USB_LOG("[MAX30003][ERR] WRITE %s failed\r\n", name);
        return 0;
    }

    SAFE_Delay(1);

    g_max30003_init_step = 0x200U | reg;
    if (MAX30003_ReadReg(reg, &readback) != HAL_OK) {
        g_max30003_init_step = 0x280U | reg;
        APP_USB_LOG("[MAX30003][ERR] READBACK %s failed\r\n", name);
        return 0;
    }
    g_max30003_init_status = readback;

    if (readback != expected) {
        g_max30003_init_step = 0x300U | reg;
        APP_USB_LOG("[MAX30003][ERR] %s mismatch: wrote=0x%06lX read=0x%06lX\r\n",
                   name, expected, readback);
        return 0;
    }

#if MAX30003_INIT_USB_VERBOSE
    APP_USB_LOG("[MAX30003][OK] %s = 0x%06lX\r\n", name, readback);
#endif
    return 1;
}

/**
  * @brief  SPI 鍐欏瘎瀛樺�?
  */
HAL_StatusTypeDef MAX30003_WriteReg(uint8_t reg, uint32_t data)
{
    uint8_t tx[4];
    tx[0] = (reg << 1) | 0x00;
    tx[1] = (uint8_t)((data >> 16) & 0xFF);
    tx[2] = (uint8_t)((data >> 8)  & 0xFF);
    tx[3] = (uint8_t)( data        & 0xFF);

    ECG_CSB_LOW();
    HAL_StatusTypeDef st = HAL_SPI_Transmit(&hspi3, tx, 4, 100);
    ECG_CSB_HIGH();

    if(st != HAL_OK) {
        APP_USB_LOG("[SPI_ERR] WriteReg failed! HAL_Status: %d, SPI_State: %d, ErrorCode: %lu\r\n",
                   st, HAL_SPI_GetState(&hspi3), HAL_SPI_GetError(&hspi3));
    }
    return st;
}

/**
  * @brief  SPI 璇诲瘎瀛樺�?
  */
HAL_StatusTypeDef MAX30003_ReadReg(uint8_t reg, uint32_t *data)
{
    uint8_t tx[4] = {0};
    uint8_t rx[4] = {0};

    tx[0] = (reg << 1) | 0x01;

    ECG_CSB_LOW();
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(&hspi3, tx, rx, 4, 100);
    ECG_CSB_HIGH();

    if (st == HAL_OK && data != NULL) {
        *data = ((uint32_t)rx[1] << 16) | ((uint32_t)rx[2] << 8) | (uint32_t)rx[3];
    } else {
        APP_USB_LOG("[SPI_ERR] ReadReg failed! HAL_Status: %d, SPI_State: %d, ErrorCode: %lu\r\n",
                   st, HAL_SPI_GetState(&hspi3), HAL_SPI_GetError(&hspi3));
    }
    return st;
}

/**
  * @brief  杞欢澶嶄綅
  */
void MAX30003_SwReset(void)
{
    MAX30003_WriteReg(MAX30003_SW_RST, 0x000000);
    SAFE_Delay(10);
}

/**
  * @brief  鍚屾鏃跺簭
  */
void MAX30003_Synch(void)
{
    MAX30003_WriteReg(MAX30003_SYNCH, 0x000000);
}

/**
  * @brief  FIFO 澶嶄�?
  */
void MAX30003_FifoReset(void)
{
    MAX30003_WriteReg(MAX30003_FIFO_RST, 0x000000);
}

/**
  * @brief  鍒濆鍖?MAX30003 (浠呴厤缃瘎瀛樺�? 涓嶅紑鍚?EINT/EOVF)
  */
void MAX30003_Init(void)
{
    uint32_t dummy = 0;
    uint32_t info1 = 0, info2 = 0, info3 = 0;

#if MAX30003_INIT_USB_VERBOSE
    APP_USB_LOG("[MAX30003] Initializing...\r\n");
#endif

    APP_USB_LOG("[MAX30003_INIT] step=cs\r\n");
    MAX30003_CS_Init();

    APP_USB_LOG("[MAX30003_INIT] step=sw_reset\r\n");
    MAX30003_SwReset();
    SAFE_Delay(20);

    /* 娓呮帀澶嶄綅鍚庣殑�?STATUS */
    APP_USB_LOG("[MAX30003_INIT] step=status1\r\n");
    MAX30003_ReadReg(MAX30003_STATUS, &dummy);
    SAFE_Delay(2);
    APP_USB_LOG("[MAX30003_INIT] step=status2\r\n");
    MAX30003_ReadReg(MAX30003_STATUS, &dummy);

    APP_USB_LOG("[MAX30003_INIT] step=info1\r\n");
    if (MAX30003_ReadReg(MAX30003_INFO, &info1) != HAL_OK) return;
    SAFE_Delay(1);
    APP_USB_LOG("[MAX30003_INIT] step=info2\r\n");
    if (MAX30003_ReadReg(MAX30003_INFO, &info2) != HAL_OK) return;
    SAFE_Delay(1);
    APP_USB_LOG("[MAX30003_INIT] step=info3\r\n");
    if (MAX30003_ReadReg(MAX30003_INFO, &info3) != HAL_OK) return;

#if MAX30003_INIT_USB_VERBOSE
    APP_USB_LOG("[MAX30003] INFO=0x%06lX\r\n", info1);
#endif

    /* CNFG_ECG: 鍏堥厤濂介噰鏍风�?澧炵�?婊ゆ尝锛屽啀寮€ CNFG_GEN */
    APP_USB_LOG("[MAX30003_INIT] step=cnfg_ecg\r\n");
    if (!MAX30003_WriteVerify(MAX30003_CNFG_ECG,
                              MAX30003_CNFG_ECG_NORMAL,
                              "CNFG_ECG")) return;

    /* CNFG_GEN: 涓€娆℃€у啓�?EN_ECG + EN_RBIAS + DCLOFF (0x081217) */
    APP_USB_LOG("[MAX30003_INIT] step=cnfg_gen\r\n");
    if (!MAX30003_WriteVerify(MAX30003_CNFG_GEN,
                              MAX30003_CNFG_GEN_NORMAL,
                              "CNFG_GEN")) return;

#if MAX30003_USE_INTERNAL_CAL_TEST
    APP_USB_LOG("[MAX30003_INIT] step=cnfg_cal\r\n");
    if (!MAX30003_WriteVerify(MAX30003_CNFG_CAL,
                              MAX30003_CNFG_CAL_1HZ_BIPOLAR,
                              "CNFG_CAL")) return;

    APP_USB_LOG("[MAX30003_INIT] step=cnfg_emux\r\n");
    if (!MAX30003_WriteVerify(MAX30003_CNFG_EMUX,
                              MAX30003_CNFG_EMUX_CAL_DIFF,
                              "CNFG_EMUX")) return;
#else
    if (!MAX30003_WriteVerify(MAX30003_CNFG_CAL,
                              0x000000,
                              "CNFG_CAL")) return;

    if (!MAX30003_WriteVerify(MAX30003_CNFG_EMUX,
                              0x000000,
                              "CNFG_EMUX")) return;
#endif

    /* Auto Fast Recovery: 鍙岀數鏋佽繍鍔ㄥ満鏅揩閫熸仮�?*/
    APP_USB_LOG("[MAX30003_INIT] step=mngr_dyn%s\r\n",
                MAX30003_INIT_SKIP_MNGR_DYN ? "_skip" : "");
#if MAX30003_INIT_SKIP_MNGR_DYN
    g_max30003_init_reg = MAX30003_MNGR_DYN;
    g_max30003_init_value = 0;
    g_max30003_init_step = 0x500U | MAX30003_MNGR_DYN;
#else
    if (!MAX30003_WriteVerify(MAX30003_MNGR_DYN,
                              MAX30003_MNGR_DYN_AUTO_FAST,
                              "MNGR_DYN")) return;
#endif

    /* 绛夊�?PLL 閿佸�?*/
    APP_USB_LOG("[MAX30003_INIT] step=pll_wait\r\n");
    uint8_t retry = 50;
    while(retry--) {
        MAX30003_ReadReg(MAX30003_STATUS, &dummy);
        if((dummy & MAX30003_STATUS_PLLINT) == 0) break;
        SAFE_Delay(2);
    }

    APP_USB_LOG("[MAX30003_INIT] step=en_int\r\n");
    if (!MAX30003_WriteVerify(MAX30003_EN_INT,
                              MAX30003_EN_INT_IDLE,
                              "EN_INT")) return;

    /* EFIT=4锛岀�?5 涓牱鏈Е鍙戜竴娆′腑�?*/
    APP_USB_LOG("[MAX30003_INIT] step=mngr_int\r\n");
    if (!MAX30003_WriteVerify(MAX30003_MNGR_INT,
                              MAX30003_MNGR_INT_FAST,
                              "MNGR_INT")) return;

    /* �?FIFO 骞跺悓姝?*/
    MAX30003_FifoReset();
    MAX30003_Synch();

    MAX30003_ReadReg(MAX30003_STATUS, &dummy);
#if MAX30003_INIT_USB_VERBOSE
    APP_USB_LOG("[MAX30003] Init done. STATUS=0x%06lX\r\n", dummy);
#endif

    /* SD 鍐欏叆璇婃柇闃舵涓嶈鍦ㄤ紶鎰熷櫒鍒濆鍖栭噷�?FatFS�?
     * 鏁版嵁鏂囦欢鎵撳紑鍓嶇殑鏃╂�?SD 璁块棶浼氬共鎵板垽鏂紝瀵勫瓨鍣ㄥ揩鐓у悗缁斁鍒板綍鍒舵棩蹇楅噷鍋氥�?*/
}

/**
  * @brief  鍚�?ECG 閲囬泦娴?�?寮€濮嬪墠閲嶇疆 FIFO/SYNCH/STATUS
  */
void MAX30003_StartStream(void)
{
    uint32_t status1 = 0;
    uint32_t status2 = 0;
    uint32_t status3 = 0;

    g_max30003_start_step = 10;

    /* 鍏堝叧闂�?ECG 涓柇锛岄伩鍏嶆�?FIFO/SYNCH 鏈熼棿瑙﹀彂浠诲姟閫氱�?*/
    g_max30003_start_step = 20;
    (void)MAX30003_WriteReg(MAX30003_EN_INT, MAX30003_EN_INT_IDLE);

    /* 鍏抽�? 鐪熸寮€濮嬭褰曞墠閲嶆柊 FIFO_RST + SYNCH锛屾竻闄?Init鈫扴tart 涔嬮棿鐨勬棫鏁版�?*/
    g_max30003_start_step = 30;
    APP_USB_LOG("[MAX30003_INIT] step=fifo_synch\r\n");
    MAX30003_FifoReset();
    g_max30003_start_step = 40;
    MAX30003_Synch();

    /* 杩炵画璇讳袱�?STATUS 娓呮帀鏃х殑 sticky flags */
    g_max30003_start_step = 50;
    (void)MAX30003_ReadReg(MAX30003_STATUS, &status1);
    g_max30003_start_status1 = status1;
    g_max30003_start_step = 60;
    (void)MAX30003_ReadReg(MAX30003_STATUS, &status2);
    g_max30003_start_status2 = status2;

    /* 鎵撳紑姝ｅ父 ECG 涓�?*/
    g_max30003_start_step = 70;
    if (MAX30003_WriteReg(MAX30003_EN_INT, MAX30003_EN_INT_NORMAL) != HAL_OK) {
        g_max30003_start_step = 71;
        return;
    }

    g_max30003_start_step = 80;
    (void)MAX30003_ReadReg(MAX30003_STATUS, &status3);
    g_max30003_start_status3 = status3;

    g_max30003_start_step = 90;
    g_ecg_rec.last_status = status3;
    MAX30003_UpdateLeadStatus(status3);
    g_max30003_start_step = 100;

#if MAX30003_INIT_USB_VERBOSE
    APP_USB_LOG("[MAX30003] StartStream status1=0x%06lX status2=0x%06lX status3=0x%06lX\r\n",
                status1, status2, status3);
#endif
}

/**
  * @brief  鍋滄�?ECG 閲囬泦娴?�?鍏抽棴涓柇骞舵�?FIFO
  */
void MAX30003_StopStream(void)
{
    uint32_t status = 0;

    (void)MAX30003_WriteReg(MAX30003_EN_INT, MAX30003_EN_INT_IDLE);

    /* �?FIFO + SYNCH锛岄伩鍏嶄笅涓€�?Start 甯﹀叆鏃ф牱鏈?*/
    MAX30003_FifoReset();
    MAX30003_Synch();

    (void)MAX30003_ReadReg(MAX30003_STATUS, &status);
    g_ecg_rec.last_status = status;

    APP_USB_LOG("[MAX30003] StopStream done. STATUS=0x%06lX\r\n", status);
}

/**
  * @brief  �?18�?ECG 鏁版嵁杞崲�?int16_t
  */
int16_t MAX30003_ConvertData(uint32_t raw_data)
{
    int32_t val;

    // 鎻愬�?18 �?ECG 鏁版�?(D[23:6])
    val = (raw_data >> 6) & 0x3FFFF;

    // 绗﹀彿鎵╁睍 (18-bit signed to 32-bit)
    if (val & 0x20000)
    {
        val |= 0xFFFC0000;
    }

    // 鍙崇Щ 2 浣嶄互閫傚簲 int16_t 鑼冨�?(18-bit -> 16-bit)
    // 娉ㄦ�? MAX30003 �?ENOB �?15.5 浣嶏紝鎵€浠ヤ涪寮冩渶浣?2 浣嶅垰濂借兘瀹岀編瑁呭叆 int16_t锛屽悓鏃舵护闄ゅ浣欏簳鍣�?
    val >>= 2;

    // 楗卞拰澶勭悊锛岄槻姝㈡孩�?
    if (val > 32767) val = 32767;
    if (val < -32768) val = -32768;

    return (int16_t)val;
}

/**
  * @brief  Burst 妯″紡璇诲�?FIFO
  */
static HAL_StatusTypeDef MAX30003_ReadFifoBurst(uint32_t *samples, uint8_t max_samples)
{
    if (max_samples == 0 || max_samples > FIFO_BURST_SIZE) return HAL_ERROR;

    // 1瀛楄妭鍛戒护 + 姣忔牱鏈?瀛楄�?
    uint16_t total = 1 + max_samples * 3; 
    uint8_t tx[1 + FIFO_BURST_SIZE * 3] = {0};
    uint8_t rx[1 + FIFO_BURST_SIZE * 3] = {0};
    
    tx[0] = (MAX30003_ECG_FIFO_BURST << 1) | 0x01;

    ECG_CSB_LOW();
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(&hspi3, tx, rx, total, 200);
    ECG_CSB_HIGH();

    if (st != HAL_OK) return st;

    for (int i = 0; i < max_samples; i++) {
        int off = 1 + i * 3; // 绮剧畝浜嗙储寮曞亸绉婚€昏緫
        samples[i] = ((uint32_t)rx[off] << 16) | ((uint32_t)rx[off+1] << 8) | rx[off+2];
    }

    return HAL_OK;
}

/**
  * @brief  鏇存�?STATUS 鐩稿叧缁熻 (PLL seen / edge / last_status)
  */
static void MAX30003_UpdateStatusStats(uint32_t status_reg)
{
    g_ecg_rec.last_status = status_reg;

    if (status_reg & MAX30003_STATUS_PLLINT) {
        g_ecg_rec.pll_status_seen_count++;
        g_ecg_rec.pll_warn_count = g_ecg_rec.pll_status_seen_count;

        /* 杈规部妫€娴? 鍙湁浠?0�? 鎵嶅�?edge count */
        if (!g_ecg_rec.pll_current_set) {
            g_ecg_rec.pll_edge_count++;
            g_ecg_rec.pll_current_set = 1;
        }
    } else {
        g_ecg_rec.pll_current_set = 0;
    }
}

/**
  * @brief  鎻愬彇骞跺�?MAX30003 FIFO 鏁版�?(drain loop, 鏈€�?4 �?
  * @note   Burst �?FIFO锛屼竴娆?32 word锛涢伩鍏嶅湪閲囨牱璺緞閲岃皟鐢ㄩ樆�?鎵撳嵃鍑芥暟�?
  */
void MAX30003_Task(void)
{
    /* DIAG: count calls and track max gap */
    g_ecg_rec.diag_task_calls++;
    /* INTB raw GPIO read */
    if (HAL_GPIO_ReadPin(ECG_INT_GPIO_Port, ECG_INT_Pin) == GPIO_PIN_RESET) {
        g_ecg_rec.diag_intb_low_count++;
    } else {
        g_ecg_rec.diag_intb_high_count++;
    }
    {
        uint32_t _now = HAL_GetTick();
        if (g_ecg_rec.diag_last_call_tick != 0U) {
            uint32_t _gap = _now - g_ecg_rec.diag_last_call_tick;
            if (_gap > g_ecg_rec.diag_max_gap_ms) {
                g_ecg_rec.diag_max_gap_ms = _gap;
            }
        }
        g_ecg_rec.diag_last_call_tick = _now;
    }

    uint8_t drain;

    for (drain = 0; drain < 4; drain++) {
        uint32_t status_reg = 0;
        uint8_t words_to_read = FIFO_BURST_SIZE;

        if (MAX30003_ReadReg(MAX30003_STATUS, &status_reg) != HAL_OK) {
            return;
        }

        MAX30003_UpdateStatusStats(status_reg);
        MAX30003_UpdateLeadStatus(status_reg);

        /* 澶勭�?FIFO overflow */
        if (status_reg & MAX30003_STATUS_EOVF) {
            g_ecg_rec.fifo_eovf_count++;
            MAX30003_FifoReset();
            MAX30003_Synch();

            uint32_t dummy = 0;
            MAX30003_ReadReg(MAX30003_STATUS, &dummy);
            g_ecg_rec.last_status = dummy;
            return;
        }

        /* �?EINT 鏃朵粛杞婚噺鎺㈡�?1 �?FIFO word�?
         * 鏈変簺璋冭瘯闃舵�?EINT/INTB 涓嶇ǔ瀹氾紝浣?FIFO 閲屽彲鑳藉凡鏈夋牱鏈€?*/
        if ((status_reg & MAX30003_STATUS_EINT) != 0) {
            g_ecg_rec.diag_eint_hits++;
            g_ecg_rec.diag_status_eint_total++;
        }
        if ((status_reg & MAX30003_STATUS_EOVF) != 0) {
            g_ecg_rec.diag_status_eovf_total++;
        }

        uint32_t fifo_words[FIFO_BURST_SIZE];
        g_ecg_rec.max30003_spi_burst_count++;
        {
            uint32_t _t0 = HAL_GetTick();
            HAL_StatusTypeDef _br = MAX30003_ReadFifoBurst(fifo_words, words_to_read);
            uint32_t _dt = HAL_GetTick() - _t0;
            if (_dt > 0) {
                g_ecg_rec.max30003_burst_us_last = _dt * 1000;
                if (_dt * 1000 > g_ecg_rec.max30003_burst_us_max)
                    g_ecg_rec.max30003_burst_us_max = _dt * 1000;
                g_ecg_rec.max30003_burst_us_sum += _dt * 1000;
                g_ecg_rec.max30003_burst_us_count++;
            }
            if (_br != HAL_OK) {
                g_ecg_rec.max30003_burst_read_error_count++;
                return;
            }
        }

        for (uint8_t i = 0; i < words_to_read; i++) {
            uint32_t raw_data = fifo_words[i];

            uint8_t etag = (raw_data >> 3) & 0x07;
            if (etag < 8) g_ecg_rec.etag_hist[etag]++;

            if (etag == 0x00 || etag == 0x02) {
                int16_t ecg_val = MAX30003_ConvertData(raw_data);

                g_ecg_rec.fifo_sample_count++;
                g_ecg_rec.fifo_valid_count++;
                Packagedata_AddEcgSample(ecg_val);

                if (etag == 0x02) {
                    g_ecg_rec.fifo_last_count++;
                    break;
                }
            }
            else if (etag == 0x01 || etag == 0x03) {
                g_ecg_rec.fifo_sample_count++;
                g_ecg_rec.fifo_fast_count++;

                if (etag == 0x03) {
                    g_ecg_rec.fifo_last_count++;
                    break;
                }
            }
            else if (etag == 0x06) {
                g_ecg_rec.fifo_empty_count++;
                return;
            }
            else if (etag == 0x07) {
                g_ecg_rec.fifo_etag_overflow_count++;
                g_ecg_rec.fifo_eovf_count++;
                MAX30003_FifoReset();
                MAX30003_Synch();
                return;
            }
            else {
                g_ecg_rec.fifo_unknown_etag_count++;
                break;
            }
        }
    }
}



/* ================================================================
 * SD Diagnostic Log: writes ECG software stats to SD every 1s
 * ================================================================ */
#include "sd_debug_log.h"
#include "record_feature_flags.h"

static uint32_t _diag_last_log_tick = 0;

void MAX30003_DiagLog_Init(void)
{
    char buf[256];
    uint32_t cnfg_gen = 0, cnfg_ecg = 0;
    MAX30003_ReadReg(MAX30003_CNFG_GEN, &cnfg_gen);
    MAX30003_ReadReg(MAX30003_CNFG_ECG, &cnfg_ecg);
    uint32_t fmstr = (cnfg_gen >> 20) & 0x03U;
    uint32_t rate  = (cnfg_ecg >> 22) & 0x03U;

    snprintf(buf, sizeof(buf),
        "INIT,%lu,status=0x%06lX,info=0x%06lX,cnfg_gen=0x%06lX,cnfg_ecg=0x%06lX,fmstr=%lu,rate=%lu,mngr_int=0x%06lX,en_int=0x%06lX",
        (unsigned long)HAL_GetTick(),
        (unsigned long)g_ecg_rec.last_status,
        (unsigned long)g_max30003_init_value,
        (unsigned long)cnfg_gen, (unsigned long)cnfg_ecg,
        (unsigned long)fmstr, (unsigned long)rate,
        0x200000UL, 0x000002UL);
    SD_DebugLog_WriteLine(buf);
    _diag_last_log_tick = HAL_GetTick();
}

void MAX30003_DiagLog_Run(void)
{
    uint32_t now = HAL_GetTick();
    if (now - _diag_last_log_tick < RECORD_DIAG_RUN_INTERVAL_MS) return;
    _diag_last_log_tick = now;

    char buf[384];
    snprintf(buf, sizeof(buf),
        "RUN,%lu,"
        "task=%lu,irq=%lu,eint=%lu,wake=%lu,timeout=%lu,max_gap=%lu,"
        "status=0x%06lX,"
        "fifo_raw=%lu,fifo_valid=%lu,fifo_fast=%lu,fifo_empty=%lu,fifo_last=%lu,eovf=%lu,etag_ovf=%lu,unk=%lu,"
        "etag=0:%lu,1:%lu,2:%lu,3:%lu,4:%lu,5:%lu,6:%lu,7:%lu,"
        "pack_in=%lu,pack_drop=%lu,buf_max=%lu,"
        "sd_written=%lu,sd_drop=%lu,sd_err=%lu,sd_flush=%lu,"
        "spi_burst=%lu,spi_err=%lu,spi_tmo=%lu,burst_us=%lu,burst_max=%lu",
        (unsigned long)now,
        (unsigned long)g_ecg_rec.diag_task_calls,
        (unsigned long)ecg_irq_count,
        (unsigned long)g_ecg_rec.diag_eint_hits,
        (unsigned long)g_ecg_rec.diag_notify_wakes,
        (unsigned long)g_ecg_rec.diag_notify_timeouts,
        (unsigned long)g_ecg_rec.diag_max_gap_ms,
        (unsigned long)g_ecg_rec.last_status,
        (unsigned long)g_ecg_rec.fifo_sample_count,
        (unsigned long)g_ecg_rec.fifo_valid_count,
        (unsigned long)g_ecg_rec.fifo_fast_count,
        (unsigned long)g_ecg_rec.fifo_empty_count,
        (unsigned long)g_ecg_rec.fifo_last_count,
        (unsigned long)g_ecg_rec.fifo_eovf_count,
        (unsigned long)g_ecg_rec.fifo_etag_overflow_count,
        (unsigned long)g_ecg_rec.fifo_unknown_etag_count,
        (unsigned long)g_ecg_rec.etag_hist[0], (unsigned long)g_ecg_rec.etag_hist[1],
        (unsigned long)g_ecg_rec.etag_hist[2], (unsigned long)g_ecg_rec.etag_hist[3],
        (unsigned long)g_ecg_rec.etag_hist[4], (unsigned long)g_ecg_rec.etag_hist[5],
        (unsigned long)g_ecg_rec.etag_hist[6], (unsigned long)g_ecg_rec.etag_hist[7],
        (unsigned long)g_ecg_rec.pack_add_ok_count,
        (unsigned long)g_ecg_rec.pack_add_drop_count,
        (unsigned long)g_ecg_rec.pack_buffer_level_max,
        (unsigned long)g_ecg_rec.ecg_written_count,
        (unsigned long)g_ecg_rec.ecg_drop_count,
        (unsigned long)g_ecg_rec.ecg_drop_count,
        (unsigned long)g_ecg_rec.sd_sync_count,
        (unsigned long)g_ecg_rec.max30003_spi_burst_count,
        (unsigned long)g_ecg_rec.max30003_spi_error_count,
        (unsigned long)g_ecg_rec.max30003_spi_timeout_count,
        (unsigned long)g_ecg_rec.max30003_burst_us_last,
        (unsigned long)g_ecg_rec.max30003_burst_us_max);
    SD_DebugLog_WriteLine(buf);
}

void MAX30003_DiagLog_Stop(void)
{
    uint32_t duration_ms = g_ecg_rec.stop_tick - g_ecg_rec.start_tick;
    uint32_t actual_fifo_sps = (duration_ms > 0)
        ? (g_ecg_rec.fifo_valid_count * 1000UL / duration_ms) : 0;
    uint32_t actual_sd_sps = (duration_ms > 0)
        ? (g_ecg_rec.ecg_written_count * 1000UL / duration_ms) : 0;

    char buf[384];
    snprintf(buf, sizeof(buf),
        "STOP,%lu,"
        "duration_ms=%lu,"
        "fifo_valid=%lu,pack_in=%lu,pack_drop=%lu,"
        "sd_written=%lu,sd_drop=%lu,sd_err=%lu,"
        "actual_fifo_sps=%lu,actual_sd_sps=%lu,"
        "eovf=%lu,etag_ovf=%lu,spi_err=%lu,spi_tmo=%lu,"
        "max_gap=%lu,burst_max_us=%lu",
        (unsigned long)HAL_GetTick(),
        (unsigned long)duration_ms,
        (unsigned long)g_ecg_rec.fifo_valid_count,
        (unsigned long)g_ecg_rec.pack_add_ok_count,
        (unsigned long)g_ecg_rec.pack_add_drop_count,
        (unsigned long)g_ecg_rec.ecg_written_count,
        (unsigned long)g_ecg_rec.ecg_drop_count,
        (unsigned long)g_ecg_rec.ecg_drop_count,
        (unsigned long)actual_fifo_sps, (unsigned long)actual_sd_sps,
        (unsigned long)g_ecg_rec.fifo_eovf_count,
        (unsigned long)g_ecg_rec.fifo_etag_overflow_count,
        (unsigned long)g_ecg_rec.max30003_spi_error_count,
        (unsigned long)g_ecg_rec.max30003_spi_timeout_count,
        (unsigned long)g_ecg_rec.diag_max_gap_ms,
        (unsigned long)g_ecg_rec.max30003_burst_us_max);
    SD_DebugLog_WriteLine(buf);
}
int32_t MAX30003_Read_Sample(void)
{
    uint32_t raw_data;
    if (MAX30003_ReadReg(MAX30003_ECG_FIFO, &raw_data) != HAL_OK) {
        return 0;
    }
    return (int32_t)raw_data;
}

void MAX30003_UpdateLeadStatus(uint32_t status)
{
    uint8_t ph = (status & MAX30003_STATUS_LDOFF_PH) ? 1 : 0;
    uint8_t pl = (status & MAX30003_STATUS_LDOFF_PL) ? 1 : 0;
    uint8_t nh = (status & MAX30003_STATUS_LDOFF_NH) ? 1 : 0;
    uint8_t nl = (status & MAX30003_STATUS_LDOFF_NL) ? 1 : 0;

    g_lead_status.raw_status = status;
    g_lead_status.last_update_ms = HAL_GetTick();
    g_lead_status.dc_loff = (status & MAX30003_STATUS_DCLOFFINT) ? 1 : 0;
    g_lead_status.p_off = (ph || pl) ? 1 : 0;
    g_lead_status.n_off = (nh || nl) ? 1 : 0;

    if (g_lead_status.dc_loff || ph || pl || nh || nl) {
        g_lead_status.state = MAX30003_LEAD_OFF;
    } else {
        g_lead_status.state = MAX30003_LEAD_ON;
    }
}

void MAX30003_GetLeadStatus(MAX30003_LeadStatus_t *out)
{
    if (out == NULL) return;
    *out = g_lead_status;
}

void MAX30003_PollLeadStatus(void)
{
    uint32_t status = 0;
    if (MAX30003_ReadReg(MAX30003_STATUS, &status) == HAL_OK) {
        MAX30003_UpdateStatusStats(status);
        MAX30003_UpdateLeadStatus(status);
    }
}

void MAX30003_Diagnostic_Dump(void)
{
    uint32_t reg[5];

    APP_USB_LOG("\r\n======================================================\r\n");
    APP_USB_LOG("         MAX30003 COMPREHENSIVE DIAGNOSTIC            \r\n");
    APP_USB_LOG("======================================================\r\n");

    /* Read core configuration registers */
    MAX30003_ReadReg(MAX30003_INFO, &reg[0]);
    MAX30003_ReadReg(MAX30003_CNFG_GEN, &reg[1]);
    MAX30003_ReadReg(MAX30003_CNFG_EMUX, &reg[2]);
    MAX30003_ReadReg(MAX30003_CNFG_ECG, &reg[3]);
    MAX30003_ReadReg(MAX30003_CNFG_CAL, &reg[4]);

    APP_USB_LOG("[0x0F] INFO       : 0x%06X\r\n", (unsigned int)reg[0]);
    APP_USB_LOG("[0x10] CNFG_GEN   : 0x%06X\r\n", (unsigned int)reg[1]);
    APP_USB_LOG("[0x14] CNFG_EMUX  : 0x%06X (OPENP=%lu, OPENN=%lu)\r\n",
               (unsigned int)reg[2], (reg[2]>>21)&1, (reg[2]>>20)&1);
    APP_USB_LOG("[0x15] CNFG_ECG   : 0x%06X\r\n", (unsigned int)reg[3]);
    APP_USB_LOG("[0x12] CNFG_CAL   : 0x%06X\r\n", (unsigned int)reg[4]);

    APP_USB_LOG("\r\n--- Executing Active DC Lead-Off Test ---\r\n");
    APP_USB_LOG("Checking physical PCB trace continuity...\r\n");

    /*
     * Enable DC Lead-Off for testing:
     * EN_DCLOFF=01 (bits 13:12), IMAG=010 for 10nA (bits 10:8)
     */
    MAX30003_WriteReg(MAX30003_CNFG_GEN, MAX30003_CNFG_GEN_NORMAL | (1UL << 12) | (2UL << 8));

    /* Wait 200ms for internal comparators to stabilize (Datasheet requires > 115ms) */
    HAL_Delay(200);

    uint32_t status_reg;
    MAX30003_ReadReg(MAX30003_STATUS, &status_reg);
    APP_USB_LOG("[0x01] STATUS     : 0x%06X\r\n", (unsigned int)status_reg);

    uint8_t loff = status_reg & 0x0F;
    APP_USB_LOG("\r\n[HARDWARE DIAGNOSIS RESULT]:\r\n");
    if (loff == 0) {
        APP_USB_LOG(" -> CLOSED: Both electrodes are physically connected.\r\n");
    } else {
        if (loff & 0x08) APP_USB_LOG(" -> [FAIL] ECGP (Positive / Pin 6) is physically OPEN or floating!\r\n");
        if (loff & 0x04) APP_USB_LOG(" -> [FAIL] ECGP (Positive / Pin 6) is SHORTED to GND!\r\n");
        if (loff & 0x02) APP_USB_LOG(" -> [FAIL] ECGN (Negative / Pin 7) is physically OPEN or floating!\r\n");
        if (loff & 0x01) APP_USB_LOG(" -> [FAIL] ECGN (Negative / Pin 7) is SHORTED to GND!\r\n");
    }

    /* Restore normal clean AFE configuration */
    MAX30003_WriteReg(MAX30003_CNFG_GEN, MAX30003_CNFG_GEN_NORMAL);

    APP_USB_LOG("======================================================\r\n");
}
