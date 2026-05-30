# 四模态真实时间窗采集与落盘实验任务书

## 0. 给 Agent 的总目标

请基于当前工程实现并验证 **四模态真实时间窗采集与落盘功能**。

本任务的目标不是继续强行把 ECG 修到标称 512Hz，也不是继续排查 MAX30003 PLL/FCLK。当前目标是：

> 让 ECG、PPG、IMU、MIC 四模态数据全部绑定到同一个真实时间轴上，确保即使 ECG 实际采样率为约 374~391Hz 且存在波动，长时间录制后仍然可以准确对齐、回放和分析。

设备具备当前日期和时间。  
本次实验假定设备当前时间为：

```text
2026-05-30 21:00:00
```

未来该时间会由外部时间源同步。

每次实验录制时长必须不少于 60 秒。  
推荐第一阶段统一使用 90 秒。

---

## 1. 背景

当前系统已经实现四模态采集：

```text
ECG: MAX30003
PPG: MAX30102
IMU: ICM20948
MIC: SAI + DMA
```

当前已知 ECG 配置为 512 SPS，但实际有效采样率约为：

```text
374~391 Hz
```

并且长期运行中可能存在轻微波动。

因此，严禁在数据分析阶段继续使用：

```text
ECG sample_index / 512
```

作为 ECG 时间轴。

必须改为：

```text
以真实 session 时间窗 + block 起止 tick + block sample_count 重建 ECG 时间轴。
```

---

## 2. 核心原则

### 2.1 统一时间基准

所有模态必须使用同一个系统时间基准。

建议使用：

```text
RTC absolute datetime + FreeRTOS/HAL tick_ms
```

其中：

```text
RTC / device datetime：用于绝对日期时间
tick_ms：用于录制过程中的高一致性相对时间
```

实验开始时记录：

```text
session_start_datetime
session_start_tick_ms
```

实验结束时记录：

```text
session_end_datetime
session_end_tick_ms
duration_ms
```

### 2.2 四模态必须可映射到同一时间窗

每个模态都必须能回答：

```text
该数据属于哪个 session？
该数据从什么时候开始？
该数据到什么时候结束？
该数据有多少样本？
该数据实际平均采样率是多少？
```

### 2.3 ECG 不再假设固定 512Hz

ECG 文件必须明确写入：

```text
configured_sps = 512
actual_avg_sps = measured
timebase = block_timestamp_interpolated
```

### 2.4 长时间对齐不能依赖理论采样率

8 小时录制时，如果 ECG 实际约 380Hz，却按 512Hz 解释，时间轴会严重压缩。  
因此必须使用真实时间戳或 block 时间窗。

---

## 3. 推荐 SD 卡目录结构

每次实验创建独立 session 目录。

假定开始时间为：

```text
2026-05-30 21:00:00
```

目录名：

```text
/REC/20260530_210000/
```

目录内文件：

```text
session.json
ecg_blocks.csv
ecg_raw.bin 或 ecg_samples.csv
ppg.csv
imu.csv
audio.wav
audio_blocks.csv
diag_summary.txt
```

如果当前工程不方便写 JSON，可以先写：

```text
session.txt
```

但字段必须完整。

---

## 4. session 元数据设计

### 4.1 session.json 推荐格式

```json
{
  "session_id": "20260530_210000",
  "device_time_start": "2026-05-30T21:00:00",
  "device_time_end": "2026-05-30T21:01:30",
  "start_tick_ms": 123456,
  "end_tick_ms": 213456,
  "duration_ms": 90000,
  "firmware_commit": "unknown",
  "device_id": "unknown",
  "time_source": "device_datetime_plus_tick_ms",
  "modalities": {
    "ecg": {
      "enabled": true,
      "configured_sps": 512,
      "actual_samples": 34560,
      "actual_avg_sps": 384.0,
      "timebase": "block_timestamp_interpolated"
    },
    "ppg": {
      "enabled": true,
      "configured_sps": 100,
      "actual_samples": 9000,
      "actual_avg_sps": 100.0,
      "timebase": "sample_tick_ms"
    },
    "imu": {
      "enabled": true,
      "configured_sps": 100,
      "actual_samples": 9000,
      "actual_avg_sps": 100.0,
      "timebase": "sample_tick_ms"
    },
    "mic": {
      "enabled": true,
      "configured_sps": 16000,
      "actual_samples": 1440000,
      "actual_avg_sps": 16000.0,
      "timebase": "audio_block_timestamp"
    }
  },
  "errors": {
    "ecg_eovf_count": 0,
    "ecg_pack_drop": 0,
    "ecg_queue_fail": 0,
    "mic_drop_blocks": 0,
    "ppg_drop": 0,
    "imu_drop": 0,
    "debug_ring_drops": 0,
    "sd_write_errors": 0
  }
}
```

### 4.2 必须包含的 session 字段

无论 JSON 还是 TXT，必须包含：

```text
session_id
device_time_start
device_time_end
start_tick_ms
end_tick_ms
duration_ms
firmware_commit
device_id
time_source
enabled_modalities
configured_sps_per_modality
actual_samples_per_modality
actual_avg_sps_per_modality
drop/error counters
```

