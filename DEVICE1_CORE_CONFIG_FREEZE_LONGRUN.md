# 设备 1 任务：三主模态目标配置冻结与长测前验证

## 0. 给 Agent 1 的强制目标

请基于当前 GitHub 工程 `kejiyang3/L496_0405`，执行 **三主模态目标配置冻结与长测前验证**。

设备 1 已经证明目标配置可行：

```text
PPG: 50Hz + FIFO avg1 → 90s: 50.42Hz, 5min: 50.20Hz
ICM: div10 → 90s: 104.25Hz, 5min: 104.24Hz
ECG: no-MIC → 90s: 488.80Hz, 5min: 488.60Hz
I2C errors = 0
queue_fail = 0
sd_write_errors = 0
```

本任务目标是把这套配置固化成正式主线配置，并完成 30min 长测前的版本冻结验证。

本任务不再重复：

```text
PPG-only avg4 / avg1 隔离实验
ICM-only div21 / FIFO batch 隔离实验
PPG+ICM no-SD 实验
MIC SAI DMA stall 实验
stop-only-fsync 实验
```

本任务只做：

```text
MIC OFF
PPG 50Hz avg1
ICM ACCEL/GYRO div10
ECG 当前 MAX30003 配置
ECG + PPG + ICM 三主模态稳定验证
```

---

## 1. 当前已确认事实

### 1.1 PPG

已确认：

```text
PPG avg4 → 12.55Hz
PPG avg1 → 50.13Hz
```

结论：

```text
PPG 12.5Hz 是 FIFO averaging=4 的配置效果，不是故障。
```

当前冻结配置：

```text
MAX30102 LED sample rate = 50Hz
FIFO averaging = 1
目标有效输出 = 50Hz
```

### 1.2 ICM20948

已确认：

```text
div=21 → 1125 / 22 = 51.14Hz
实测约 52Hz
div=10 → 实测约 104.24~104.25Hz
```

当前冻结配置：

```text
ACCEL_SMPLRT_DIV = 10
GYRO_SMPLRT_DIV = 10
目标 ODR ≈ 104Hz
```

### 1.3 ECG

已确认：

```text
MIC OFF 后，ECG 接近 488~489Hz。
```

当前冻结目标：

```text
配置仍为 512 SPS
实际目标接受范围：480~500Hz
短期不继续强攻 512Hz
```

### 1.4 MIC

当前策略：

```text
MIC OFF
AudioTask 不创建或不参与主判定
MIC 不得影响三主模态
```

---

## 2. 必须固化的代码配置

## 2.1 PPG 配置

必须确保代码里有明确配置：

```c
#define RECORD_PPG_LED_SAMPLE_RATE_HZ        50
#define RECORD_PPG_FIFO_AVERAGING            1
```

或等效配置。

必须在录制 summary 中输出：

```text
PPG_LED_SAMPLE_RATE_HZ=50
PPG_FIFO_AVERAGING=1
PPG_FIFO_CONFIG_READBACK=0x1F 或当前驱动对应 avg1 的值
PPG_EXPECTED_EFFECTIVE_SPS=50
```

---

## 2.2 ICM 配置

必须确保：

```c
#define RECORD_ICM_ACCEL_SMPLRT_DIV          10
#define RECORD_ICM_GYRO_SMPLRT_DIV           10
```

或等效配置。

必须在 summary 中输出：

```text
ICM_ACCEL_SMPLRT_DIV=10
ICM_GYRO_SMPLRT_DIV=10
ICM_EXPECTED_ODR=104Hz 左右
ICM_REG_READBACK_OK=1
```

---

## 2.3 MIC 配置

必须确保：

```c
#define RECORD_ENABLE_AUDIO 0
```

或等效关闭 MIC。

summary 中必须写：

```text
MIC_ENABLED=0
MIC_STATUS=DISABLED
```

---

## 3. 设备 1 实验矩阵

## 3.1 实验 A：90 秒三主模态复现 ×3

### 设置

```text
ECG ON
PPG ON, 50Hz avg1
ICM ON, div10
MIC OFF
duration = 90s
writer = 当前稳定 baseline writer
fsync = 当前正常策略，不使用 stop-only-fsync
```

### 执行

连续三轮：

```text
A1: 90s run1
A2: 90s run2
A3: 90s run3
```

### 通过标准

每轮必须满足：

```text
PPG actual_sps = 48~52Hz
ICM actual_sps = 100~106Hz
ECG actual_sps = 480~500Hz
I2C errors = 0
queue_fail = 0
sd_write_errors = 0
文件可解析
三主模态 count_match
```

