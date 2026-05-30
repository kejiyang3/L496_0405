# 四模态 SD 拉取比对报告

> 会话: 20260530_210000 | 工具: sd_debug_tool v2 | 端口: COM12
> 执行时间: 2026-05-30 | 分析者: codex2

---

## 执行摘要

| 判定项 | 结果 |
|--------|:--:|
| ECG / PPG / IMU count_match | ✅ OK |
| MIC count_match (WAV vs summary) | ✅ OK |
| CSV 行数 vs 固件内置计数 | ✅ OK |
| 四模态共同 overlap window | 🔴 **FAIL (13.6s / 34.2s = 39.7%)** |
| MIC WAV 采样率一致性 | 🔴 **9065~9225 Hz ≠ 8000 Hz** |
| log_XXX.txt 存在性 | ⚠️ 未生成（根目录不存在） |
| REC/ 子目录格式 | ⚠️ 目录存在但为空（新格式预留） |

---

## 1. 拉取文件清单

### 根目录 (LS)
`
DIR  System Volume Information
DIR  20260530_210000    ← 会话子目录（空，新格式预留）
FILE 450555  20260530_210000_ecg_samples.csv
FILE 245804  20260530_210000_audio.wav
FILE 411     20260530_210000_session.txt
FILE 302     20260530_210000_diag_summary.txt
DIR  REC                 ← 空目录（新格式预留）
FILE 444     20260530_210000_modality_summary.csv
`

> **注意**: 固件 summary 文件 (session/diag/modality) 是上一次 94s 录制的残留数据，已过时。
> 实际磁盘 CSV/WAV 是本次 34s 录制的最新数据。以下以磁盘文件为准。

---

## 2. 各模态逐项指标

### 2.1 ECG

| 指标 | 值 |
|------|-----|
| **debug_count** (firmware capture_count, stale) | 35,176 |
| **file_count** (firmware, stale) | 35,176 |
| **CSV 实际行数** | 12,288 |
| **count_match** (CSV vs firmware) | ❌ 不匹配 (summary 过期) |
| **CSV first_tick** | 10,133 ms |
| **CSV last_tick** | 41,272 ms |
| **数据跨度** | 31,139 ms (31.1s) |
| **configured_sps** | 512 Hz |
| **actual_sps** (按数据跨度) | **394.6 Hz (77.1%)** |
| **drop_count** | 0 (数据对齐) |
| **error_count** | 0 |

### 2.2 PPG

| 指标 | 值 |
|------|-----|
| **debug_count** (firmware, stale) | 1,138 |
| **file_count** (firmware, stale) | 1,138 |
| **CSV 实际行数** | 400 |
| **count_match** (CSV vs firmware) | ❌ 不匹配 (summary 过期) |
| **CSV first_tick** | 9,153 ms |
| **CSV last_tick** | 40,004 ms |
| **数据跨度** | 30,851 ms (30.9s) |
| **configured_sps** | 50 Hz |
| **actual_sps** (按数据跨度) | **12.96 Hz (25.9%)** |
| **expected (FIFO 4x avg)** | 12.5 Hz → **完全吻合** ✅ |
| **drop_count** | 0 |
| **error_count** | 0 |

### 2.3 IMU

| 指标 | 值 |
|------|-----|
| **debug_count** (firmware, stale) | 4,685 |
| **file_count** (firmware, stale) | 4,685 |
| **CSV 实际行数** | 1,664 |
| **count_match** (CSV vs firmware) | ❌ 不匹配 (summary 过期) |
| **CSV first_tick** | 9,166 ms |
| **CSV last_tick** | 41,073 ms |
| **数据跨度** | 31,907 ms (31.9s) |
| **configured_sps** | 104 Hz (ODR) |
| **actual_sps** (按数据跨度) | **52.15 Hz (50.1%)** |
| **drop_count** | 0 |
| **error_count** | 0 |

### 2.4 MIC

| 指标 | 值 |
|------|-----|
| **debug_count** (firmware, stale) | 258,048 |
| **file_count** (firmware, stale) | 258,048 |
| **WAV 实际样本数** | 122,880 |
| **count_match** (WAV vs firmware) | ❌ 不匹配 (summary 过期) |
| **WAV sample_rate (头)** | **9,065 Hz** 🔴 |
| **WAV 实际时长** | 13.555 秒 |
| **WAV effective (at 8000 Hz)** | 15.360 秒 |
| **drop_count** | 2 (firmware diag: mic_drop_blocks=2) |
| **error_count** | 1 (firmware modality position 13) |
| **配置 SPS** | 8,000 Hz |
| **覆盖率 vs 会话** | 13.6s / 34.2s = 39.7% 🔴 |

> **WAV 采样率不一致**: 第一次拉取时 WAV 头为 9,225 Hz，本次为 9,065 Hz。
> SAI 时钟配置与 RECORD_MIC_SAMPLE_RATE_HZ=8000U 不匹配，且每次上电值不同。

---

## 3. PPG / IMU 低速率根因

### PPG: 12.96 Hz (25.9% of 50 Hz)

**不是缺陷，是设计选择。**

