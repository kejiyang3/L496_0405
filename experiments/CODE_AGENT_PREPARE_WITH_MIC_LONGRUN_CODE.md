# 代码 Agent 任务：补齐 with-MIC 长测所需代码，不执行实验

## 0. 给代码 Agent 的强制目标

请读取并严格执行 `CODE_AGENT_PREPARE_WITH_MIC_LONGRUN_CODE.md`。

本任务只负责写代码、编译、提交并推送 GitHub。  
不要执行 1h 长测。  
不要拉 SD 卡数据。  
不要再写长篇实验分析。  
不要根据旧数据判断 PASS。  
验证工作由另一个 agent / 另一台设备完成。

当前事实：

```text
所谓 1h with-MIC 长测从未真实执行。
SD 卡上只有约 94s session。
没有 3600s CSV。
没有 3600s WAV。
之前 “10min=13MB，1h=35.8MB” 是推测或误报。
A1/A2/A3 实际是 audio=0 的 no-MIC baseline。
sd_smoke_test 才是 audio=1，但只有 124s，且 WAV/CSV 状态不完整。
```

本任务的代码目标：

```text
1. 修复 session 唯一命名，避免 RTC 固定导致同名覆盖；
2. 修复 / 明确 MIC WAV 文件路径，必须落到当前 session 目录；
3. 补齐 MIC 文件 open/write/close 诊断；
4. 补齐 with-MIC 2min smoke / 1h / no-MIC baseline 等实验配置开关；
5. 保持 v0.3 三主模态稳定配置；
6. 保持 MIC 为低优先级附属模态；
7. 编译通过；
8. 提交并推送 GitHub；
9. 给出清晰的 commit hash 和修改文件列表。
```

---

## 1. 必须保持的 v0.3 主配置

不得破坏以下配置：

```text
PPG = 50Hz + FIFO avg1
PPG FIFO_CONFIG readback = 0x1F
ICM ACCEL_SMPLRT_DIV = 10
ICM GYRO_SMPLRT_DIV = 10
ECG MAX30003 当前配置
LVGL OFF
RECORD_AUDIO_ERROR_REQUESTS_STOP = 0
audio_open_file mutex timeout = 20ms
MIC 为低优先级附属模态
```

MIC 上电引脚已经在 `main.h` 配置：

```c
#define EN_MIC_Pin GPIO_PIN_12
#define EN_MIC_GPIO_Port GPIOB
```

必须保留 MIC 上电控制：

```text
MIC 实验前 EN_MIC ON；
等待 50~200ms；
启动 SAI DMA / AudioTask；
session 结束 / MIC error / stop 时 EN_MIC OFF。
```

---

## 2. 本任务不做什么

禁止做以下事情：

```text
1. 不跑 1h；
2. 不跑 8h；
3. 不根据旧 SD 数据写 PASS；
4. 不继续分析旧的 94s session；
5. 不改 PPG 回 avg4；
6. 不改 ICM 回 div21；
7. 不重新启用 MIC fail-fast 停止整个 session；
8. 不启用 stop-only-fsync；
9. 不引入大规模 buffer/batch 改动；
10. 不让 MIC 抢占 ECG/PPG/ICM。
```

---

## 3. 必须实现：session 唯一命名

当前 RTC 固定：

```text
2026-05-30 21:00
```

导致所有 session 可能变成：

```text
20260530_210000
```

这会造成旧数据覆盖、混杂和错误判断。

请实现唯一 session id。可以任选一种可靠方案：

### 推荐方案 A：基础时间 + tick

```text
/REC/20260530_210000_T<start_tick>/
```

示例：

```text
/REC/20260530_210000_T00123456/
```

### 推荐方案 B：基础时间 + run counter

```text
/REC/20260530_210000_R001/
```

如果使用 run counter，必须扫描现有目录并自动递增，避免覆盖。

### 推荐方案 C：手动实验 ID 宏

提供宏：

```c
#define RECORD_SESSION_TAG "V03_MIC_1H_RERUN_D2_001"
```

目录：

```text
/REC/V03_MIC_1H_RERUN_D2_001/
```

最低要求：

```text
每次开始录制都不能覆盖旧 session。
```

---

## 4. 必须实现：所有文件写入当前 session 目录

当前发现存在：

```text
root audio.wav
REC/audio.wav
session 内没有 audio.wav
```

