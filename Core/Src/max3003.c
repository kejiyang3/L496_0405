#include "max3003.h"
#include "spi.h"
#include "usart.h"
#include "usb_printf.h"
#include "app_log.h"
#include "ecg_record_control.h"
#include "sd_debug_log.h"
#include "cmsis_os.h"
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
            return HAL_TIMEOUT;
        }

        *(__IO uint8_t *)&hspi3.Instance->DR = tx[i];

        if (MAX30003_SPI_WaitSet(SPI_FLAG_RXNE) != HAL_OK) {
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

/* 外部引用 — Packagedata_AddEcgSample 弱实现 (可被外部覆盖) */
__attribute__((weak)) void Packagedata_AddEcgSample(int16_t ecg)
{
    (void)ecg;
    /* 默认空实现, 由外部模块 (如 edf_storage.c) 覆盖 */
}

/* 安全延时：osDelay 不依赖 HAL tick，内核运行中专用 */
static void SAFE_Delay(uint32_t ms)
{
    if (ms > 0) osDelay(ms);
}

/* DC Lead-Off 状态缓存 */
static MAX30003_LeadStatus_t g_lead_status = {
    .state = MAX30003_LEAD_UNKNOWN,
    .p_off = 0,
    .n_off = 0,
    .dc_loff = 0,
    .raw_status = 0,
    .last_update_ms = 0
};

/**
  * @brief  CS 引脚初始化
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
  * @brief  写寄存器并读回验证
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
  * @brief  SPI 写寄存器
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
  * @brief  SPI 读寄存器
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
  * @brief  软件复位
  */
void MAX30003_SwReset(void)
{
    MAX30003_WriteReg(MAX30003_SW_RST, 0x000000);
    SAFE_Delay(10);
}

/**
  * @brief  同步时序
  */
void MAX30003_Synch(void)
{
    MAX30003_WriteReg(MAX30003_SYNCH, 0x000000);
}

/**
  * @brief  FIFO 复位
  */
void MAX30003_FifoReset(void)
{
    MAX30003_WriteReg(MAX30003_FIFO_RST, 0x000000);
}

/**
  * @brief  初始化 MAX30003 (仅配置寄存器, 不开启 EINT/EOVF)
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

    /* 清掉复位后的旧 STATUS */
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

    /* CNFG_ECG: 先配好采样率/增益/滤波，再开 CNFG_GEN */
    APP_USB_LOG("[MAX30003_INIT] step=cnfg_ecg\r\n");
    if (!MAX30003_WriteVerify(MAX30003_CNFG_ECG,
                              MAX30003_CNFG_ECG_NORMAL,
                              "CNFG_ECG")) return;

    /* CNFG_GEN: 一次性写入 EN_ECG + EN_RBIAS + DCLOFF (0x081217) */
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

    /* Auto Fast Recovery: 双电极运动场景快速恢复 */
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

    /* 等待 PLL 锁定 */
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

    /* EFIT=4，约 5 个样本触发一次中断 */
    APP_USB_LOG("[MAX30003_INIT] step=mngr_int\r\n");
    if (!MAX30003_WriteVerify(MAX30003_MNGR_INT,
                              MAX30003_MNGR_INT_FAST,
                              "MNGR_INT")) return;

    /* 清 FIFO 并同步 */
    MAX30003_FifoReset();
    MAX30003_Synch();

    MAX30003_ReadReg(MAX30003_STATUS, &dummy);
#if MAX30003_INIT_USB_VERBOSE
    APP_USB_LOG("[MAX30003] Init done. STATUS=0x%06lX\r\n", dummy);
#endif

    /* SD 写入诊断阶段不要在传感器初始化里碰 FatFS。
     * 数据文件打开前的早期 SD 访问会干扰判断，寄存器快照后续放到录制日志里做。 */
}

/**
  * @brief  启动 ECG 采集流 — 开始前重置 FIFO/SYNCH/STATUS
  */