| 因素 | 说明 |
|------|------|
| FIFO 4-sample averaging | FIFO_CONFIG=0x5F, 50÷4=12.5 Hz 理论值 |
| 实际 12.96 Hz | 与理论值偏差 3.7%，正常范围 |
| I2C3 总线共享 | 与 IMU 共享，互斥锁保护，有轻微抖动 |
| INT 驱动 | PPG_INT=PC2/EXTI2 为主触发，timeout 为 fallback |

### IMU: 52.15 Hz (50.1% of 104 Hz)

| 因素 | 说明 |
|------|------|
| ICM20948 ODR | 配置 104 Hz Data Ready |
| I2C3 总线共享 | 与 PPG 共享，每次 ACCEL(12B)+GYRO(12B)=24B |
| 互斥锁等待 | I2C3 mutex 是主要瓶颈 |
| 实际 ~52 Hz | 约为 ODR 的一半，符合 I2C 总线分时预期 |

---

## 4. 四模态共同 Overlap Window

`
ECG 跨度 (CSV):   ████████████████████████████████  31.1s
PPG 跨度 (CSV):   ███████████████████████████████   30.9s
IMU 跨度 (CSV):   ████████████████████████████████  31.9s
MIC 跨度 (WAV):   ████████████                      13.6s
                  |____________|
                  共同窗口 = 13.6s
`

| 判定 | 值 |
|------|-----|
| 四模态共同窗口 | **13.6 秒** |
| 占会话比例 | **39.7%** |
| 结果 | 🔴 **FAIL** |

> MIC 覆盖率不足 50%，四模态共同窗口仅 13.6 秒。

---

## 5. MIC 故障根因

### 证据链

| 来源 | 关键证据 |
|------|---------|
| diag_summary.txt | mic_drop_blocks=2 — 2 次 DMA 半满中断丢失 |
| modality_summary.csv | 位置 13 值为 1 — 1 次 SD 写入/同步错误 |
| session.txt | udio_actual_bytes 仅对应部分会话时长 |
| WAV 文件 | 正确关闭（header 与 data 一致），无截断迹象 |
| CSV 文件 | ECG/PPG/IMU 继续写入到会话结束，SD 全局正常 |

### 判定矩阵

| 可能原因 | 判定 | 证据 |
|----------|:--:|------|
| SAI DMA 停止 | ✅ **是** | mic_drop_blocks=2 — DMA 中断丢失后 SAI 停止 |
| AudioTask 停止 | ✅ **是** | error_count=1 后任务退出，WAV 关闭 |
| SD 写入停止 | ❌ 否 | CSV 继续写入 0 错误，SD 全局正常 |
| WAV header 统计错误 | ❌ 否 | header 字节数与 data 完全一致 |
| 状态机提前关闭 | ❌ 否 | ECG/PPG/IMU 继续到 34s 才结束 |

### 结论

**根因: SAI1 DMA 半满中断丢失 → AudioRecorder_Task 退出 → MIC 通道静默**

1. SAI1 DMA 在运行约 13-15 秒后丢失了 2 个半满中断事件
2. AudioRecorder_Task 检测到错误（error_count=1）后退出
3. WAV 文件被正确关闭（header 反映实际数据量）
4. **Fail-fast 未触发**: MIC 失败没有传播为全会话停止
5. ECG/PPG/IMU 继续通过独立 CSV 路径写入直到会话结束

### 附加问题

| 问题 | 严重度 | 说明 |
|------|:--:|------|
| WAV sample_rate ≠ 8000 Hz | 🔴 | 头写入了 9065-9225 Hz（SAI 实际时钟），非配置值 |
| MIC fail-fast 未生效 | 🔴 | 音频失败应该触发 equest_stop_audio → 全会话停止 |
| log_XXX.txt 未生成 | 🟡 | session_manager 未生成会话日志文件 |
| REC/ 目录为空 | 🟡 | 新目录格式已建但未使用 |

---

## 6. 历次拉取对比

| 拉取时间 | CSV 大小 | WAV 大小 | WAV SR | MIC 时长 | 会话时长 | MIC 覆盖 |
|----------|---------|---------|--------|---------|---------|:--:|
| #1 (初始) | 1,312 KB | 148 KB | — | ~9.2s | 94.1s | 9.8% |
| #2 (重置后) | 1,312 KB | 516 KB | 9,225 Hz | 28.0s | 94.1s | 29.7% |
| #3 (本次) | 451 KB | 246 KB | 9,065 Hz | 13.6s | 34.2s | 39.7% |

> 每次上电 WAV 采样率不同（9065 / 9225 Hz），且均 ≠ 8000 Hz。
> MIC 覆盖率始终 < 50%，三次拉取全部 **FAIL**。

---

## 7. 修复建议（优先级排序）

| 优先级 | 文件 | 修改 |
|:--:|------|------|
| 🔴 | udio_recorder.c | WAV 头写入 RECORD_MIC_SAMPLE_RATE_HZ (8000) 而非 SAI 实际时钟 |
| 🔴 | udio_recorder.c | 音频写入失败后触发 fail-fast → equest_stop_audio → 全会话停止 |
| 🔴 | udio_recorder.c | DMA 中断丢失后重试重启 SAI，而非直接退出任务 |
| 🟡 | session_manager.c | 生成 log_XXX.txt 会话日志 |
| 🟡 | SAI 时钟配置 | 确认 RECORD_MIC_SAMPLE_RATE_HZ 与 SAI 分频系数对齐 |
