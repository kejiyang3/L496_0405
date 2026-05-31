# 设备 1 任务：with-MIC 一小时长测验证

## 0. 给 Agent 1 的强制目标

请读取并严格执行 `DEVICE1_WITH_MIC_1H_LONGRUN.md`。

当前设备 1 已经证明：

```text
v0.3 三主模态稳定版 PASS；
MIC 已成功受控重新接入；
MIC WAV 写入不会拖慢 ECG / PPG / ICM；
CORE 三主模态在 A/M0/M1/M2/M3 中全部 PASS；
MIC 每轮稳定掉 2 个 block，但不影响核心三主模态。
```

本任务不再定位 “MIC 每轮掉 2 个 block” 的细节。  
当前策略是：

```text
MIC 掉 2 个 block 可以接受；
只要 MIC 不影响 ECG / PPG / ICM，CORE 就继续 PASS；
MIC 单独标记 WARN。
```

本阶段目标：

> 在设备 1 上执行 ECG + PPG + ICM + MIC 的 1 小时长测，验证 MIC 作为附属模态开启时，核心三主模态是否仍然长期稳定。

---

## 1. 当前固定配置

必须保持以下配置：

```text
PPG: 50Hz, FIFO avg1, FIFO_CONFIG=0x1F
ICM: ACCEL_SMPLRT_DIV=10, GYRO_SMPLRT_DIV=10
ECG: MAX30003 当前配置
MIC: ON, low-priority auxiliary modality
EN_MIC: GPIOB12 controlled
LVGL: OFF
RECORD_AUDIO_ERROR_REQUESTS_STOP = 0
audio_open_file mutex timeout = 20ms
```

MIC 上电控制：

```c
#define EN_MIC_Pin GPIO_PIN_12
#define EN_MIC_GPIO_Port GPIOB
```

MIC 启动必须：

```text
MIC_PowerOn
等待 50~200ms
启动 SAI DMA / AudioTask
```

MIC 停止必须：

```text
停止 AudioTask / SAI DMA
close WAV
MIC_PowerOff
```

---

## 2. 本任务不做什么

本任务不再做：

```text
1. 不再分析 MIC 掉 2 block 的详细原因；
2. 不再做 20/30/50ms mutex timeout 对照；
3. 不再做 stop flush 专项；
4. 不再修改 PPG/ICM 采样配置；
5. 不再追求 MIC 0 drop；
6. 不再让 MIC 错误停止整个 session。
```

---

## 3. 一票否决条件

以下任一出现，本轮 FAIL：

```text
1. ECG / PPG / ICM 任一 count_match 失败；
2. PPG 不在 48~52Hz；
3. ICM 不在 100~106Hz；
4. ECG 低于 480Hz 或相比 90s/5min with-MIC baseline 下降超过 3%；
5. i2c_errors > 0；
6. queue_fail > 0；
7. sd_write_errors > 0；
8. HardFault；
9. FR_INVALID_OBJECT；
10. MIC 导致主三模态停止；
11. session 结束后 EN_MIC 未关闭；
12. CSV / WAV 文件无法拉取或无法解析。
```

MIC 单独 drop / incomplete 不直接导致 CORE FAIL，只标记：

```text
MIC WARN
```

---

## 4. 实验前检查

长测前必须确认：

```text
1. SD 卡剩余空间足够；
2. session 目录唯一，不与旧数据混淆；
3. RTC 无电池导致同名目录问题已处理；
4. sd_debug_tool 能拉取较大 CSV / WAV；
5. 电源稳定；
6. EN_MIC 初始为 OFF；
7. LVGL 为 OFF；
8. MIC 上电控制有效；
9. v0.3 配置 readback 正确。
```

必须记录：

```text
SD free space
预计 CSV size per hour
预计 WAV size per hour
session path
firmware commit
```

---

## 5. 实验 A：with-MIC 10 分钟预跑

### 设置

```text
ECG ON
PPG ON, 50Hz avg1
ICM ON, div10
MIC ON, low-priority WAV
duration = 600s
```

### 目的

在 1h 前先确认 with-MIC 10min 没有明显问题。

### PASS 标准

```text
ECG 480~500Hz
PPG 48~52Hz
ICM 100~106Hz
count_match
i2c_errors=0
queue_fail=0
sd_write_errors=0
MIC status = OK 或 WARN
EN_MIC stop 后 OFF
```

