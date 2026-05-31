# 设备 1 任务：三主模态目标采样率配置固化实验

## 0. 给 Agent 1 的强制目标

请基于当前 GitHub 工程 `kejiyang3/L496_0405` 和最新 `PPG_ICM_RATE_ISOLATION_HANDOFF.md`，执行 **三主模态目标采样率配置固化实验**。

设备 1 已完成 PPG / ICM 采样率隔离验证。当前不再重复 case1~case7 的隔离实验。  
现在进入下一阶段：把隔离实验得到的结论转化为稳定的目标配置。

当前已经确认：

```text
PPG 12.5Hz 不是故障，而是 MAX30102 FIFO averaging=4 导致。
PPG avg4 → 12.55Hz
PPG avg1 → 50.13Hz

ICM 52Hz 不是 I2C/SD/调度瓶颈，而是寄存器分频 div=21 导致。
ICM div=21 → ODR = 1125 / (1 + 21) = 51.14Hz
实测约 52.08Hz

PPG + ICM 一起运行不会互相拖慢。
no-SD 后速率不恢复，说明 SD writer 不是 PPG/ICM 低速根因。
ECG + PPG + ICM no-MIC 下，ECG 可达约 488.91Hz。
```

本任务目标：

```text
1. 固化 MIC OFF 的三主模态路线；
2. 选择并验证 PPG 目标有效采样率；
3. 将 ICM 目标 ODR 调整到约 100Hz；
4. 复测 ECG + PPG + ICM no-MIC 90s / 5min；
5. 证明三主模态 count 闭合、采样率稳定、SD 文件可解析；
6. 输出最终推荐配置。
```

本任务不处理 MIC。  
MIC 必须关闭或最低优先级，不参与 PASS/FAIL。  
本任务不继续追 MAX30003 512Hz 硬件根因。  
ECG 当前 no-MIC 下接近 489Hz，可作为短期稳定目标。

---

## 1. 当前证据总结

### 1.1 PPG 结论

隔离实验结果：

| Case | 配置 | PPG SPS | 结论 |
|---|---|---:|---|
| Case 1 | PPG-only avg4 | 12.55Hz | avg4 基线 |
| Case 2 | PPG-only avg1 | 50.13Hz | avg1 后恢复到约 50Hz |

结论：

```text
PPG 12.5Hz 是 FIFO averaging=4 的配置效果，不是丢样。
```

### 1.2 ICM 结论

隔离实验结果：

| Case | 配置 | IMU SPS | 结论 |
|---|---|---:|---|
| Case 3 | ICM-only div=21 | 52.08Hz | 配置导致 |
| Case 4 | ICM FIFO batch | 52.10Hz | FIFO batch 不改变 ODR |

结论：

```text
ICM 52Hz 是 SMPLRT_DIV=21 导致。
ODR = 1125 / (1 + 21) = 51.14Hz。
```

### 1.3 组合结论

| Case | 结果 | 结论 |
|---|---|---|
| Case 5 PPG+ICM | 速率与单独运行一致 | PPG/ICM 不互相拖慢 |
| Case 6 PPG+ICM no-SD | 与 Case 5 一致 | SD writer 不是根因 |
| Case 7 ECG+PPG+ICM no-MIC | ECG≈488.91Hz, PPG≈13.44Hz, ICM≈52.22Hz | no-MIC 三主模态可行 |

---

## 2. 目标配置选择

## 2.1 PPG 推荐配置

### 方案 PPG-A：高时间分辨率

```text
LED sample rate = 50Hz
FIFO averaging = 1
effective PPG output ≈ 50Hz
```

优点：

```text
更适合 ECG-PPG 联合分析
更适合脉搏波时间细节
更适合后续运动状态联合分析
```

缺点：

```text
噪声可能高于 avg4
I2C/SD 数据量约为 avg4 的 4 倍
```

### 方案 PPG-B：低噪声低数据量

```text
LED sample rate = 50Hz
FIFO averaging = 4
effective PPG output ≈ 12.5Hz
```

优点：

```text
更低噪声
更低数据量
系统压力小
```

缺点：

```text
时间分辨率较低
不适合精细 ECG-PPG 时序分析
```

### 本任务默认选择

默认先固化：

```text
PPG-A: 50Hz + avg1 → effective ≈ 50Hz
```

