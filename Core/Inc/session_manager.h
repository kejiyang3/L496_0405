#ifndef __SESSION_MANAGER_H__
#define __SESSION_MANAGER_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SESSION_DIR_PREFIX  "/REC"
#define SESSION_ID_LEN      16
#define SESSION_DIR_LEN     32
#define SESSION_DT_LEN      20

#define SESSION_DEFAULT_YEAR   2026
#define SESSION_DEFAULT_MONTH  5
#define SESSION_DEFAULT_DAY    30
#define SESSION_DEFAULT_HOUR   21
#define SESSION_DEFAULT_MIN    0
#define SESSION_DEFAULT_SEC    0

typedef struct {
    char     session_id[SESSION_ID_LEN];
    char     session_dir[SESSION_DIR_LEN];
    char     dt_start[SESSION_DT_LEN];
    char     dt_end[SESSION_DT_LEN];
    uint32_t tick_start_ms;
    uint32_t tick_end_ms;
    uint32_t duration_ms;
    uint8_t  created;
} SessionInfo_t;

extern SessionInfo_t g_session;

int  Session_Create(uint32_t tick_now);
int  Session_WriteMeta(void);
int  Session_WriteDiagSummary(void);

#ifdef __cplusplus
}
#endif
#endif
