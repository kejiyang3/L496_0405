# PPG / ICM 采样率隔离验证实验任务书

## 0. 给 Agent 的强制目标

请基于当前 GitHub 工程 `kejiyang3/L496_0405`，执行 **PPG / ICM20948 采样率隔离验证实验**。

当前问题：

```text
PPG configured_sps = 50Hz，但实际约 12.96Hz
ICM configured_odr = 104Hz，但实际约 52.15Hz
```

这两个采样率都明显偏低。不能简单归因于“四模态竞争”或“I2C3 共享”，必须做隔离实验确认：

```text
1. PPG-only 时，PPG 是否仍然只有约 12.5Hz？
2. ICM-only 时，ICM 是否仍然只有约 52Hz？
3. PPG + ICM 同时运行时，是否因为 I2C3 共享导致速率下降？
4. PPG 的 12.5Hz 是否确实来自 MAX30102 FIFO averaging=4？
5. ICM 的 52Hz 是否来自真实 ODR 配置、DataReady 处理丢半、FIFO 未启用、I2C 带宽不足，还是任务调度问题？
```

本任务的重点不是 ECG，也不是 MIC。  
本任务只围绕：

```text
PPG 采样率隔离
ICM 采样率隔离
PPG + ICM 共享 I2C3 影响
```

最终目标：

```text
明确 PPG 和 ICM 的低采样率到底是配置导致、总线导致、调度导致，还是驱动读取策略导致。
```

---

## 1. 当前已知现象

### 1.1 PPG 当前现象

最新 SD 拉取报告显示：

```text
PPG configured_sps = 50Hz
PPG CSV 实际行数 = 400
PPG 数据跨度 = 30.851s
PPG actual_sps = 12.96Hz
```

报告推测：

```text
MAX30102 FIFO_CONFIG=0x5F
FIFO averaging = 4
50Hz / 4 = 12.5Hz
```

这可能是正常配置结果，但必须通过隔离实验确认。

### 1.2 ICM 当前现象

最新 SD 拉取报告显示：

```text
ICM configured_odr = 104Hz
ICM CSV 实际行数 = 1664
ICM 数据跨度 = 31.907s
ICM actual_sps = 52.15Hz
```

报告推测：

```text
I2C3 与 PPG 共享
互斥锁等待
每次读 ACCEL + GYRO 约 24B
实际约为 ODR 一半
```

但这只是推测。必须通过 ICM-only、ICM+PPG 对照确认。

---

## 2. 一票否决条件

以下任一出现，本任务 FAIL：

```text
1. 没有 PPG-only 实验；
2. 没有 ICM-only 实验；
3. 没有 PPG + ICM 组合实验；
4. 没有解析实际 CSV 文件；
5. 只看 debug summary，不看文件行数；
6. 没有读回 MAX30102 / ICM20948 关键配置寄存器；
7. 没有给出 first_tick、last_tick、file_count、actual_sps；
8. 没有解释 PPG 12.5Hz 和 ICM 52Hz 的来源。
```

---

## 3. 必须新增或确认的诊断字段

## 3.1 PPG 诊断字段

必须记录：

```text
ppg_task_calls
ppg_irq_count
ppg_timeout_wakes
ppg_fifo_reads
ppg_fifo_samples
ppg_samples_queued
ppg_samples_written
ppg_file_count
ppg_fifo_overflow_count
ppg_read_fail_count
ppg_i2c_error_count
ppg_i2c_wait_max
ppg_i2c_hold_max
ppg_first_tick_ms
ppg_last_tick_ms
ppg_actual_sps
```

必须读回 MAX30102 配置：

```text
FIFO_CONFIG
SPO2_CONFIG
MODE_CONFIG
INT_ENABLE
INT_STATUS
LED pulse amplitude registers
sample_average
sample_rate
pulse_width
```

必须解码：

```text
PPG_LED_SAMPLE_RATE
PPG_FIFO_AVERAGING
PPG_EFFECTIVE_OUTPUT_RATE = LED_SAMPLE_RATE / FIFO_AVERAGING
```

---

