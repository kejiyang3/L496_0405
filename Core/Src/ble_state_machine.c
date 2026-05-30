/*
 * ble_state_machine.c - BLE命令解析与状态机实现
 *
 * 协议说明:
 *   - 基于行的简单文本协议
 *   - 命令以 \r\n 或 \n 结束
 *   - 命令大小写不敏感
 *   - 响应以 + 开头
 *
 * 支持的命令:
 *   PING            -> +PONG\r\n
 *   START           -> +OK\r\n 或 +ERR:recording\r\n
 *   STOP            -> +OK\r\n
 *   STATUS          -> +STATE:rec,0,ecg,0,mic,0,bat,85\r\n
 *   FNAME           -> +FNAME:ecg_001.csv\r\n
 *   INFO            -> +INFO:L496,v0.3,codex2\r\n
 *   SYNC:<time>     -> +OK\r\n
 */

#include "ble_state_machine.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* UART发送 (由外部提供) */
extern void BLE_SM_Send(const char *data);

/* 外部变量 - 录音状态 */
extern volatile uint8_t ecg_streaming;

/* 内部状态 */
static BLE_State_t g_ble_state = BLE_STATE_IDLE;

/* 辅助: 大小写不敏感字符串比较 */
static int str_ieq(const char *a, const char *b, uint16_t len) {
    if (len == 0) return -1;
    for (uint16_t i = 0; i < len; i++) {
        char ca = a[i];
        char cb = b[i];
        if (ca == '\0' || cb == '\0') {
            if (ca == '\0' && cb == '\0') return 0;
            return (ca == '\0') ? -1 : 1;
        }
        if (tolower((unsigned char)ca) != tolower((unsigned char)cb)) {
            return ((unsigned char)tolower((unsigned char)ca) - (unsigned char)tolower((unsigned char)cb));
        }
    }
    return 0;
}

/* 解析命令行 */
static BLE_Cmd_t parse_command(const char *line, uint16_t len,
                                const char **param, uint16_t *param_len)
{
    *param = NULL;
    *param_len = 0;

    /* 找冒号分隔符 */
    uint16_t cmd_end = 0;
    for (uint16_t i = 0; i < len; i++) {
        if (line[i] == ':') {
            cmd_end = i;
            *param = line + i + 1;
            *param_len = len - i - 1;
            break;
        }
    }
    if (*param == NULL) {
        cmd_end = len;
    }

    /* 匹配命令 */
    if (str_ieq(line, "PING", cmd_end) == 0) return BLE_CMD_PING;
    if (str_ieq(line, "START", cmd_end) == 0) return BLE_CMD_START;
    if (str_ieq(line, "STOP", cmd_end) == 0) return BLE_CMD_STOP;
    if (str_ieq(line, "STATUS", cmd_end) == 0) return BLE_CMD_STATUS;
    if (str_ieq(line, "FNAME", cmd_end) == 0) return BLE_CMD_FNAME;
    if (str_ieq(line, "INFO", cmd_end) == 0) return BLE_CMD_INFO;
    if (str_ieq(line, "SYNC", cmd_end) == 0) return BLE_CMD_SYNC;

    return BLE_CMD_UNKNOWN;
}

/* === 公共API === */

void BLE_SM_Init(void) {
    g_ble_state = BLE_STATE_IDLE;
}

BLE_State_t BLE_SM_GetState(void) {
    return g_ble_state;
}

const char *BLE_SM_GetStateStr(void) {
    switch (g_ble_state) {
        case BLE_STATE_IDLE:       return "IDLE";
        case BLE_STATE_CONNECTED:  return "CONNECTED";
        case BLE_STATE_RECORDING:  return "RECORDING";
        default:                   return "UNKNOWN";
    }
}

void BLE_SM_OnConnected(void) {
    if (g_ble_state == BLE_STATE_IDLE) {
        g_ble_state = BLE_STATE_CONNECTED;
    }
}

void BLE_SM_OnDisconnected(void) {
    g_ble_state = BLE_STATE_IDLE;
}

void BLE_SM_OnRecordingStarted(void) {
    if (g_ble_state == BLE_STATE_CONNECTED) {
        g_ble_state = BLE_STATE_RECORDING;
    }
}

void BLE_SM_OnRecordingStopped(void) {
    if (g_ble_state == BLE_STATE_RECORDING) {
        g_ble_state = BLE_STATE_CONNECTED;
    }
}

void BLE_SM_ProcessLine(const char *line, uint16_t len) {
    if (line == NULL || len == 0) return;

    const char *param = NULL;
    uint16_t param_len = 0;
    BLE_Cmd_t cmd = parse_command(line, len, &param, &param_len);

    switch (cmd) {
        case BLE_CMD_PING:
            BLE_SM_Send("+PONG\r\n");
            break;

        case BLE_CMD_START:
            if (ecg_streaming) {
                BLE_SM_Send("+ERR:ALREADY_RECORDING\r\n");
            } else {
                /* 标记请求开始录音 - 由外部实现 */
                BLE_SM_Send("+OK\r\n");
            }
            break;

        case BLE_CMD_STOP:
            if (!ecg_streaming) {
                BLE_SM_Send("+ERR:NOT_RECORDING\r\n");
            } else {
                BLE_SM_Send("+OK\r\n");
            }
            break;

        case BLE_CMD_STATUS: {
            char buf[128];
            /* 格式: +STATE:rec,<0|1>,st,<STATE> */
            snprintf(buf, sizeof(buf),
                     "+STATE:rec,%d,st,%s\r\n",
                     ecg_streaming ? 1 : 0,
                     BLE_SM_GetStateStr());
            BLE_SM_Send(buf);
            break;
        }

        case BLE_CMD_FNAME: {
            char buf[128];
            extern char g_current_ecg_filename[64];
            snprintf(buf, sizeof(buf), "+FNAME:%s\r\n", g_current_ecg_filename);
            BLE_SM_Send(buf);
            break;
        }

        case BLE_CMD_INFO:
            BLE_SM_Send("+INFO:L496_0405,v0.3,codex2\r\n");
            break;

        case BLE_CMD_SYNC:
            /* SYNC:<timestamp> - 仅确认，时间同步由外部处理 */
            BLE_SM_Send("+OK\r\n");
            break;

        case BLE_CMD_UNKNOWN:
        default:
            BLE_SM_Send("+ERR:UNKNOWN_CMD\r\n");
            break;
    }
}