---

## 3.2 实验 B：5 分钟三主模态复现 ×3

### 设置

```text
同实验 A
duration = 300s
```

### 执行

连续三轮：

```text
B1: 5min run1
B2: 5min run2
B3: 5min run3
```

### 通过标准

```text
PPG actual_sps = 48~52Hz
ICM actual_sps = 100~106Hz
ECG actual_sps = 480~500Hz
I2C errors = 0
queue_fail = 0
sd_write_errors = 0
文件可解析
三主模态 count_match
无 HardFault
无 FR_INVALID_OBJECT
```

---

## 3.3 实验 C：30 分钟冻结长测 ×1

只有 A/B 全部通过后执行。

### 设置

```text
ECG ON
PPG ON, 50Hz avg1
ICM ON, div10
MIC OFF
duration = 1800s
```

### 通过标准

```text
三主模态文件完整
三主模态 count_match
PPG 48~52Hz
ICM 100~106Hz
ECG 480~500Hz 或与前两组一致
I2C errors = 0
queue_fail = 0
sd_write_errors = 0
无 HardFault
无 FR_INVALID_OBJECT
```

---

## 4. 文件拉取与解析要求

每轮实验必须从 SD 实际文件解析，不允许只看 debug summary。

必须拉取：

```text
ecg_samples.csv 或当前三主模态 CSV
session.txt / session.json
diag_summary.txt
modality_summary.csv，如存在
```

必须输出：

```text
ECG file_count
ECG first_tick
ECG last_tick
ECG actual_sps

PPG file_count
PPG first_tick
PPG last_tick
PPG actual_sps
PPG FIFO_CONFIG readback

ICM file_count
ICM first_tick
ICM last_tick
ICM actual_sps
ICM div readback
```

---

## 5. PC 验证脚本要求

使用或更新：

```text
python/ppg_icm_rate_report.py
tools/verify_core_modalities.py
```

必须输出表格：

```csv
run,duration_s,ecg_count,ecg_sps,ppg_count,ppg_sps,imu_count,imu_sps,ppg_cfg,icm_div,i2c_err,queue_fail,sd_err,count_match,status
A1,90,...
A2,90,...
A3,90,...
B1,300,...
B2,300,...
B3,300,...
C1,1800,...
```

---

## 6. PASS / WARN / FAIL

### PASS

```text
90s ×3 全部通过
5min ×3 全部通过
30min ×1 通过
PPG/ICM/ECG 采样率稳定
三主模态 count_match
I2C errors = 0
queue_fail = 0
sd_write_errors = 0
```

### WARN

```text
ECG 低于 480Hz 但三轮稳定且 count_match
PPG/ICM 偶发轻微偏差但无 error
```

### FAIL

```text
PPG 不在 48~52Hz
ICM 不在 100~106Hz
ECG 明显低于 no-MIC baseline
任一主模态 count 不闭合
I2C error
queue_fail
sd_write_errors
MIC 被意外开启
```

---

## 7. 最终提交物

请提交：

```text
1. 当前 commit
2. 固化配置 diff
3. A1/A2/A3 90s 结果
4. B1/B2/B3 5min 结果
5. C1 30min 结果，如执行
6. SD 实际文件解析输出
7. 最终推荐配置
8. 是否可以命名版本 v0.3-core-three-modal-stable
```

最终报告格式：

```text
最终判断：

PPG:
  config:
  measured_range:
  status:

ICM:
  config:
  measured_range:
  status:

ECG:
  config:
  measured_range:
  status:

System:
  MIC:
  I2C errors:
  queue_fail:
  SD errors:
  30min_pass:

结论：
PASS / WARN / FAIL
```

---

## 8. 给 Agent 1 的简短目标

请把设备 1 已验证通过的三主模态目标配置固化为正式主线配置：MIC OFF，PPG=50Hz + FIFO avg1，ICM ACCEL/GYRO div=10，ECG 保持当前 MAX30003 配置。不要再重复 PPG/ICM 隔离实验，也不要继续处理 MIC。请执行 90s×3、5min×3、30min×1 的三主模态验证。每轮必须从 SD 实际 CSV 解析 ECG/PPG/ICM 的 file_count、actual_sps 和 count_match，并读回 PPG FIFO_CONFIG 与 ICM 分频寄存器。PASS 标准：PPG 48~52Hz，ICM 100~106Hz，ECG 480~500Hz，I2C errors=0，queue_fail=0，sd_write_errors=0，MIC disabled，文件可解析。若通过，建议命名为 v0.3-core-three-modal-stable。
