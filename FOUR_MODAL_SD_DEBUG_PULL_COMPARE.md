# 四模态 SD Debug 拉取比对实验任务书

## 0. 给 Agent 的总目标

请基于当前工程实现并执行 **四模态 SD Debug 拉取比对实验**。

本任务不是让 ECG、PPG、IMU、MIC 各自做互相孤立的闭环，而是：

> 使用 SD Debug / Session Summary / Modality Summary 作为统一审计账本，在每次录制结束后，从 SD 卡拉取四模态数据文件与 debug/summary 文件，逐项比对四个模态的采集计数、入队计数、写盘计数、文件实际记录数、时间覆盖范围和实际采样率。

核心目标：

1. 四模态同时采集；
2. 四模态同时落盘；
3. SD Debug / Summary 记录每个模态的关键计数；
4. 录制结束后从 SD 卡拉取：
   - session metadata；
   - modality summary；
   - debug summary；
   - ECG 文件；
   - PPG 文件；
   - IMU 文件；
   - MIC WAV + audio block index；
5. 使用 PC 端脚本或人工表格进行四模态交叉比对；
6. 确认 debug 计数、writer 计数、文件实际行数/大小一致；
7. 确认四模态都覆盖同一个真实 session 时间窗；
8. 确认每次实验录制不少于 60 秒，推荐 90 秒。

设备当前时间假定为：

```text
2026-05-30 21:00:00
```

未来该时间会由外部时间源同步。

---

## 1. 本任务和上一版的区别

上一版容易误解为：

```text
ECG 自己闭环
PPG 自己闭环
IMU 自己闭环
MIC 自己闭环
```

本任务要求改成：

```text
SD Debug / Summary 作为统一审计账本
录制后拉取 SD 文件
四个模态一起比对
用文件实际内容验证 debug 计数
用 debug 计数验证采集链路
用 session 时间窗验证四模态对齐
```

也就是说，最终验收不是“某个模态自己说自己正常”，而是：

```text
Debug 计数 == writer 计数 == SD 文件实际记录数/大小
四模态时间窗互相重叠
四模态实际采样率都可计算
```

---

## 2. 绝对禁止事项

### 2.1 禁止只看 debug 计数

SD Debug 只是审计账本，不能单独作为最终证据。

必须拉取 SD 数据文件并比对。

### 2.2 禁止只看文件存在

文件存在不代表数据完整。

必须验证：

```text
文件行数
文件大小
sample_count
block_count
timestamp/tick 单调性
actual_avg_sps
```

### 2.3 禁止只验证 ECG

必须同时比对：

```text
ECG
PPG
IMU
MIC
```

### 2.4 禁止 ECG 使用 sample_index / 512 作为时间轴

ECG 当前实际约 374~391Hz，且可能波动。

ECG 时间轴必须使用：

```text
block_start_tick_ms
block_end_tick_ms
sample_count
```

### 2.5 禁止实时路径同步写 SD debug

SD Debug 必须继续使用 RAM ring buffer + 低优先级 DebugLogWriter。  
禁止回退到实时任务中 `f_open/f_write/f_sync/f_close`。

---

## 3. Session 目录结构

每次录制创建：

```text
/REC/YYYYMMDD_HHMMSS/
```

例如：

```text
/REC/20260530_210000/
```

目录下至少包含：

```text
session.json 或 session.txt
modality_summary.csv
debug_summary.txt
ecg_blocks.csv
ecg_raw.bin 或 ecg_samples.csv
ppg.csv
imu.csv
audio.wav
audio_blocks.csv
```

可选：

```text
debug_log.txt
sd_debug_diag.csv
writer_diag.csv
```

---

## 4. SD Debug / Summary 作为统一审计账本