## 3.2 ICM 诊断字段

必须记录：

```text
icm_task_calls
icm_irq_count
icm_timeout_wakes
icm_dataready_count
icm_read_ok_count
icm_read_fail_count
icm_samples_queued
icm_samples_written
icm_file_count
icm_i2c_error_count
icm_i2c_wait_max
icm_i2c_hold_max
icm_first_tick_ms
icm_last_tick_ms
icm_actual_sps
icm_accel_samples
icm_gyro_samples
icm_mag_samples
```

必须读回 ICM20948 配置：

```text
WHO_AM_I
USER_CTRL
LP_CONFIG
PWR_MGMT_1
PWR_MGMT_2
ACCEL_CONFIG
ACCEL_CONFIG_2
GYRO_CONFIG_1
GYRO_CONFIG_2
ACCEL_SMPLRT_DIV_1
ACCEL_SMPLRT_DIV_2
GYRO_SMPLRT_DIV
INT_ENABLE
INT_STATUS
FIFO_EN
FIFO_MODE
FIFO_COUNTH / FIFO_COUNTL
```

必须解码：

```text
ACCEL_ODR
GYRO_ODR
MAG_ODR
DLPF
SMPLRT_DIV
FIFO enabled/disabled
DataReady enabled/disabled
```

---

## 4. 实验矩阵

## 4.1 实验 P1：PPG-only 90 秒

### 设置

```text
ECG OFF
ICM OFF
MIC OFF
PPG ON
SD ON
duration = 90s
```

### 目的

判断 PPG 单独运行时是否仍为约 12.5Hz。

### 必须记录

```text
PPG file_count
PPG first_tick_ms
PPG last_tick_ms
PPG actual_sps
PPG FIFO_CONFIG
PPG sample_average
PPG configured_led_sps
PPG effective_expected_sps
PPG fifo_overflow
PPG i2c_error
```

### 判定

| 结果 | 结论 |
|---|---|
| PPG-only ≈ 12.5Hz，且 avg=4 | PPG 低速率是配置导致，不是缺陷 |
| PPG-only ≈ 50Hz | 四模态或 I2C 共享导致降速 |
| PPG-only 仍远低于 expected | PPG 驱动/中断/I2C 问题 |

---

## 4.2 实验 P2：PPG-only avg1 90 秒

### 设置

```text
ECG OFF
ICM OFF
MIC OFF
PPG ON
FIFO averaging = 1
LED sample rate = 50Hz
duration = 90s
```

### 目的

验证 FIFO averaging 是否决定输出率。

### 预期

```text
actual_sps ≈ 50Hz
```

### 判定

| 结果 | 结论 |
|---|---|
| avg1 后约 50Hz | PPG 12.5Hz 确认为 avg4 导致 |
| avg1 后仍 12.5Hz | 配置未生效或驱动限速 |
| avg1 后 I2C error/overflow | 读取策略不足以支撑 50Hz |

---

## 4.3 实验 I1：ICM-only 90 秒

### 设置

```text
ECG OFF
PPG OFF
MIC OFF
ICM ON
SD ON
duration = 90s
```

### 目的

判断 ICM 单独运行时是否能达到 104Hz。

### 必须记录

```text
ICM file_count
ICM first_tick_ms
ICM last_tick_ms
ICM actual_sps
ICM ODR register decode
ICM read_ok_count
ICM read_fail_count
ICM i2c_wait_max
ICM i2c_hold_max
ICM dataready_count
```

### 判定

| 结果 | 结论 |
|---|---|
| ICM-only ≈ 104Hz | ICM 本身正常，PPG/I2C 共享导致组合降速 |
| ICM-only ≈ 52Hz | ICM 配置/驱动/调度本身就是 52Hz |
| ICM-only 不稳定 | ICM 驱动、INT、I2C 或 TXS0104 问题 |

---

## 4.4 实验 I2：ICM-only FIFO batch 90 秒

### 设置

```text
ICM ON
FIFO enabled
batch read FIFO
ECG OFF
PPG OFF
MIC OFF
duration = 90s
```

### 目的

