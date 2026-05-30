#ifndef __SD_DEBUG_LOG_H__
#define __SD_DEBUG_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void SD_DebugLog_Init(void);
void SD_DebugLog_StartNewFile(uint32_t seq);
const char *SD_DebugLog_GetPath(void);
void SD_DebugLog_WriteLine(const char *line);
void SD_DebugLog_WriteEvent(const char *tag, uint32_t value);
void SD_DebugLog_WriteSnapshot(void);
void SD_DebugLog_WriteSessionSummary(void);

void DebugLogWriter_Task(void *argument);
void SD_DebugLog_RequestStopFlush(void);
uint8_t SD_DebugLog_IsFlushComplete(void);

#ifdef __cplusplus
}
#endif

#endif /* __SD_DEBUG_LOG_H__ */
