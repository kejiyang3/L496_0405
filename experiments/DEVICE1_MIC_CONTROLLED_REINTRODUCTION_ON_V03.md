# 设备 1 任务：在 v0.3 三主模态稳定版上受控重新接入 MIC

## 0. 给 Agent 1 的强制目标

请读取并严格执行 `DEVICE1_MIC_CONTROLLED_REINTRODUCTION_ON_V03.md`。

注意：设备 1 不适合长时间测试，因此设备 1 不再负责 1h / 8h 长测。  
设备 1 当前任务改为：

> 在已经验证通过的 v0.3 三主模态稳定配置上，受控、分阶段、低优先级地重新接入 MIC，并验证 MIC 不会破坏 ECG/PPG/ICM 三主模态。

当前 v0.3 三主模态稳定配置为：

```text
MIC OFF
PPG = 50Hz + FIFO avg1
PPG FIFO_CONFIG = 0x1F
ICM ACCEL_SMPLRT_DIV = 10
ICM GYRO_SMPLRT_DIV = 10
ECG 当前 MAX30003 配置
LVGL OFF
```

已知稳定结果：

```text
PPG ≈ 50.14~50.43Hz
IMU ≈ 104.23~104.25Hz
ECG ≈ 488.56~489.00Hz
i2c_err = 0
queue_fail = 0
sd_write_errors = 0
count_match 一致
30min no-MIC 已 PASS
```

设备 1 本任务不做长测。  
设备 1 本任务只做 90s / 5min 级别的 MIC 受控接入验证。

---

## 1. 本任务核心原则

MIC 现在只能作为附属模态。

```text
ECG / PPG / ICM 是主系统；
MIC 是低优先级附属；
MIC 可以 drop；
MIC 可以 incomplete；
MIC 可以 fail；
但 MIC 不允许拖垮 ECG/PPG/ICM。
```

主系统 PASS/FAIL 只看：

```text
ECG
PPG
ICM
```

MIC 单独标记：

```text
MIC_OK
MIC_WARN
MIC_INCOMPLETE
MIC_STALLED
MIC_DISABLED
MIC_ERROR
```

---

## 2. 必须先确认 v0.3 配置

在接入 MIC 前，设备 1 必须先确认当前代码已经是 v0.3 配置：

```text
PPG FIFO_CONFIG = 0x1F
ICM accel_div = 10
ICM gyro_div = 10
MIC_ENABLED = 0 for baseline
LVGL OFF
```

如果不是，必须先合入设备 2 或主线提供的 v0.3 配置 patch/commit。

---

## 3. MIC 上电引脚硬要求

当前 `main.h` 已配置 MIC 电源控制引脚：

```c
#define EN_MIC_Pin GPIO_PIN_12
#define EN_MIC_GPIO_Port GPIOB
```

设备 1 接入 MIC 时，必须显式控制该引脚。

### 3.1 no-MIC baseline

```text
EN_MIC 必须 OFF
mic_power_state_at_start = OFF
mic_power_state_at_stop = OFF
```

### 3.2 MIC 实验启动顺序

```text
1. MIC_PowerOn();
2. 等待 50~200ms 稳定；
3. 清零 MIC 计数器；
4. 如需写音频，打开 WAV/RAW 文件；
5. 启动 SAI DMA；
6. 启动或通知 AudioTask；
7. 记录 mic_power_on_tick；
8. 进入采集。
```

建议：

```c
HAL_GPIO_WritePin(EN_MIC_GPIO_Port, EN_MIC_Pin, GPIO_PIN_SET);
osDelay(100);
```

如果硬件为低电平使能，必须根据原理图改为反逻辑，并在 summary 中记录。

### 3.3 MIC 停止顺序

```text
1. 停止 AudioTask 录制状态；
2. 停止 SAI DMA；
3. flush/close MIC 文件，如启用；
4. 记录 MIC 状态；
5. MIC_PowerOff();
6. 记录 mic_power_off_tick。
```

即使 MIC error / stall，也必须执行 MIC_PowerOff。

---

## 4. MIC 写 SD 规则

MIC 写 SD 必须低优先级、可丢弃、不可阻塞。

```text
1. MIC 写 SD 使用短 mutex timeout，例如 5~20ms；
2. 获取不到 SD mutex 就 drop 当前 audio block；
3. 不允许 osWaitForever；
4. 不允许 MIC 频繁 f_sync；
5. 不允许 MIC long write 阻塞主三模态 writer；
6. MIC close/header 修正失败只标记 MIC_ERROR，不影响 ECG/PPG/ICM 文件关闭；
7. MIC stall 只记录，不停止三主模态主 session。
```

---

## 5. 实验矩阵

## 实验 A：no-MIC baseline 90s ×3

### 设置

```text
ECG ON
PPG ON, 50Hz avg1
ICM ON, div10
MIC OFF
EN_MIC OFF
LVGL OFF
duration = 90s
```

### 执行

```text
A1
A2
A3
```

### PASS 标准

```text
PPG 48~52Hz
IMU 100~106Hz
ECG 480~500Hz
count_match
i2c_errors = 0
queue_fail = 0
sd_write_errors = 0
EN_MIC remains OFF
```

如果 A 不通过，禁止进入 MIC 接入。

---

## 实验 M0：MIC DMA only，不写 WAV，90s