判断 ICM 单样本读取效率是否导致只能 52Hz。

### 判定

| 结果 | 结论 |
|---|---|
| FIFO batch 后接近 104Hz | 原单样本读取效率不足 |
| FIFO batch 仍 52Hz | 配置或 DataReady 频率就是 52Hz |
| FIFO overflow | 读取周期过慢或 batch 太小 |

---

## 4.5 实验 C1：PPG + ICM 90 秒

### 设置

```text
ECG OFF
MIC OFF
PPG ON
ICM ON
SD ON
duration = 90s
```

### 目的

确认 I2C3 共享时是否互相拖慢。

### 必须记录

```text
PPG actual_sps
ICM actual_sps
PPG i2c_wait_max / hold_max
ICM i2c_wait_max / hold_max
Mtx_I2C3 timeout count
PPG file_count
ICM file_count
```

### 判定

| 结果 | 结论 |
|---|---|
| PPG-only 正常，ICM-only 正常，组合下降 | I2C3 共享/互斥锁调度导致 |
| PPG-only 12.5，组合 12.5 | PPG 正常配置输出 |
| ICM-only 104，组合 52 | ICM 被 PPG/I2C 竞争拖慢 |
| ICM-only 52，组合 52 | ICM 本身即 52Hz |

---

## 4.6 实验 C2：PPG + ICM no-SD 90 秒

### 设置

```text
PPG ON
ICM ON
SD OFF
只计数，不写文件
duration = 90s
```

### 目的

排除 SD 写入对 PPG/ICM 的影响。

### 判定

| 结果 | 结论 |
|---|---|
| no-SD 后速率上升 | SD writer 影响 PPG/ICM |
| no-SD 仍低 | 不是 SD，查 I2C/配置/任务调度 |
| no-SD 稳定但 with-SD 不稳 | 批量写/缓冲区需要优化 |

---

## 4.7 实验 C3：三主模态 no-MIC 90 秒

### 设置

```text
ECG ON
PPG ON
ICM ON
MIC OFF
SD ON
duration = 90s
```

### 目的

确认在核心系统中 PPG/ICM 的最终表现。

---

## 5. I2C3 共享诊断

PPG 和 ICM 如果共享 I2C3，必须记录：

```text
i2c3_mutex_acquire_count
i2c3_mutex_timeout_count
i2c3_wait_ms_last
i2c3_wait_ms_max
i2c3_hold_ms_last
i2c3_hold_ms_max
i2c3_bytes_read_ppg
i2c3_bytes_read_icm
i2c3_error_count
```

输出：

```text
I2C3STAT,tick,
ppg_wait_max=,
ppg_hold_max=,
icm_wait_max=,
icm_hold_max=,
mutex_timeout=,
bytes_ppg=,
bytes_icm=,
err=
```

判定：

```text
如果 wait_max 很高：互斥锁竞争。
如果 hold_max 很高：单次 I2C 事务过长。
如果 timeout 增加：调度/优先级或总线卡死。
如果 error 增加：硬件总线/电平转换/TXS0104 问题。
```

---

## 6. 任务调度诊断

必须记录：

```text
Task_PPG calls/s
Task_IMU calls/s
PPG notify_wakes/s
ICM notify_wakes/s
PPG timeout_wakes/s
ICM timeout_wakes/s
```

判断：

```text
如果 PPG/ICM task 调用频率不足：任务优先级/调度问题。
如果 task 调用足够但样本少：芯片配置或 FIFO/ODR 问题。
如果 task 调用多但 I2C wait 高：I2C3 竞争问题。
```

---

## 7. CSV / 文件比对

每个实验结束后必须解析实际 CSV 文件。

输出表：

```csv
experiment,modality,configured_rate,expected_effective_rate,file_count,first_tick,last_tick,duration_ms,actual_sps,count_match,status
P1,PPG,50/avg4,12.5,...
P2,PPG,50/avg1,50,...
I1,ICM,104,104,...
I2,ICM_FIFO,104,104,...
C1,PPG,...
C1,ICM,...
```

不要只看 summary。必须看实际文件行数。

---

## 8. 结果判定总表