如果 10min 不通过，禁止进入 1h。

---

## 6. 实验 B：with-MIC 1 小时长测

### 设置

```text
ECG ON
PPG ON, 50Hz avg1
ICM ON, div10
MIC ON, low-priority WAV
duration = 3600s
```

### 目标

验证：

```text
1. CORE 三主模态 1h 不衰减；
2. MIC 作为附属模态不会拖垮核心；
3. SD 写入和文件系统 1h 内稳定；
4. MIC drop 可记录但不影响 CORE；
5. 结束后 CSV / WAV 可拉取并解析。
```

### 必须记录

CORE：

```text
ecg_file_count
ecg_actual_sps
ecg_count_match

ppg_file_count
ppg_actual_sps
ppg_count_match

icm_file_count
icm_actual_sps
icm_count_match

i2c_errors
queue_fail
sd_write_errors
```

MIC：

```text
mic_enabled
mic_status
mic_effective_duration
mic_blocks_in
mic_blocks_written
mic_blocks_dropped
mic_drop_count
mic_stall_count
mic_file_bytes
mic_sd_mutex_timeout
mic_last_error
mic_power_on_count
mic_power_off_count
mic_power_state_at_stop
```

SD / session：

```text
session_duration
csv_file_size
wav_file_size
sd_free_before
sd_free_after
pull_success
parse_success
```

---

## 7. 判定标准

### CORE PASS

```text
ECG 480~500Hz
PPG 48~52Hz
ICM 100~106Hz
三主模态 count_match
i2c_errors=0
queue_fail=0
sd_write_errors=0
CSV 可解析
```

### MIC OK

```text
MIC 文件完整
MIC drop 不明显
MIC effective_duration 接近 session duration
```

### MIC WARN

```text
MIC 每轮稳定 drop 少量 block
MIC incomplete
MIC stall 但未影响 CORE
MIC effective_duration 不完整但有记录
```

### CORE FAIL

```text
MIC 影响三主模态
主三模态 count 不闭合
采样率明显下降
SD / queue / I2C 出错
系统崩溃
```

最终允许的好结果：

```text
CORE PASS + MIC OK
CORE PASS + MIC WARN
```

不接受：

```text
CORE FAIL
```

---

## 8. 输出报告格式

最终报告必须包含：

```text
# DEVICE1 with-MIC 1h longrun report

## 1. 固件与配置
commit:
session:
duration:
PPG config:
ICM config:
MIC power pin:
MIC policy:

## 2. 10min 预跑结果
ECG:
PPG:
ICM:
MIC:
errors:
判定:

## 3. 1h 长测结果
ECG:
PPG:
ICM:
count_match:
i2c_errors:
queue_fail:
sd_write_errors:

## 4. MIC 状态
effective_duration:
blocks_in:
blocks_written:
blocks_dropped:
drop_count:
stall_count:
status:

## 5. 文件状态
CSV size:
WAV size:
pull:
parse:
SD free before/after:

## 6. 最终结论
CORE PASS / CORE FAIL
MIC OK / MIC WARN / MIC FAIL

## 7. 下一步建议
是否允许进入 with-MIC 30min/1h/8h 更长测试：
是否建议保留 MIC 默认开启：
是否建议继续作为附属模态：
```

---

## 9. 给 Agent 1 的简短目标

请读取并严格执行 `DEVICE1_WITH_MIC_1H_LONGRUN.md`。当前不要再定位 MIC 每轮掉 2 个 block 的细节，损失就先接受；只要 MIC 不影响 ECG/PPG/ICM，CORE 就判定 PASS，MIC 单独标记 WARN。请保持 v0.3 配置：PPG=50Hz avg1、ICM accel/gyro div10、ECG 当前配置、LVGL OFF、MIC 低优先级附属开启，并控制 `EN_MIC_Pin=GPIO_PIN_12` / `EN_MIC_GPIO_Port=GPIOB` 上电和断电。先跑 with-MIC 10min 预跑，PASS 后执行 with-MIC 1h 长测。PASS 标准：ECG 480~500Hz、PPG 48~52Hz、ICM 100~106Hz、三主模态 count_match、i2c_errors=0、queue_fail=0、sd_write_errors=0、CSV/WAV 可拉取解析、session 结束后 EN_MIC=OFF。MIC drop/incomplete 只记为 MIC WARN，不允许影响 CORE。