### 4.1 session summary 必须记录

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
recording_mode
enabled_modalities
```

### 4.2 每个模态必须记录统一字段

`modality_summary.csv` 必须包含：

```csv
modality,enabled,configured_sps,capture_count,queue_submit_ok,queue_submit_fail,writer_get_count,written_count,file_count,first_tick_ms,last_tick_ms,duration_ms,actual_avg_sps,drop_count,error_count,status
```

示例：

```csv
ECG,1,512,11236,11,0,11,11236,11236,123456,213456,90000,374.5,0,7,OK
PPG,1,100,9000,90,0,90,9000,9000,123460,213450,89990,100.0,0,0,OK
IMU,1,100,9000,90,0,90,9000,9000,123462,213452,89990,100.0,0,0,OK
MIC,1,16000,1440000,2813,0,2813,1440000,1440000,123458,213458,90000,16000.0,0,0,OK
```

字段定义：

| 字段 | 含义 |
|---|---|
| `capture_count` | 驱动层实际采到的样本数 |
| `queue_submit_ok` | 成功提交到 writer/队列的 block 或 sample 数 |
| `queue_submit_fail` | 队列提交失败次数 |
| `writer_get_count` | writer 从队列取出的 block 数 |
| `written_count` | writer 认为写入成功的样本数 |
| `file_count` | PC 拉取文件后实际解析出的样本数 |
| `first_tick_ms` | 该模态首个样本/block tick |
| `last_tick_ms` | 该模态最后样本/block tick |
| `actual_avg_sps` | 按真实时间窗计算的平均采样率 |
| `drop_count` | 明确丢弃数 |
| `error_count` | 读失败、写失败、overflow、timeout 等 |
| `status` | OK/WARN/FAIL |

---

## 5. 四模态数据文件要求

### 5.1 ECG 文件

推荐：

```text
ecg_blocks.csv
ecg_raw.bin
```

`ecg_blocks.csv`：

```csv
block_id,tick_start_ms,tick_end_ms,sample_count,configured_sps,first_sample_index,last_sample_index,raw_offset_bytes
```

比对规则：

```text
sum(ecg_blocks.sample_count) == modality_summary[ECG].capture_count
ecg_raw.bin 文件大小 / sizeof(sample) == modality_summary[ECG].file_count
modality_summary[ECG].written_count == file_count
tick_start_ms / tick_end_ms 单调递增
```

### 5.2 PPG 文件

`ppg.csv`：

```csv
tick_ms,red,ir,green,status
```

或者 block 格式：

```csv
block_id,tick_start_ms,tick_end_ms,sample_count,first_sample_index,last_sample_index
```

比对规则：

```text
ppg.csv 数据行数 == modality_summary[PPG].file_count
file_count == written_count
tick_ms 单调递增
actual_avg_sps = file_count * 1000 / (last_tick - first_tick)
```

### 5.3 IMU 文件

`imu.csv`：

```csv
tick_ms,ax,ay,az,gx,gy,gz,mx,my,mz,status
```

如果磁力计频率不同，可拆分：

```text
imu_accel_gyro.csv
imu_mag.csv
```

比对规则：

```text
imu.csv 数据行数 == modality_summary[IMU].file_count
file_count == written_count
tick_ms 单调递增
accel/gyro/mag 有效性由 status 标记
```

### 5.4 MIC 文件

```text
audio.wav
audio_blocks.csv
```

`audio_blocks.csv`：

```csv
block_id,tick_start_ms,tick_end_ms,sample_count,nominal_sps,byte_offset,bytes_written,drop_count
```

比对规则：

```text
sum(audio_blocks.sample_count) == modality_summary[MIC].capture_count 或扣除 drop 后一致
audio.wav 数据区大小 / bytes_per_sample == modality_summary[MIC].file_count
audio_blocks byte_offset + bytes_written 不越界
tick_start_ms / tick_end_ms 单调递增
```

---

## 6. SD Debug 需要新增/确认的计数器

### 6.1 ECG

```text
ecg_fifo_valid_count
ecg_sample_count
ecg_written_count
ecg_pack_blocks
ecg_pack_drop_blocks
ecg_queue_submit_ok
ecg_queue_submit_fail
ecg_writer_get_blocks
ecg_fifo_eovf_count
ecg_fifo_empty_count
ecg_unknown_etag_count
ecg_first_tick_ms
ecg_last_tick_ms
```

### 6.2 PPG

```text
ppg_task_calls
ppg_irq_count
ppg_fifo_reads
ppg_fifo_samples
ppg_samples_queued
ppg_samples_written
ppg_fifo_overflow_count
ppg_read_fail_count
ppg_i2c_error_count
ppg_queue_submit_ok
ppg_queue_submit_fail
ppg_first_tick_ms
ppg_last_tick_ms
```

### 6.3 IMU

```text
imu_task_calls
imu_read_ok_count
imu_read_fail_count
imu_samples_queued
imu_samples_written
imu_i2c_error_count
imu_queue_submit_ok
imu_queue_submit_fail
imu_accel_samples
imu_gyro_samples
imu_mag_samples
imu_first_tick_ms
imu_last_tick_ms
```

### 6.4 MIC

```text
mic_dma_half_count
mic_dma_full_count
mic_blocks_in
mic_blocks_written
mic_blocks_dropped
mic_bytes_captured
mic_bytes_written
mic_sample_count
mic_audio_file_bytes
mic_mutex_timeouts
mic_dma_overrun
mic_first_tick_ms
mic_last_tick_ms
```

### 6.5 SD / Writer 共享诊断

```text
sd_write_errors
sd_mutex_timeouts
csv_writer_blocks
csv_writer_errors
debug_ring_drops
debug_writer_drops
fatfs_last_error
```

---

## 7. 实验流程

### 7.1 实验 A：90 秒四模态基线

设置：

```text
device_time = 2026-05-30 21:00:00
duration = 90s
ECG ON
PPG ON
IMU ON
MIC ON
SD ON
DebugLog ring buffer ON
```

流程：

```text
1. 烧录固件。
2. 设备创建 /REC/20260530_210000/。
3. 连续录制 90 秒。
4. 停止录制并确保所有 writer flush 完成。
5. 断电或取卡。
6. 从 SD 拉取整个 session 目录。
7. PC 端运行 verify 脚本。
8. 输出四模态比对报告。
```

通过标准：

```text
session 文件存在
modality_summary.csv 存在
四模态数据文件均存在
debug summary 存在
ECG debug 计数 == ECG 文件计数
PPG debug 计数 == PPG 文件计数
IMU debug 计数 == IMU 文件计数
MIC debug 计数 == WAV/audio_blocks 文件计数
四模态 tick 单调
四模态有共同 overlap window
```

### 7.2 实验 B：5 分钟四模态稳定性

设置：

```text
duration = 300s
四模态全开
```

重点观察：

```text
采样率是否稳定
debug ring drops 是否过多
MIC drop 是否增加
PPG/IMU 是否被 I2C mutex 影响
SD/FatFS 是否出现错误
```

通过标准：

```text
无 HardFault
无 FR_INVALID_OBJECT
无 SD 掉盘
四模态文件可解析
四模态 file_count 与 summary 一致
共同 overlap window 正常
```

### 7.3 实验 C：30 分钟长测

设置：

```text
duration = 1800s
四模态全开
```

通过标准：

```text
session 正常结束
所有文件完整关闭
summary 与实际文件一致
无时间戳倒退
无不可解释的大 gap
```

---

## 8. 拉取与比对脚本

请实现 PC 端脚本：

```text
tools/verify_four_modal_session.py
```

输入：

```text
/REC/YYYYMMDD_HHMMSS/
```

脚本必须做：

```text
1. 读取 session.json/session.txt
2. 读取 modality_summary.csv
3. 解析 ecg_blocks.csv + ecg_raw.bin
4. 解析 ppg.csv
5. 解析 imu.csv
6. 解析 audio_blocks.csv + audio.wav
7. 计算每个模态 file_count
8. 计算每个模态 first_tick / last_tick
9. 计算每个模态 actual_avg_sps
10. 检查 tick 单调性
11. 检查文件大小与 block index 一致性
12. 检查 debug summary 与文件实际一致性
13. 计算四模态共同 overlap window
14. 输出 PASS/WARN/FAIL
```

输出示例：

```text
Session: 20260530_210000
Duration: 90.000s