这是不可接受的。

必须统一为：

```text
/REC/<session_id>/ecg_samples.csv
/REC/<session_id>/audio.wav
/REC/<session_id>/session.txt
/REC/<session_id>/modality_summary.csv
/REC/<session_id>/diag_summary.txt
```

如果当前工程仍使用根目录文件，也必须迁移到 session 目录，或者至少在 summary 中明确写出实际路径。

必须禁止同一轮实验出现：

```text
根目录 audio.wav
REC/audio.wav
session 内无 audio.wav
```

---

## 5. 必须实现：MIC WAV 文件链路诊断

在 session / diag / modality summary 中输出以下字段。

### MIC 文件字段

```text
mic_enabled
mic_mode
mic_wav_expected
mic_wav_path
mic_file_open_attempted
mic_file_open_ok
mic_file_open_result
mic_file_close_attempted
mic_file_close_ok
mic_file_close_result
mic_file_bytes
mic_wav_data_bytes
mic_wav_sample_rate
mic_wav_header_finalized
mic_last_fresult
mic_last_error
```

### MIC 采集字段

```text
mic_audio_task_started
mic_sai_dma_started
mic_dma_half_count
mic_dma_full_count
mic_last_dma_tick
mic_last_write_tick
mic_blocks_in
mic_blocks_written
mic_blocks_dropped
mic_blocks_write_failed
mic_drop_count
mic_stall_count
mic_effective_duration
```

### MIC 电源字段

```text
mic_power_pin=GPIOB12
mic_power_active_level=HIGH 或 LOW
mic_power_on_count
mic_power_off_count
mic_power_on_tick
mic_power_off_tick
mic_power_state_at_start
mic_power_state_at_stop
```

如果 WAV 不存在，必须明确输出：

```text
MIC_STATUS=MIC_NO_WAV
```

不能只写：

```text
MIC WARN
```

---

## 6. 必须实现：实验配置开关

请提供清晰的编译期或运行期实验开关，方便验证 agent 直接选择。

最低需要以下模式：

```text
EXP_CORE_BASELINE_90S
EXP_WITH_MIC_SMOKE_2MIN
EXP_WITH_MIC_1H
EXP_MIC_ONLY_WAV_2MIN
EXP_CORE_MIC_DMA_ONLY_2MIN
EXP_CORE_MIC_WAV_2MIN
```

建议定义在类似：

```text
Core/Inc/record_experiment_config.h
```

或工程已有配置文件中。

每个模式必须明确设置：

### EXP_CORE_BASELINE_90S

```text
duration = 90s
ECG ON
PPG ON
ICM ON
MIC OFF
EN_MIC OFF
```

### EXP_WITH_MIC_SMOKE_2MIN

```text
duration = 120s
ECG ON
PPG ON
ICM ON
MIC ON
WAV ON
EN_MIC ON
```

### EXP_WITH_MIC_1H

```text
duration = 3600s
ECG ON
PPG ON
ICM ON
MIC ON
WAV ON
EN_MIC ON
session tag = V03_MIC_1H_RERUN 或唯一自动生成
```

### EXP_MIC_ONLY_WAV_2MIN

```text
duration = 120s
ECG OFF
PPG OFF
ICM OFF
MIC ON
WAV ON
EN_MIC ON
```

### EXP_CORE_MIC_DMA_ONLY_2MIN

```text
duration = 120s
ECG ON
PPG ON
ICM ON
MIC DMA ON
WAV OFF
EN_MIC ON
```

### EXP_CORE_MIC_WAV_2MIN

```text
duration = 120s
ECG ON
PPG ON
ICM ON
MIC ON
WAV ON
EN_MIC ON
```

---

## 7. 必须实现：summary 中写入实验模式

每次 session 必须输出：

```text
experiment_mode=
session_id=
session_dir=
requested_duration_ms=
actual_duration_ms=
audio_enabled=
wav_enabled=
core_enabled=
```

这样验证 agent 可以一眼判断：

```text
这到底是不是 1h with-MIC。
```

---

## 8. 必须实现：WAV header 正确性

WAV header 必须固定：

```text
sample_rate = 8000
channels = 1
bits_per_sample = 16
```

停止时必须更新：

```text
RIFF chunk size
data chunk size
```

如果 close / header update 失败，必须记录：