---

## 5. ECG 数据格式设计

### 5.1 推荐 ECG block 索引文件

文件：

```text
ecg_blocks.csv
```

字段：

```csv
block_id,tick_start_ms,tick_end_ms,sample_count,configured_sps,actual_block_sps,first_sample_index,last_sample_index,raw_offset_bytes
```

示例：

```csv
0,123456,123520,25,512,390.6,0,24,0
1,123520,123584,24,512,375.0,25,48,50
```

### 5.2 ECG 原始数据

推荐二进制：

```text
ecg_raw.bin
```

格式：

```text
int16_t ECG samples
little-endian
continuous samples
```

如果当前工程更容易 CSV，也可以：

```text
ecg_samples.csv
```

但长期录制推荐 binary。

### 5.3 ECG block 时间重建公式

对每个 block：

```text
block_duration_ms = tick_end_ms - tick_start_ms
```

第 `i` 个样本时间：

```text
sample_time_ms = tick_start_ms + i * block_duration_ms / sample_count
```

如果 `sample_count == 1`：

```text
sample_time_ms = tick_start_ms
```

注意：

```text
不要使用 sample_index / 512 作为 ECG 时间。
```

---

## 6. MIC 数据格式设计

### 6.1 WAV 文件

保留：

```text
audio.wav
```

WAV header 中可以保留 nominal sample rate，例如 16000Hz。

### 6.2 audio block 索引文件

新增：

```text
audio_blocks.csv
```

字段：

```csv
block_id,tick_start_ms,tick_end_ms,sample_count,nominal_sps,byte_offset,bytes_written,drop_count
```

示例：

```csv
0,123460,123492,512,16000,44,1024,0
1,123492,123524,512,16000,1068,1024,0
```

### 6.3 MIC 时间重建

MIC 每个 block 内按 nominal sample rate 插值：

```text
sample_time_ms = tick_start_ms + i * 1000 / nominal_sps
```

如果 `tick_end_ms` 与 nominal duration 不一致，以 `tick_start_ms` 为锚点，同时在后处理中检查 drift。

---

## 7. PPG 数据格式设计

PPG 采样率较低，建议每个样本直接带 tick。

文件：

```text
ppg.csv
```

字段：

```csv
tick_ms,red,ir,green,status
```

示例：

```csv
123456,12345,23456,0,0
123466,12348,23460,0,0
```

如果 PPG 是 block 读取，也可以写 block：

```csv
block_id,tick_start_ms,tick_end_ms,sample_count,first_sample_index,last_sample_index
```

---

## 8. IMU 数据格式设计

文件：

```text
imu.csv
```

字段：

```csv
tick_ms,ax,ay,az,gx,gy,gz,mx,my,mz,status
```

示例：

```csv
123456,0.01,0.02,0.98,0.1,0.0,-0.1,10,20,30,0
```

如果磁力计频率较低，可允许 `mx/my/mz` 为空或复用最近值，但必须在 status 中标记。

---

## 9. 录制流程设计

### 9.1 创建 session

录制开始时：

```text
1. 获取 device datetime，例如 2026-05-30 21:00:00。
2. 读取 tick_ms。
3. 创建 session_id = YYYYMMDD_HHMMSS。
4. 创建 SD 目录 /REC/YYYYMMDD_HHMMSS/。
5. 写入 session_start 元数据。
6. 打开各模态文件。
7. 开启四模态采集。
```

### 9.2 录制过程

录制期间：

```text
ECG:
  每个 ECG block 记录 tick_start_ms / tick_end_ms / sample_count。
PPG:
  每个样本或 block 记录 tick_ms。
IMU:
  每个样本或 block 记录 tick_ms。
MIC:
  每个 DMA block 记录 tick_start_ms / tick_end_ms / sample_count。
```

禁止在实时路径中做长时间 SD debug 写入。

### 9.3 停止 session

录制结束时：

```text
1. 停止四模态采集。
2. flush writer queue。
3. 回写 WAV header。
4. 写 session_end 元数据。
5. 计算每个模态 actual_avg_sps。
6. 写 diag_summary.txt。
7. 关闭所有文件。
```

---

## 10. 实验 1：90 秒四模态时间窗基线

### 10.1 目的

验证四模态数据都能落到同一个真实时间窗。

### 10.2 设置

```text
假定 device time = 2026-05-30 21:00:00
录制时长 = 90 秒
ECG enabled, configured 512 SPS
PPG enabled
IMU enabled
MIC enabled
SD DebugLog 使用 RAM ring buffer
```

### 10.3 验收标准

```text
/REC/20260530_210000/ 目录存在
session.json 或 session.txt 存在
ecg_blocks.csv 存在
ecg_raw.bin 或 ecg_samples.csv 存在
ppg.csv 存在
imu.csv 存在
audio.wav 存在
audio_blocks.csv 存在
duration_ms 接近 90000
ECG written == ECG sample == FIFO valid
ECG block tick 单调递增
MIC block tick 单调递增
PPG tick 单调递增
IMU tick 单调递增
```

