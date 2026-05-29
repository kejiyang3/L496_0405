# AudioTask 优先级反转 + 热循环重入 — 修复报告

Date: 2026-05-29

## 背景

闭环诊断 (MAX30003_AUDIO_TASK_CONFLICT_DIAG.md) 发现 MIC 开启后 MAX30003 ECG 从 374 Hz 掉到 74 Hz，根因为：

1. **优先级反转**：AudioTask 在录制时被提升到 `osPriorityAboveNormal1`（高于 SensorTask 的 `osPriorityAboveNormal`）
2. **热循环重入**：`sd_file_opened==0` 时 AudioTask 反复进入录制流程，每次执行 `memset(64KB SRAM2)` + `HAL_SAI_DMAStop`
3. **SD 互斥锁长阻塞**：`audio_write_locked` 持有 `Mtx_SDCard` 最长 1000ms

## 修改内容

### P0: RECORD_ENABLE_AUDIO=0 彻底关闭 AudioTask

`Core/Inc/record_feature_flags.h`：正常模式下 `RECORD_DIAG_AUDIO_TASK_CREATE` 改为 `(RECORD_ENABLE_AUDIO)`

当 `RECORD_ENABLE_AUDIO=0` 时：
- 不创建 Task_Audio
- 不进入 AudioRecorder_Task
- 不拉高 EN_MIC
- 不启动 SAI DMA
- 不打开 mic_xxx.wav

### P1: 移除 AudioTask 优先级提升

`Core/Src/freertos.c` line 657：删除 `osThreadSetPriority(Task_AudioHandle, osPriorityAboveNormal1)`

`Core/Src/audio_recorder.c`：删除冗余的 `osThreadSetPriority(..., osPriorityNormal1)`

AudioTask 保持创建时的 `osPriorityNormal1`，低于 SensorTask 的 `osPriorityAboveNormal`。

### P2: 修复 AudioTask 热循环重入

`Core/Src/audio_recorder.c`：添加 session latch

```c
static uint32_t handled_seq = 0xFFFFFFFFU;

// 如果 CSV 未打开，禁止进入
if (g_ecg_rec.sd_file_opened == 0U) { osDelay(20); continue; }

// 同一 file_seq 只能进入一次
if (handled_seq == g_ecg_rec.file_seq) { osDelay(20); continue; }

handled_seq = g_ecg_rec.file_seq;

// state 离开 RECORDING 时重置
if (g_ecg_rec.state != ECG_REC_RECORDING) { handled_seq = 0xFFFFFFFFU; }
```

### P3: 缩短音频 SD 互斥锁超时

`Core/Src/audio_recorder.c`：`audio_write_locked` 中 `osMutexAcquire` 超时从 `pdMS_TO_TICKS(1000)` 改为 `pdMS_TO_TICKS(20)`

超时时丢弃音频块而非阻塞等待。

## 修复结果

| Case | 条件 | 修复前 | 修复后 | 变化 |
|------|------|--------|--------|------|
| **A** | 无 Audio | 374.6 Hz | **374.8 Hz** | 基线不变 |
| **C** | AudioTask 空闲 | 100 Hz | **374.4 Hz** | **+3.7x** ✅ |
| **F** | 完整 MIC | 74.4 Hz | **74.7 Hz** | +0.3 Hz ⚠️ |

### Case A 详细 (374.8 Hz)
```
ecg_sample_count = 11244
diag_task_calls  = 2874
diag_max_gap_ms  = 1010
fifo_eovf_count  = 8
fifo_empty_count = 2866
```

### Case C 详细 (374.4 Hz)
```
ecg_sample_count = 11232
diag_task_calls  = 2860
diag_max_gap_ms  = 1012
fifo_eovf_count  = 8
fifo_empty_count = 2852
```

### Case F 详细 (74.7 Hz)
```
ecg_sample_count = 2241
diag_task_calls  = 583
diag_max_gap_ms  = 1008
fifo_eovf_count  = 25
fifo_empty_count = 558
mic_bytes        = 1146880
```

## 根因判断

| 问题 | 状态 |
|------|------|
| AudioTask 热循环重入 | ✅ 已修复 |
| 优先级反转 | ✅ 已修复 |
| 音频 SD 长阻塞 | ⚠️ 部分修复（mutex 超时 1000→20ms，但 FATFS f_write 本身仍慢） |
| ECG 不到 512 Hz (Case A) | ❌ 未处理（FCLK/INTB 问题，下一阶段） |

## 排除项

- EN_MIC 硬件电源干扰（Case B: 374.4 Hz，无影响）
- SAI DMA 中断干扰（Case D: 393 Hz，无影响）
- MIC RAM 共享缓冲冲突（Case E: 376 Hz，无影响）
- EINT gating bug（已修复，始终读取 FIFO）

## 下一步

1. Case F SD 写入瓶颈：分析 `f_write` 耗时，考虑异步双缓冲
2. Case A 375→512 Hz：查 MAX30003 INTB 通知机制（99% timeout）
3. 将 MIC 和 ECG SD 写入分离到不同优先级任务

## 提交

- Commit: `87defa0`
- 仓库: https://github.com/kejiyang3/L496_0405
- 固件: 已恢复 `RECORD_DIAG_CASE=0`（正常模式）