```text
mic_wav_header_finalized=0
mic_file_close_result=<FRESULT>
MIC_STATUS=MIC_WAV_HEADER_FAIL
```

不能再出现：

```text
header sample_rate = 9065Hz
```

---

## 9. 必须实现：sd_debug_tool 能递归拉取 session

如果当前 PC 拉取工具不能递归拉取 `/REC/<session_id>/`，请修复或新增命令。

最低需要：

```bash
python pc/usb_sd_pull.py list-recursive /
python pc/usb_sd_pull.py pull-session /REC/<session_id> -o <local_dir>
python pc/usb_sd_pull.py get-all -o <local_dir>
```

必须能拉取：

```text
session.txt
modality_summary.csv
diag_summary.txt
ecg_samples.csv
audio.wav
```

如果工具暂时不好改，至少新增一个简单 Python 脚本：

```text
tools/pull_session_recursive.py
```

---

## 10. 必须实现：PC 验证脚本

新增或更新：

```text
tools/verify_session_integrity.py
```

输入：

```text
本地拉取的 session 目录
```

输出：

```text
session_id
requested_duration_ms
actual_duration_ms
csv_first_tick
csv_last_tick
csv_duration_ms
ecg_file_count
ppg_file_count
icm_file_count
ecg_actual_sps
ppg_actual_sps
icm_actual_sps
wav_exists
wav_size
wav_sample_rate
wav_data_chunk_size
wav_duration_s
mic_status
core_pass_candidate
mic_status_candidate
```

这个脚本不需要替代人工验证，但必须能快速发现：

```text
没有 1h session
没有 WAV
CSV 只有 94s
WAV header 不对
count 不闭合
```

---

## 11. 编译与提交要求

代码完成后必须执行：

```text
1. clean build；
2. 确认 0 errors；
3. 尽量消除 warnings，至少不得新增关键 warning；
4. git diff 检查；
5. git status 确认；
6. commit；
7. push 到 GitHub。
```

建议 commit 名：

```text
prepare-with-mic-longrun-session-wav-integrity
```

提交信息必须包含：

```text
- unique session directory
- session-local audio.wav
- MIC WAV diagnostics
- experiment mode switches
- recursive session pull / verify script
```

---

## 12. 最终交付物

请提交给用户：

```text
1. commit hash
2. push branch
3. 修改文件列表
4. 新增实验模式列表
5. session 命名规则
6. WAV 路径规则
7. 新增 summary 字段列表
8. 新增 / 修改 PC 工具说明
9. build 结果
10. 下一位验证 agent 应读取哪个 MD 或如何选择实验模式
```

---

## 13. 给代码 Agent 的简短目标

请读取并严格执行 `CODE_AGENT_PREPARE_WITH_MIC_LONGRUN_CODE.md`。你的任务只写代码、编译、提交并推送 GitHub，不执行长测、不拉 SD、不根据旧数据判断 PASS。当前必须为后续验证 agent 准备好 with-MIC 真实长测所需代码：修复 RTC 固定导致的 session 同名覆盖，保证每次录制使用唯一 `/REC/<session_id>/` 目录；所有文件必须写入当前 session 目录，包括 `ecg_samples.csv`、`audio.wav`、`session.txt`、`modality_summary.csv`、`diag_summary.txt`；补齐 MIC WAV 链路诊断字段，包括 mic_wav_path、mic_file_open_attempted、mic_file_open_ok、mic_file_open_result、mic_file_bytes、mic_wav_data_bytes、mic_blocks_in、mic_blocks_written、mic_blocks_dropped、mic_last_error、mic_dma_half/full_count；提供清晰实验模式：core baseline 90s、with-MIC smoke 2min、with-MIC 1h、MIC-only WAV 2min、CORE+MIC DMA-only 2min、CORE+MIC WAV 2min；修复 WAV header，确保 sample_rate=8000，stop 时更新 data chunk；修复或新增递归拉取 session 的 PC 工具和 `tools/verify_session_integrity.py`。必须保持 v0.3 配置：PPG=50Hz avg1、ICM div10、ECG 当前配置、LVGL OFF、MIC 低优先级附属、RECORD_AUDIO_ERROR_REQUESTS_STOP=0。完成后 clean build，提交并 push GitHub，返回 commit hash、修改文件列表和 build 结果。