void MAX30003_StartStream(void)
{
    uint32_t status1 = 0;
    uint32_t status2 = 0;
    uint32_t status3 = 0;

    g_max30003_start_step = 10;

    /* 先关闭正常 ECG 中断，避免清 FIFO/SYNCH 期间触发任务通知 */
    g_max30003_start_step = 20;
    (void)MAX30003_WriteReg(MAX30003_EN_INT, MAX30003_EN_INT_IDLE);

    /* 关键: 真正开始记录前重新 FIFO_RST + SYNCH，清除 Init→Start 之间的旧数据 */
    g_max30003_start_step = 30;
    APP_USB_LOG("[MAX30003_INIT] step=fifo_synch\r\n");
    MAX30003_FifoReset();
    g_max30003_start_step = 40;
    MAX30003_Synch();

    /* 连续读两次 STATUS 清掉旧的 sticky flags */
    g_max30003_start_step = 50;
    (void)MAX30003_ReadReg(MAX30003_STATUS, &status1);
    g_max30003_start_status1 = status1;
    g_max30003_start_step = 60;
    (void)MAX30003_ReadReg(MAX30003_STATUS, &status2);
    g_max30003_start_status2 = status2;

    /* 打开正常 ECG 中断 */
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
  * @brief  停止 ECG 采集流 — 关闭中断并清 FIFO
  */
void MAX30003_StopStream(void)
{
    uint32_t status = 0;

    (void)MAX30003_WriteReg(MAX30003_EN_INT, MAX30003_EN_INT_IDLE);

    /* 清 FIFO + SYNCH，避免下一次 Start 带入旧样本 */
    MAX30003_FifoReset();
    MAX30003_Synch();

    (void)MAX30003_ReadReg(MAX30003_STATUS, &status);
    g_ecg_rec.last_status = status;

    APP_USB_LOG("[MAX30003] StopStream done. STATUS=0x%06lX\r\n", status);
}

/**
  * @brief  将 18位 ECG 数据转换为 int16_t
  */
int16_t MAX30003_ConvertData(uint32_t raw_data)
{
    int32_t val;

    // 提取 18 位 ECG 数据 (D[23:6])
    val = (raw_data >> 6) & 0x3FFFF;

    // 符号扩展 (18-bit signed to 32-bit)
    if (val & 0x20000)
    {
        val |= 0xFFFC0000;
    }

    // 右移 2 位以适应 int16_t 范围 (18-bit -> 16-bit)
    // 注意: MAX30003 的 ENOB 是 15.5 位，所以丢弃最低 2 位刚好能完美装入 int16_t，同时滤除多余底噪。
    val >>= 2;

    // 饱和处理，防止溢出
    if (val > 32767) val = 32767;
    if (val < -32768) val = -32768;

    return (int16_t)val;
}

/**
  * @brief  Burst 模式读取 FIFO
  */
static HAL_StatusTypeDef MAX30003_ReadFifoBurst(uint32_t *samples, uint8_t max_samples)
{
    if (max_samples == 0 || max_samples > FIFO_BURST_SIZE) return HAL_ERROR;

    // 1字节命令 + 每样本3字节
    uint16_t total = 1 + max_samples * 3; 
    uint8_t tx[1 + FIFO_BURST_SIZE * 3] = {0};
    uint8_t rx[1 + FIFO_BURST_SIZE * 3] = {0};
    
    tx[0] = (MAX30003_ECG_FIFO_BURST << 1) | 0x01;

    ECG_CSB_LOW();
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(&hspi3, tx, rx, total, 200);
    ECG_CSB_HIGH();

    if (st != HAL_OK) return st;

    for (int i = 0; i < max_samples; i++) {
        int off = 1 + i * 3; // 精简了索引偏移逻辑
        samples[i] = ((uint32_t)rx[off] << 16) | ((uint32_t)rx[off+1] << 8) | rx[off+2];
    }

    return HAL_OK;
}

/**
  * @brief  更新 STATUS 相关统计 (PLL seen / edge / last_status)
  */
static void MAX30003_UpdateStatusStats(uint32_t status_reg)
{
    g_ecg_rec.last_status = status_reg;

    if (status_reg & MAX30003_STATUS_PLLINT) {
        g_ecg_rec.pll_status_seen_count++;
        g_ecg_rec.pll_warn_count = g_ecg_rec.pll_status_seen_count;

        /* 边沿检测: 只有从 0→1 才加 edge count */
        if (!g_ecg_rec.pll_current_set) {
            g_ecg_rec.pll_edge_count++;
            g_ecg_rec.pll_current_set = 1;
        }
    } else {
        g_ecg_rec.pll_current_set = 0;
    }
}

/**
  * @brief  提取并处理 MAX30003 FIFO 数据 (drain loop, 最多 4 轮)
  * @note   Burst 读 FIFO，一次 32 word；避免在采样路径里调用阻塞/打印函数。
  */
void MAX30003_Task(void)
{
    uint8_t drain;

    for (drain = 0; drain < 4; drain++) {
        uint32_t status_reg = 0;
        uint8_t words_to_read = FIFO_BURST_SIZE;

        if (MAX30003_ReadReg(MAX30003_STATUS, &status_reg) != HAL_OK) {
            return;
        }

        MAX30003_UpdateStatusStats(status_reg);
        MAX30003_UpdateLeadStatus(status_reg);

        /* 处理 FIFO overflow */
        if (status_reg & MAX30003_STATUS_EOVF) {
            g_ecg_rec.fifo_eovf_count++;
            MAX30003_FifoReset();
            MAX30003_Synch();

            uint32_t dummy = 0;
            MAX30003_ReadReg(MAX30003_STATUS, &dummy);
            g_ecg_rec.last_status = dummy;
            return;
        }

        /* 无 EINT 时仍轻量探测 1 个 FIFO word。
         * 有些调试阶段 EINT/INTB 不稳定，但 FIFO 里可能已有样本。 */
        if ((status_reg & MAX30003_STATUS_EINT) == 0) {
            words_to_read = MAX30003_NO_EINT_DRAIN_SAMPLES;
        }

        uint32_t fifo_words[FIFO_BURST_SIZE];
        if (MAX30003_ReadFifoBurst(fifo_words, words_to_read) != HAL_OK) {
            return;
        }

        for (uint8_t i = 0; i < words_to_read; i++) {
            uint32_t raw_data = fifo_words[i];

            uint8_t etag = (raw_data >> 3) & 0x07;

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