ECG:
  summary_capture_count: 33720
  file_count: 33720
  actual_avg_sps: 374.7
  count_match: OK
  tick_monotonic: OK

PPG:
  summary_capture_count: 9000
  file_count: 9000
  actual_avg_sps: 100.0
  count_match: OK
  tick_monotonic: OK

IMU:
  summary_capture_count: 9000
  file_count: 9000
  actual_avg_sps: 100.0
  count_match: OK
  tick_monotonic: OK

MIC:
  summary_capture_count: 1440000
  wav_file_count: 1440000
  block_index_match: OK
  actual_avg_sps: 16000.0

Common overlap:
  start_tick_ms: 123500
  end_tick_ms: 213400
  duration_ms: 89900

RESULT: PASS
```

---

## 9. PASS / WARN / FAIL 标准

### 9.1 FAIL

任一情况出现即 FAIL：

```text
session 文件缺失
modality_summary.csv 缺失
任一启用模态数据文件缺失
ECG written_count != ECG file_count
PPG written_count != PPG file_count 且无 drop 解释
IMU written_count != IMU file_count 且无 drop 解释
MIC wav size 与 audio_blocks 不一致
tick 非单调
四模态共同 overlap window 不存在
FR_INVALID_OBJECT
SD 掉盘
HardFault
```

### 9.2 WARN

```text
ECG SPS < 360Hz 但计数一致
PPG/IMU SPS 偏差 > 5%
MIC 有 drop 但被记录
Debug ring drops 增多
某模态时间覆盖略短
```

### 9.3 PASS

```text
四模态文件完整
summary 与实际文件一致
tick 单调
共同 overlap window 存在
采样率可解释
无未解释 drop/error
```

---

## 10. 最终提交物

请提交：

```text
1. 当前 commit
2. 修改文件列表
3. session.json/session.txt 示例
4. modality_summary.csv 示例
5. debug_summary.txt 示例
6. 90 秒 session 目录文件列表
7. 90 秒 verify 脚本输出
8. 5 分钟 verify 脚本输出
9. 如执行 30 分钟长测，提交 verify 输出
10. 四模态采样率比对表
11. 四模态文件计数 vs debug 计数比对表
12. 共同 overlap window
13. 最终判断
```

最终判断格式：

```text
最终判断：