---

## 11. 实验 2：连续 5 分钟四模态时间窗稳定性

### 11.1 目的

验证较长时间录制下，各模态时间窗不会明显漂移。

### 11.2 设置

```text
录制时长 = 5 分钟
四模态全开
```

### 11.3 验收标准

```text
duration_ms 接近 300000
ECG actual_avg_sps 合理，例如 374~391Hz
ECG sample count 与 duration 对应
MIC block 覆盖时间接近 session duration
PPG/IMU 覆盖时间接近 session duration
无严重 SD write error
无 ECG pack drop
无 queue submit fail
```

---

## 12. 实验 3：长时间时间轴漂移模拟

### 12.1 目的

验证后处理脚本不依赖 ECG=512Hz，而是使用 block timestamp。

### 12.2 输入

使用实验 1 或实验 2 生成的数据。

### 12.3 后处理脚本必须输出

```text
Session ID
device_time_start
device_time_end
duration_ms

ECG:
  first timestamp
  last timestamp
  sample_count
  configured_sps
  actual_avg_sps
  reconstructed_duration

PPG:
  first timestamp
  last timestamp
  sample_count
  actual_avg_sps

IMU:
  first timestamp
  last timestamp
  sample_count
  actual_avg_sps

MIC:
  first timestamp
  last timestamp
  sample_count
  actual_avg_sps

Common overlap window:
  start datetime
  end datetime
  duration
```

### 12.4 验收标准

```text
ECG reconstructed duration 与 session duration 一致
四模态 common overlap window 正常
不使用 ECG sample_index / 512 推导全局时间
```

---

## 13. 实验 4：60 秒最小录制验收

### 13.1 目的

作为每次固件修改后的快速验收实验。

### 13.2 设置

```text
录制时长 >= 60 秒
四模态全开
```

### 13.3 通过标准

```text
session 文件完整
四模态文件完整
ECG 时间窗可重建
MIC 时间窗可重建
PPG/IMU tick 单调
无 HardFault
无 FR_INVALID_OBJECT
无严重 SD error
```

---

## 14. 后处理重采样建议

后处理时建议统一映射到目标分析频率，例如：

```text
ECG: 根据真实时间轴重采样到 256Hz 或 384Hz
PPG: 根据 tick 插值到 100Hz
IMU: 根据 tick 插值到 100Hz
MIC: 保持原 WAV sample rate，使用 audio block tick 校准起点
```

不要把 ECG 硬解释为 512Hz。

---

## 15. 时间同步策略

当前假定时间：

```text
2026-05-30 21:00:00
```

未来同步后，应记录：

```text
time_sync_source
last_sync_datetime
last_sync_tick_ms
time_sync_valid
```

如果录制过程中发生时间同步，不要修改历史 tick。  
推荐：

```text
绝对时间 = session_start_datetime + (tick_ms - session_start_tick_ms)
```

这样录制过程中即使 RTC 被校准，也不会让数据时间轴跳变。

---

## 16. 诊断字段

session 结束时必须写：

```text
ecg_eovf_count
ecg_fifo_empty_count
ecg_unknown_etag
ecg_pack_drop
ecg_queue_fail
ecg_writer_blocks
mic_drop_blocks
audio_mutex_timeouts
ppg_drop
imu_drop
debug_ring_drops
sd_write_errors
max_gap_recording_ms
```

---

## 17. 最终提交物

请提交：

```text
1. 当前 commit
2. 修改文件列表
3. session 元数据格式
4. ECG block 格式
5. MIC block 格式
6. PPG/IMU 文件格式
7. 90 秒实验输出目录截图或文件列表
8. session.json / session.txt 示例
9. 5 分钟实验结果表
10. 后处理脚本输出
11. 最终判断：四模态时间窗是否可对齐
```

最终判断格式：

```text
最终判断：

证据：
1.
2.
3.

已实现：
1.
2.
3.

仍需改进：
1.
2.
3.

下一步：
1.
2.
3.
```

---

## 18. 给 Agent 的简短执行目标

请实现四模态带真实时间窗落盘功能。设备当前时间假定为 2026-05-30 21:00:00，每次实验录制不少于 60 秒，推荐 90 秒。录制开始时创建 `/REC/YYYYMMDD_HHMMSS/` session 目录，并写入 session 元数据，包括绝对开始时间、结束时间、start_tick_ms、end_tick_ms、duration_ms、四模态启用状态、配置采样率、实际样本数和实际平均采样率。ECG 不得再用 sample_index/512 作为时间轴，必须按 ECG block 的 tick_start_ms、tick_end_ms、sample_count 重建真实时间。MIC 需要 audio_blocks.csv，PPG/IMU 每个样本或 block 必须带 tick_ms。最终通过 90 秒和 5 分钟四模态实验，证明 ECG、PPG、IMU、MIC 都能映射到同一个真实时间窗，长时间录制后不会因 ECG 实际采样率波动而时间窗对不上。
