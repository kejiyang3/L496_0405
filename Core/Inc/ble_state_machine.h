/*
 * ble_state_machine.h - BLE命令解析与状态机
 *
 * 协议: 基于行的文本命令协议 (AT风格)
 *   命令格式: <CMD>[:<参数>]\r\n
 *   响应格式: +<TYPE>[:<数据>]\r\n
 *   PING -> +PONG
 *   STATUS -> +STATE:rec,0,bat,85
 */

#ifndef BLE_STATE_MACHINE_H
#define BLE_STATE_MACHINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BLE连接状态 */
typedef enum {
    BLE_STATE_IDLE       = 0,  /* 空闲/未连接 */
    BLE_STATE_CONNECTED  = 1,  /* 已连接，等待命令 */
    BLE_STATE_RECORDING  = 2   /* 录音中 */
} BLE_State_t;

/* 命令解析结果 */
typedef enum {
    BLE_CMD_NONE      = 0,
    BLE_CMD_PING      = 1,   /* 心跳 */
    BLE_CMD_START     = 2,   /* 开始录音 */
    BLE_CMD_STOP      = 3,   /* 停止录音 */
    BLE_CMD_STATUS    = 4,   /* 查询状态 */
    BLE_CMD_FNAME     = 5,   /* 查询当前文件名 */
    BLE_CMD_INFO      = 6,   /* 查询设备信息 */
    BLE_CMD_SYNC      = 7,   /* 时间同步 */
    BLE_CMD_UNKNOWN   = 0xFF /* 未知命令 */
} BLE_Cmd_t;

/* 初始化状态机 */
void BLE_SM_Init(void);

/* 处理一行命令 (不含换行符) */
void BLE_SM_ProcessLine(const char *line, uint16_t len);

/* 获取当前状态 */
BLE_State_t BLE_SM_GetState(void);

/* 获取状态描述字符串 */
const char *BLE_SM_GetStateStr(void);

/* 通知BLE连接状态变化 (由外部事件驱动) */
void BLE_SM_OnConnected(void);
void BLE_SM_OnDisconnected(void);

/* 通知录音状态变化 */
void BLE_SM_OnRecordingStarted(void);
void BLE_SM_OnRecordingStopped(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_STATE_MACHINE_H */