如果实测出现 PPG overflow/I2C error/SD 压力，再退回 avg4。

---

## 2.2 ICM 推荐配置

当前：

```text
ACCEL_SMPLRT_DIV = 21
GYRO_SMPLRT_DIV = 21
ODR ≈ 51.14Hz
```

目标：

```text
ICM effective output ≈ 100Hz
```

根据公式：

```text
ODR = 1125 / (1 + div)
```

推荐测试：

```text
div = 10
ODR = 1125 / 11 ≈ 102.27Hz
```

因此默认修改为：

```text
ACCEL_SMPLRT_DIV = 10
GYRO_SMPLRT_DIV = 10
```

如果 div=10 后 I2C 或文件写入压力过大，可测试：

```text
div = 11 → 93.75Hz
div = 14 → 75Hz
```

但第一目标是 div=10。

---

## 3. 代码修改要求

## 3.1 PPG 配置

请在 MAX30102 初始化或 feature flag 中提供明确宏：

```c
#define RECORD_PPG_LED_SAMPLE_RATE_HZ        50
#define RECORD_PPG_FIFO_AVERAGING            1
```

确保实际写入 MAX30102 FIFO_CONFIG 后读回：

```text
FIFO_CONFIG = avg1 对应值，例如 0x1F
```

具体 bit 以当前驱动为准，不要硬写错误值。

必须在 `diag_summary.txt` 或 `session.txt` 中输出：

```text
PPG_LED_SAMPLE_RATE_HZ=
PPG_FIFO_AVERAGING=
PPG_EFFECTIVE_EXPECTED_SPS=
PPG_FIFO_CONFIG_READBACK=
```

---

## 3.2 ICM 配置

请将 ICM accel/gyro sample divider 修改为：

```c
#define RECORD_ICM_ACCEL_SMPLRT_DIV    10
#define RECORD_ICM_GYRO_SMPLRT_DIV     10
```

或在当前驱动中直接设置：

```text
ACCEL_SMPLRT_DIV = 10
GYRO_SMPLRT_DIV = 10
```

必须读回并输出：

```text
ACCEL_SMPLRT_DIV_1
ACCEL_SMPLRT_DIV_2
GYRO_SMPLRT_DIV
ACCEL_ODR_EXPECTED
GYRO_ODR_EXPECTED
```

期望：

```text
ODR ≈ 102.27Hz
```

---

## 3.3 MIC 关闭

本任务要求：

```c
#define RECORD_ENABLE_AUDIO 0
```

或等效关闭 MIC / AudioTask。

验收：

```text
AudioTask 不创建，或 MIC status = DISABLED。
```

---

## 4. 实验矩阵

## 4.1 实验 A：PPG avg1 + ICM div10，三主模态 90 秒

### 设置

```text
ECG ON
PPG ON, 50Hz avg1
ICM ON, div10
MIC OFF
duration = 90s
SD ON
```

### 预期

```text
ECG ≈ 480~500Hz
PPG ≈ 50Hz
ICM ≈ 100~103Hz
```

### 必须记录

```text
ECG file_count / actual_sps / count_match
PPG file_count / actual_sps / count_match
ICM file_count / actual_sps / count_match
PPG FIFO_CONFIG readback
ICM divider readback
I2C3 wait max
I2C3 hold max
I2C error count
SD write error count
queue fail count
```

---

## 4.2 实验 B：三主模态 5 分钟稳定性

### 设置

```text
同实验 A
duration = 300s
```

### 目的

验证目标配置在中等时长下稳定。

### 通过条件

```text
ECG/PPG/ICM file_count == capture_count 或差异明确解释
PPG actual_sps 稳定在 48~52Hz
ICM actual_sps 稳定在 98~105Hz
ECG actual_sps 稳定在 480~500Hz 或与 no-MIC baseline 一致
I2C errors = 0
queue_fail = 0
SD error = 0
```

---

## 4.3 实验 C：PPG avg4 回退对照

如果实验 A 或 B 出现 I2C/SD 压力、PPG overflow、主模态 count 不闭合，则执行。

### 设置

```text
ECG ON
PPG ON, 50Hz avg4
ICM ON, div10
MIC OFF
duration = 90s
```

### 目的

判断 PPG 50Hz avg1 是否给系统带来压力。

### 判定

