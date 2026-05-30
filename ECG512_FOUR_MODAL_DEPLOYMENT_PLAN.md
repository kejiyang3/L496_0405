# ECG 512Hz + 四模态采集新部署任务书

## 0. 总目标

本阶段目标是把工程从“问题定位阶段”推进到“可稳定部署阶段”。

最终目标分成两步：

1. **部署目标 A：四模态稳定采集**
   - MIC + ECG + PPG + IMU 同时开启；
   - ECG 在当前已验证基线附近稳定运行，即约 374~391 Hz；
   - ECG 写入可靠：`ecg_written_count == ecg_sample_count == fifo_valid_count`；
   - Pack/Queue 无丢失；
   - AudioTask、DebugLogWriter、MultiSensor_SDWriter 不阻塞 SensorTask；
   - 作为当前可用版本冻结。

2. **部署目标 B：ECG 从 374 Hz 提升到 512 Hz**
   - 在部署目标 A 稳定后，单独排查 ECG-only 为什么不到 512；
   - 不再混入 AudioTask、SD Debug Log、CSV writer 等已经修复的问题；
   - 重点围绕 MAX30003 配置、FCLK、INTB/EXTI、FIFO drain 周期、SPI burst、512/256/128 比例测试。

禁止把这两个目标混在一个补丁里。  
先实现四模态稳定，再追 ECG 512。

---

## 1. 当前最新事实

### 1.1 已修复问题

| 问题 | 状态 |
|---|---|
| AudioTask 热循环重入 | 已修复 |
| AudioTask 优先级反转 | 已修复 |
| SD Debug Log 阻塞 ECG | 已修复 |
| DebugLogWriter 持久文件句柄导致 CSV `FR_INVALID_OBJECT` | 已修复 |
| `ecg_written_count=0` 偶发失败 | 已修复 |
| Case F 写入可靠性 | 3/3 轮通过 |

### 1.2 最新 Case F 验证结果

三轮 Case F 完整计数器：

| 指标 | R1 | R2 | R3 |
|---|---:|---:|---:|
| `ecg_sample_count` | 11727 | 11234 | 11236 |
| `ecg_written_count` | 11727 | 11234 | 11236 |
| `fifo_valid_count` | 11727 | 11234 | 11236 |
| ECG SPS | ~391 Hz | ~374 Hz | ~375 Hz |
| `fifo_eovf_count` | 6 | 7 | 7 |
| `diag_max_gap_recording_ms` | 1003 | 1003 | 1003 |
| `ecg_pack_drop_blocks` | 0 | 0 | 0 |
| `ecg_queue_submit_fail` | 0 | 0 | 0 |
| `mic_bytes` | 147456 | 147456 | 147456 |

结论：

```text
四模态 Case F 已经从灾难性 74Hz 恢复到约 374~391Hz。
ECG 数据写入链路已经可靠。
Pack pipeline 健康。
DebugLog ring drop 可接受。
```

### 1.3 仍未解决问题

| 问题 | 当前判断 |
|---|---|
| `diag_max_gap_recording_ms = 1003ms` | 录制阶段仍存在约 1s 阻塞，需要定位 |
| Case A / Case F ECG 约 374Hz，不到 512Hz | 下一阶段重点 |
| INTB/EXTI 约 1Hz | 可能不是主采样节拍，但需要重新定义其角色 |
| FCLK 曾测得 37kHz / 毛刺 | 需要在 ECG-only 阶段复查 |
| PPG/IMU 四模态完整长期稳定性 | 需要长测 |

---

## 2. 部署原则

### 2.1 不再回退的原则

以下修复必须保留：

1. `RECORD_ENABLE_AUDIO=0` 时不创建 AudioTask；
2. AudioTask 优先级低于 SensorTask；
3. AudioTask session latch；
4. 音频 SD 写入短等待、可丢块；
5. SD Debug Log RAM ring buffer；
6. DebugLogWriter 低优先级；
7. DebugLogWriter close-after-flush；
8. 禁止 DebugLogWriter 中重复 `f_mount`；
9. 禁止实时任务直接 `f_open/f_write/f_sync/f_close` 写 debug log；
10. MAX30003 始终 drain 策略，禁止 EINT gating 回归。

### 2.2 优先级原则

建议最终任务优先级：