最终报告必须填写：

| 实验 | PPG SPS | ICM SPS | 结论 |
|---|---:|---:|---|
| PPG-only avg4 | | - | |
| PPG-only avg1 | | - | |
| ICM-only | - | | |
| ICM-only FIFO | - | | |
| PPG+ICM | | | |
| PPG+ICM no-SD | | | |
| ECG+PPG+ICM no-MIC | | | |

---

## 9. 可能修复方向

### 9.1 如果 PPG avg1 能到 50Hz

说明 PPG 正常。  
按业务需要选择：

```text
稳定低噪声：PPG 50Hz avg4 → effective 12.5Hz
高时间分辨率：PPG 50Hz avg1 → effective 50Hz
折中：PPG 100Hz avg4 → effective 25Hz
```

### 9.2 如果 ICM-only 只有 52Hz

优先查：

```text
ICM20948 ODR 寄存器配置
SMPLRT_DIV
DLPF
DataReady 中断配置
任务 timeout 周期
是否软件只每 20ms 读取一次
```

### 9.3 如果 ICM-only 104Hz，但 PPG+ICM 变 52Hz

优先改：

```text
I2C3 事务批量化
PPG/ICM task 优先级
I2C3 mutex 策略
ICM FIFO batch read
提高 I2C3 speed
```

### 9.4 如果 no-SD 后恢复

优先改：

```text
三主模态 ring buffer
MultiSensor_SDWriter batch 4KB
stop-only f_sync
```

---

## 10. PASS / WARN / FAIL

### PASS

```text
PPG 低速率由 FIFO averaging 明确解释，或 avg1 后达到配置目标
ICM 低速率根因明确
PPG/ICM file_count == capture_count
PPG/ICM tick 单调
I2C3 无不可解释 timeout/error
```

### WARN

```text
PPG 保持 12.5Hz，但业务可接受
ICM 保持 52Hz，但业务可接受
I2C3 wait 较高但无 drop
```

### FAIL

```text
PPG-only 不符合任何配置预期
ICM-only 不符合 ODR 预期且无法解释
PPG/ICM 文件计数不闭合
I2C3 error/timeout 增加
SD 文件解析失败
```

---

## 11. 最终提交物

请提交：

```text
1. 当前 commit
2. MAX30102 配置寄存器读回与解码
3. ICM20948 配置寄存器读回与解码
4. PPG-only avg4 结果
5. PPG-only avg1 结果
6. ICM-only 结果
7. ICM-only FIFO batch 结果
8. PPG+ICM 结果
9. PPG+ICM no-SD 结果
10. 三主模态 no-MIC 结果
11. I2C3STAT 日志
12. 任务调度统计
13. CSV 实际行数比对
14. 最终判断
```

最终判断格式：

```text
最终判断：

PPG:
  低速率原因：
  配置证据：
  文件证据：
  是否需要修改配置：

ICM:
  低速率原因：
  配置证据：
  文件证据：
  是否需要 FIFO / I2C / 调度优化：

PPG+ICM 组合:
  是否互相影响：
  I2C3 是否瓶颈：

下一步：
1.
2.
3.
```

---

## 12. 给 Agent 的简短目标

请执行 PPG / ICM20948 采样率隔离验证。当前 PPG 配置 50Hz 但实际约 12.96Hz，ICM 配置 104Hz 但实际约 52.15Hz，不能直接假设它们是正常的，也不能直接归因于 I2C3 共享。必须做 PPG-only avg4、PPG-only avg1、ICM-only、ICM-only FIFO batch、PPG+ICM、PPG+ICM no-SD、ECG+PPG+ICM no-MIC 七组实验。每组必须从实际 CSV 文件解析 file_count、first_tick、last_tick、actual_sps，并读回 MAX30102 / ICM20948 关键配置寄存器。必须记录 I2C3 mutex wait/hold、I2C error、Task_PPG/Task_IMU 调用频率。最终判断 PPG 12.5Hz 是否由 FIFO averaging 导致，ICM 52Hz 是配置/调度/FIFO/I2C 哪一类问题。