ECG:
  capture/debug/file 是否一致：
  actual_avg_sps：
  状态：

PPG:
  capture/debug/file 是否一致：
  actual_avg_sps：
  状态：

IMU:
  capture/debug/file 是否一致：
  actual_avg_sps：
  状态：

MIC:
  capture/debug/file 是否一致：
  actual_avg_sps：
  状态：

共同时间窗：
  start:
  end:
  duration:

总体结论：
PASS / WARN / FAIL

证据：
1.
2.
3.

仍需改进：
1.
2.
3.
```

---

## 11. 给 Agent 的简短执行目标

请实现并执行四模态 SD Debug 拉取比对实验。设备当前时间假定为 2026-05-30 21:00:00，每次录制不少于 60 秒，推荐 90 秒。本任务不是让 ECG、PPG、IMU、MIC 各自孤立闭环，而是用 SD Debug / session summary / modality_summary 作为统一审计账本，录制结束后从 SD 卡拉取四模态数据文件并逐项比对。必须生成 `/REC/YYYYMMDD_HHMMSS/` session 目录，包含 session.json、modality_summary.csv、debug_summary.txt、ecg_blocks.csv/ecg_raw.bin、ppg.csv、imu.csv、audio.wav/audio_blocks.csv。PC 端脚本必须解析这些文件，验证每个模态的采集计数、入队计数、写盘计数、文件实际记录数、first_tick_ms、last_tick_ms、actual_avg_sps 是否一致，并计算四模态共同 overlap window。最终通过 90 秒和 5 分钟四模态实验，证明 SD debug 计数与四模态实际文件内容一致，且 ECG、PPG、IMU、MIC 都能映射到同一个真实时间窗。