| 结果 | 结论 |
|---|---|
| avg1 异常，avg4 正常 | PPG 数据量/I2C 负载影响系统，可考虑 avg4 |
| avg1 正常 | 使用 avg1 |
| 两者都异常 | 查 I2C3/SD writer/任务优先级 |

---

## 4.4 实验 D：ICM div11 / div14 回退对照

如果 div10 下 ICM 不稳定，执行：

```text
D1: div11 → expected 93.75Hz
D2: div14 → expected 75Hz
```

目标：

```text
找到 ICM 在当前系统中的最高稳定输出率。
```

---

## 4.5 实验 E：目标配置 30 分钟长测

只有 5 分钟通过后执行。

### 设置

```text
ECG ON
PPG ON, 最终选择 avg1 或 avg4
ICM ON, 最终选择 div10/div11/div14
MIC OFF
duration = 1800s
```

### 通过条件

```text
三主模态文件完整
三主模态 count_match
无 I2C error
无 SD error
无 queue fail
采样率稳定
```

---

## 5. PC 端验证

必须更新或使用：

```text
python/ppg_icm_rate_report.py
tools/verify_core_modalities.py
```

每次实验必须从 SD 实际文件解析：

```text
file_count
first_tick
last_tick
actual_sps
```

禁止只看 debug summary。

输出表格：

```csv
experiment,ecg_sps,ppg_sps,icm_sps,ppg_cfg,icm_div,i2c_err,i2c_wait_max,sd_err,count_match,status
A,488.9,50.1,102.3,avg1,10,0,?,0,YES,OK
B,...
C,...
```

---

## 6. PASS / WARN / FAIL 标准

### PASS

```text
MIC OFF
ECG/PPG/ICM 三主模态文件均可解析
ECG/PPG/ICM count_match
PPG avg1 后约 50Hz
ICM div10 后约 102Hz
I2C error = 0
queue fail = 0
SD error = 0
5 分钟稳定通过
```

### WARN

```text
ECG 未到 512 但稳定在 480~500Hz
PPG avg1 可用但噪声待评估
ICM div10 可用但 I2C wait 稍高
```

### FAIL

```text
PPG avg1 后仍 12.5Hz
ICM div10 后仍 52Hz 且寄存器读回不正确
I2C error 增加
queue fail
SD error
任一主模态文件 count 不闭合
MIC 被意外开启且影响主模态
```

---

## 7. 最终提交物

请提交：

```text
1. 当前 commit
2. PPG 配置修改 diff
3. ICM div 修改 diff
4. 实验 A 90s 结果
5. 实验 B 5min 结果
6. 如执行，PPG avg4 回退结果
7. 如执行，ICM div11/div14 回退结果
8. 如执行，30min 长测结果
9. SD 实际文件解析输出
10. 最终推荐配置
```

最终报告格式：

```text
最终判断：

PPG:
  selected_config:
  fifo_average:
  expected_sps:
  measured_sps:
  register_readback:
  status:

ICM:
  selected_div:
  expected_odr:
  measured_sps:
  register_readback:
  status:

ECG:
  measured_sps:
  count_match:
  status:

System:
  MIC:
  I2C_error:
  SD_error:
  queue_fail:
  5min_pass:

最终推荐：
1.
2.
3.
```

---

## 8. 给 Agent 1 的简短目标

请基于 PPG/ICM 隔离实验最终结论，进入三主模态目标采样率配置固化。当前不要重复 case1~case7。请关闭 MIC，设置 PPG 为 50Hz + FIFO averaging=1，目标有效输出约 50Hz；将 ICM20948 的 ACCEL_SMPLRT_DIV 和 GYRO_SMPLRT_DIV 从 21 改为 10，目标 ODR≈1125/(1+10)=102.27Hz。然后执行 ECG+PPG+ICM no-MIC 90 秒和 5 分钟实验。必须从 SD 实际 CSV 解析 ECG/PPG/ICM 的 file_count、first_tick、last_tick、actual_sps，并读回 MAX30102 FIFO_CONFIG 和 ICM20948 分频寄存器。PASS 标准是 PPG≈50Hz、ICM≈100~103Hz、ECG 维持 no-MIC baseline 接近 489Hz，三主模态 count_match，I2C error=0，queue_fail=0，SD error=0。如果 PPG avg1 或 ICM div10 引入压力，再做 PPG avg4 或 ICM div11/div14 回退对照。