| 任务 | 优先级 | 原则 |
|---|---|---|
| SensorTask | `osPriorityAboveNormal` | 最高，负责 ECG/PPG/IMU 调度 |
| AudioTask | `osPriorityNormal1` 或 `osPriorityNormal` | 不得高于 SensorTask |
| MultiSensor_SDWriter | `osPriorityNormal` | 可写入但不得阻塞 SensorTask |
| DebugLogWriter | `osPriorityLow` | 日志可丢 |
| PPGDiagWriter | `osPriorityBelowNormal` 或更低 | 可丢 |
| AudioSDWriter，如新增 | `osPriorityBelowNormal` | 音频可丢 |

### 2.3 数据优先级原则

```text
ECG 实时性 > ECG 数据完整性 > PPG/IMU > MIC > Debug log
```

音频和 debug log 都可以丢。  
ECG FIFO drain 不能被任何 SD 写入拖死。

---

## 3. 部署阶段 A：四模态稳定版冻结

### 3.1 目标

形成一个可用固件版本：

```text
MIC + ECG + PPG + IMU 全部开启
ECG 稳定约 374~391 Hz
ECG 写入可靠
系统 30s / 5min / 30min 长测不崩
```

### 3.2 必须固定的配置

```c
#define RECORD_ENABLE_AUDIO 1
#define RECORD_ECG_SAMPLE_RATE_HZ 512U
#define RECORD_DIAG_CASE 0
```

确保所有实验宏关闭：

```c
#define RECORD_TEST_AUDIO_DROP_BEFORE_FWRITE 0
#define RECORD_TEST_AUDIO_WRITE_ONLY_NO_SYNC 0
#define RECORD_TEST_AUDIO_NO_PERIODIC_SYNC 0
#define RECORD_TEST_DISABLE_CSV_WRITER 0
#define RECORD_TEST_DISABLE_SD_DEBUG_LOG 0
#define RECORD_TEST_DISABLE_ALL_SD_LOGGING 0
```

保留生产级修复宏：

```c
#define RECORD_FIX_AUDIO_NONBLOCKING_DISCARD 1
#define RECORD_SD_DEBUG_RING_BUFFER 1
```

如果宏名不同，请以当前代码实际宏名为准。

---

## 4. 部署阶段 A 的验收测试

### 4.1 30 秒三轮短测

连续跑 3 轮完整四模态录制。

每轮记录：

```text
duration_ms
ecg_sample_count
fifo_valid_count
ecg_written_count
ecg_pack_blocks
ecg_pack_drop_blocks
ecg_queue_submit_ok
ecg_queue_submit_fail
ecg_writer_get_blocks
fifo_eovf_count
fifo_empty_count
fifo_unknown_etag_count
diag_task_calls
diag_max_gap_recording_ms
mic_bytes
mic_drops
ppg_sample_count
imu_sample_count
sd_write_bytes
debug_ring_drops
```

验收标准：

```text
ecg_written_count == ecg_sample_count == fifo_valid_count
ecg_pack_drop_blocks == 0
ecg_queue_submit_fail == 0
ECG SPS >= 360 Hz
fifo_eovf_count <= 8 / 30s
mic_bytes > 0
PPG/IMU 有数据
系统不 HardFault
SD 文件可拉取并解析
```

### 4.2 5 分钟中测

跑 5 分钟完整四模态录制。

验收标准：

```text
ECG SPS >= 360 Hz
ecg_written_count == ecg_sample_count
pack_drop == 0
queue_submit_fail == 0
SD 文件完整关闭
WAV 文件可读
CSV 文件可解析
debug_ring_drops 可接受
```

### 4.3 30 分钟长测

跑 30 分钟，确认：

```text
SD 不掉盘
FatFS 不返回 FR_INVALID_OBJECT
DebugLogWriter 不破坏 CSV 文件句柄
AudioTask 不重入
SensorTask 不被低优先级 writer 反向阻塞
```

---

## 5. 部署阶段 B：diag_max_gap_recording_ms = 1003ms 根因定位

### 5.1 目标

当前 `diag_max_gap_recording_ms = 1003ms` 已确认发生在录制阶段。  
但 ECG 仍可达到约 374~391 Hz，说明该 1s gap 可能是：

1. 停止/切换阶段被计入；
2. 某个低频任务每 1s 阻塞一次；
3. 某个传感器 I2C / SPI / SD 操作 timeout；
4. SensorTask 中某段逻辑被长时间执行；
5. 诊断统计方式仍有边界问题。

本阶段目标是定位 1s gap 出现在哪个代码段。

### 5.2 增加 SensorTask 分段耗时

在 SensorTask 主循环中加入分段计时：