### 设置

```text
ECG ON
PPG ON
ICM ON
MIC_PowerOn
等待 100ms
SAI DMA ON
不写 WAV
只统计 DMA half/full、blocks_in、stall/drop
duration = 90s
```

### 目的

确认 MIC 上电 + SAI DMA 是否影响主三模态。

### PASS 标准

```text
主三模态 PASS
MIC 若 stall，只记录，不停止主三模态
mic_power_on_count = 1
mic_power_off_count = 1
session 结束后 EN_MIC OFF
```

---

## 实验 M1：MIC WAV low-priority，90s

### 设置

```text
ECG ON
PPG ON
ICM ON
MIC ON
EN_MIC ON
AudioTask 低优先级
WAV 写入开启
MIC SD mutex timeout 短
duration = 90s
```

### PASS 标准

主系统：

```text
ECG/PPG/ICM count_match
PPG 48~52Hz
IMU 100~106Hz
ECG 不低于 no-MIC baseline 超过 3%
i2c_errors = 0
queue_fail = 0
sd_write_errors = 0
```

MIC：

```text
可以 OK / WARN / INCOMPLETE
必须记录 drop/stall/effective_duration
不得影响主系统
```

---

## 实验 M2：MIC WAV low-priority，90s ×3

只有 M1 主系统 PASS 后执行。

```text
M2-1
M2-2
M2-3
```

PASS 标准同 M1。

---

## 实验 M3：MIC WAV low-priority，5min ×1

只有 M2 主系统 PASS 后执行。

```text
duration = 300s
```

PASS 标准：

```text
主三模态 PASS
MIC 状态可为 OK/WARN/INCOMPLETE
MIC 不影响主三模态
```

---

## 6. 必须新增 / 确认的 summary 字段

主三模态：

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
mic_power_pin=GPIOB12
mic_power_active_level
mic_power_on_count
mic_power_off_count
mic_power_on_tick
mic_power_off_tick
mic_power_state_at_start
mic_power_state_at_stop

mic_dma_half_count
mic_dma_full_count
mic_blocks_in
mic_blocks_written
mic_blocks_dropped
mic_stall_count
mic_effective_duration
mic_file_bytes
mic_sd_mutex_timeout
mic_last_error
```

---

## 7. PASS / WARN / FAIL

### CORE PASS

```text
ECG/PPG/ICM count_match
PPG 48~52Hz
ICM 100~106Hz
ECG 不低于 no-MIC baseline 超过 3%
i2c_errors = 0
queue_fail = 0
sd_write_errors = 0
```

### MIC WARN

```text
MIC incomplete
MIC drop
MIC stall
MIC duration 不足
```

这些只影响 MIC 状态，不影响 CORE PASS。

### CORE FAIL

```text
MIC 导致 ECG/PPG/ICM 任一 count 不闭合
MIC 导致主三模态采样率明显劣化
MIC 导致 sd_write_errors
MIC 导致 queue_fail
MIC 导致 HardFault / FR_INVALID_OBJECT
MIC 未断电
MIC 未上电却判断故障
```

---

## 8. 最终提交物

请提交：

```text
1. 当前 commit
2. v0.3 配置 readback
3. A1/A2/A3 no-MIC baseline 结果
4. M0 MIC DMA only 结果
5. M1 MIC WAV 90s 结果
6. M2 MIC WAV 90s×3 结果
7. M3 MIC WAV 5min 结果，如执行
8. 主三模态 no-MIC vs MIC 对比表
9. MIC 状态表
10. 是否允许进入后续 30min with-MIC 附属测试
```

最终判断格式：

```text
最终判断：

CORE:
  ECG:
  PPG:
  ICM:
  count_match:
  errors:
  status:

MIC:
  power_control:
  status:
  effective_duration:
  drops:
  stalls:
  是否影响 CORE:

结论：
CORE PASS / CORE FAIL
MIC OK / MIC WARN / MIC FAIL
```

---

## 9. 给 Agent 1 的简短目标

请读取并严格执行 `DEVICE1_MIC_CONTROLLED_REINTRODUCTION_ON_V03.md`。设备 1 不适合长时间测试，所以设备 1 现在不做 1h/8h 长测，而是负责在 v0.3 三主模态稳定版上受控重新接入 MIC。请先确认 v0.3 配置：MIC OFF、PPG=50Hz avg1、ICM accel/gyro div10、ECG 当前配置、LVGL OFF。先跑 no-MIC baseline 90s×3，确认 PPG 48~52Hz、ICM 100~106Hz、ECG 480~500Hz、count_match、i2c_errors=0、queue_fail=0、sd_write_errors=0。只有 baseline 通过后才允许接入 MIC。MIC 接入时必须控制 `main.h` 中的 `EN_MIC_Pin=GPIO_PIN_12`、`EN_MIC_GPIO_Port=GPIOB`：no-MIC 时 EN_MIC OFF，MIC 实验前 MIC_PowerOn 并等待 50~200ms，结束/失败/session stop 时 MIC_PowerOff。MIC 必须是低优先级附属模态，允许 drop/incomplete/stall，但不得停止或拖慢 ECG/PPG/ICM。最终主系统 PASS/FAIL 只看 ECG/PPG/ICM，MIC 单独标记 OK/WARN/FAIL。