```c
typedef enum {
    SENSOR_SEG_NONE = 0,
    SENSOR_SEG_WAIT_NOTIFY,
    SENSOR_SEG_MAX30003,
    SENSOR_SEG_PPG,
    SENSOR_SEG_IMU,
    SENSOR_SEG_PACK,
    SENSOR_SEG_SD_SUBMIT,
    SENSOR_SEG_STOP_CHECK,
    SENSOR_SEG_AUDIO_SYNC,
    SENSOR_SEG_OTHER
} SensorSegment_t;

volatile uint32_t g_sensor_seg_max_ms[16];
volatile uint32_t g_sensor_seg_last_ms[16];
volatile uint32_t g_sensor_seg_hit_count[16];
volatile uint32_t g_sensor_long_gap_segment;
volatile uint32_t g_sensor_long_gap_tick;
```

每段执行前后记录：

```c
uint32_t t0 = HAL_GetTick();
/* segment code */
uint32_t dt = HAL_GetTick() - t0;
if (dt > g_sensor_seg_max_ms[seg]) {
    g_sensor_seg_max_ms[seg] = dt;
}
if (dt > 100U) {
    g_sensor_long_gap_segment = seg;
    g_sensor_long_gap_tick = HAL_GetTick();
}
```

每秒或停止时记录：

```text
SEGSTAT,
wait_notify_max=,
max30003_max=,
ppg_max=,
imu_max=,
pack_max=,
sd_submit_max=,
stop_check_max=,
audio_sync_max=,
other_max=,
long_gap_segment=,
long_gap_tick=
```

### 5.3 判定

| 最大段 | 结论 |
|---|---|
| `WAIT_NOTIFY` 约 1000ms | 不是阻塞，是统计边界或 timeout 设置问题 |
| `PPG` / `IMU` 约 1000ms | I2C/SPI 传感器超时 |
| `SD_SUBMIT` 约 1000ms | 队列/SD writer 交互阻塞 |
| `STOP_CHECK` 约 1000ms | 停止状态机或文件关闭阻塞 |
| `MAX30003` 约 1000ms | SPI/STATUS/FIFO 读取异常 |
| `OTHER` 约 1000ms | 继续细分 |

---

## 6. 部署阶段 C：ECG 374Hz → 512Hz 专项

### 6.1 前置条件

只有当部署阶段 A 通过后，才进入本阶段。

必须满足：

```text
Case F 四模态稳定 >= 360Hz
ecg_written_count == ecg_sample_count
pack_drop == 0
queue_submit_fail == 0
SD 文件可靠
```

### 6.2 核心目标

确认 ECG-only 和四模态下，为什么 MAX30003 配置 512 SPS 但实际只有约 374 Hz。

不要再混入 AudioTask / SD Debug Log / CSV writer 问题。

---

## 7. ECG 512Hz 专项实验矩阵

### 7.1 实验 C1：完全无 SD，只计数

设置：

```text
Audio OFF
PPG OFF
IMU OFF
CSV OFF
Debug OFF
ECG FIFO only
不写 SD
只用 GDB 读计数器
```

目标：

判断 SD 是否仍影响 ECG-only。

判断：

```text
如果 FIFO valid 接近 512Hz：SD/写入仍是主因。
如果仍约 374Hz：问题在 MAX30003 产样/读取/调度。
```

### 7.2 实验 C2：提高 SensorTask 主动 drain 频率

当前 SensorTask 约 8ms 调用一次 MAX30003_Task。  
理论上 8ms = 125Hz task 调用，如果每次平均读 3~4 样本，可以达到 374Hz，但要达到 512Hz，必须保证每次读足够多且不频繁读空。

测试：

```text
8ms timeout → 4ms timeout → 2ms timeout
```

每次记录：

```text
task_calls
fifo_valid_count
fifo_empty_count
fifo_eovf_count
fifo_last_count
fifo_unknown_etag
spi_burst_count
burst_us_max
```

判断：

| 结果 | 结论 |
|---|---|
| 4ms/2ms 后 ECG 接近 512 | 主动 drain 频率不足 |
| 4ms/2ms 后仍 374 | 不是 task 调用频率 |
| empty 暴增 | 读太频繁但芯片产样不足 |
| eovf 降低但速率不升 | FIFO 读取/etag 解析问题 |

### 7.3 实验 C3：STATUS-only no-FIFO-read

运行 5~10 秒，只读 STATUS，不读 FIFO。

目标：

如果 MAX30003 正常 512 SPS 产样，不读 FIFO 应该很快 EINT/EOVF。

记录：

```text
status_eint_count
status_eovf_count
intb_low_count
exti_raw_count
pll_warn_count
```

判断：

```text
EINT/EOVF 很快出现：MAX30003 在产样，问题在 FIFO drain / 读取。
EINT/EOVF 仍少：MAX30003 产样/时钟/配置异常。
```

### 7.4 实验 C4：Normal FIFO read vs Burst read

分别测试：

1. Burst read 32 words；
2. Normal read one word × 32；
3. 只读到 ETAG last 即停止；
4. 固定读 4 / 8 / 16 / 32 words。

记录：

```text
fifo_valid
etag_hist[0..7]
unknown_etag
empty
eovf
spi_error
burst_us_max
```

判断：

```text
Normal read 正常、Burst 异常：Burst 命令/长度/CS 时序问题。
二者都 374：不是 burst 路径单点。
```

### 7.5 实验 C5：512 / 256 / 128 比例测试

配置：

```text
512 SPS
256 SPS
128 SPS
```

每组 ECG-only，无 SD，只计数。

判断：

| 结果 | 结论 |
|---|---|
| 512→374, 256→187, 128→94 | 比例性时钟/配置问题 |
| 512→374, 256→256, 128→128 | 高速 drain/吞吐问题 |
| 512→374, 256→126, 128→63 | 约半速/时钟问题 |
| 所有都卡 374 以下 | 软件限速/调度问题 |

### 7.6 实验 C6：FCLK 实测闭环

在 MAX30003 CLK 引脚测：

```text
频率
毛刺
过冲/下冲
上升沿/下降沿
MIC OFF
MIC ON
SAI DMA ON/OFF
```

注意：测芯片脚，不只测 PA8。

判断：

```text
32.768k 干净：FCLK 基本排除。
37k 或毛刺严重：FCLK/串扰仍是主因候选。
```

### 7.7 实验 C7：寄存器读回

初始化后和录制中每 5s 读：

```text
STATUS
INFO
CNFG_GEN
CNFG_ECG
MNGR_INT
EN_INT
```

解码：

```text
FMSTR
RATE
EFIT
EINT enable
EOVF enable
PLLINT
```

512 SPS 期望：

```text
FMSTR = 0
RATE = 0
```

---

## 8. 四模态最终架构建议

### 8.1 SensorTask

职责：

```text
ECG FIFO drain
PPG/IMU 轻量读取或触发
数据入队
不得直接写 SD
不得直接 f_sync/f_close
```

### 8.2 MultiSensor_SDWriter

职责：

```text
消费 ECG/PPG/IMU queue
批量写 CSV
短 mutex timeout
失败可 drop 或延后
不得阻塞 SensorTask
```

### 8.3 AudioTask

职责：

```text
SAI DMA 控制
音频 block 入 ring buffer
不高于 SensorTask
音频可丢
```

### 8.4 AudioSDWriter

如果后续音频写入仍有扰动，新增：

```text
低优先级 AudioSDWriter
批量写 WAV
录制中不频繁 f_sync
停止时回写 header
```

### 8.5 DebugLogWriter

当前架构保留：

```text
RAM ring buffer
低优先级 writer
短 mutex timeout
close-after-flush
日志可丢
```

---

## 9. 版本冻结策略

### 9.1 v0.1：四模态稳定版

目标：

```text
Case F 稳定 374~391Hz
ECG 写入可靠
MIC/PPG/IMU 有数据
```

冻结条件：

```text
30s × 3 通过
5min × 1 通过
```

### 9.2 v0.2：四模态长测版

目标：

```text
30min 四模态稳定
SD 文件完整
无 FR_INVALID_OBJECT
无 HardFault
```

### 9.3 v0.3：ECG 512Hz 攻关版

目标：

```text
ECG-only 512Hz
四模态 >= 500Hz
```

只有 v0.1 / v0.2 通过后，才进入 v0.3。

---

## 10. 给 Agent 的当前执行目标

请按以下顺序执行：

```text
1. 固化当前 Case F 修复成果，不要回退 SD DebugLog / AudioTask 修复。
2. 建立 v0.1 四模态稳定版配置。
3. 跑 30s × 3 Case F，确认 ECG SPS、写入数、pack、queue、SD 文件完整。
4. 增加 SensorTask 分段耗时，定位 diag_max_gap_recording_ms=1003ms。
5. 若四模态稳定，通过 v0.1。
6. 跑 5min 中测，通过后冻结 v0.2。
7. 然后启动 ECG 512Hz 专项 C1~C7。
```

不要把 Case A 374→512 和四模态稳定性混在一个补丁里。
