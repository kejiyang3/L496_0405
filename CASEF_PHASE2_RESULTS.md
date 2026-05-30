# Case F Phase 2: CSV/SD Blocking + INTB/EXTI ????

**Date**: 2026-05-29

---

## P0: IRQ Priority ??

| Channel | Peripheral | Priority | Notes |
|---------|-----------|----------|-------|
| DMA2_Channel6 | **SAI1 (MIC)** | **5** | Circular DMA, VERY_HIGH |
| DMA2_Channel4 | SDMMC1 RX | 6 | SD card read |
| DMA2_Channel5 | SDMMC1 TX | 6 | SD card write |
| **EXTI9_5** | **ECG INTB** | **7** | < SAI + SDMMC DMA |
| DMA1_Channel4 | SAI related | 8 | |
| DMA1_Channel5 | SAI related | 8 | |
| DMA1_Channel3 | SPI1 TX (LCD) | 10 | |
| EXTI3 | Touch | 11 | |

**??**: SAI DMA (5) ????? EXTI9_5 (7)?????? INTB ? Case A ??? ~1 Hz???????????

### ?? osWaitForever
- `ecg_sd_logger.c:138,212` ? ? ECG SD writer (???)
- `sd_sensor_logger.c:102,133,223,266` ? PPG Diag writer (ISOLATE_TASKS=1 ???)
- `sd_wav_test.c:130` ? ????

---

## ??????

| ?? | CASE | CSV | Debug | All SD | ECG Rate | task_calls | eovf | notify_wakes | exti_raw | intb_low | status_eint |
|------|------|-----|-------|--------|----------|------------|------|-------------|----------|----------|-------------|
| **Case A** | 1 | ON | ON | ON | **374.7 Hz** | 2872 | 8 | 33 (1.1Hz) | 34 | 34 | 26 |
| **P3 baseline** | 6 | ON | ON | ON | **130.7 Hz** | 1010 | 22 | 65 | ? | ? | ? |
| **P2 (no CSV)** | 6 | OFF | ON | ON | **49.6 Hz** | 399 | 26 | 52 | 53 | 51 | 25 |
| **P3 (no debug)** | 6 | ON | OFF | ON | **375.0 Hz** ? | 2879 | 7 | 20 | 20 | 19 | 12 |
| **P4 (no SD)** | 6 | OFF | OFF | OFF | **374.3 Hz** ? | 2878 | 8 | 16 | 17 | 17 | 9 |

---

## ????

### 1. SD Debug Log ? Case F ???? ???

?? SD debug log ??ECG ? 130.7 Hz **????? 375 Hz**??? Case A??

`SD_DebugLog_WriteLine()` ???????
1. ?? `Mtx_SDCardHandle` (?? FatFS ??? 50ms)
2. `f_open` ? `f_write` ? `f_sync` ? `f_close`
3. ???

SensorTask ? 250ms ?? `EN_MIC_ON_BY_DIAG` ???????? `DiagLog_Run` ?????? SD ??? 45ms??? CPU ?????SensorTask ? `MAX30003_Task()` ????? 96 Hz (Case A) ?? 33 Hz (Case F)?FIFO ??????????

### 2. INTB/EXTI ??? Case ??? ~1 Hz ??

| Case | notify_wakes/30s | exti_raw/30s | ECG Rate |
|------|-----------------|-------------|----------|
| A | 33 (1.1 Hz) | 34 | 374.7 Hz |
| P3 no-debug | 20 | 20 | 375.0 Hz |
| P4 no-SD | 16 | 17 | 374.3 Hz |

INTB ????????? **~1 Hz**?????? 374 Hz?? ECG ????????? **SensorTask ?? 8ms ????** ? ?? INTB ????????? `MAX30003_Task()` ?? FIFO?

**??**: INTB ???? MAX30003 ??/???????? Case F ??????

### 3. CSV Writer ?????? ??

P2 (?? CSV writer) ? ECG ?? 49.6 Hz??????
- CSV writer ???? MSWriter ???????
- `ecg_sample_count` ??????????? vs ??????
- ??????

---

## ????

| ?? | ?? |
|------|------|
| SD debug log ?? SensorTask | ? **??????** |
| INTB/EXTI ??? | ?? ??? Case ??? (~1Hz)???/?????? Case F ???? |
| SAI DMA ????? EXTI | ? ?? ? Case A ? INTB ??? 1 Hz |
| CSV writer ?? | ? ?? ? ?????? |
| Audio f_write ?? | ? ?? ? F1/F2 ???? |

---

## ????

### P0: SD Debug Log ????

```c
// ??A: ?? SensorTask ?? SD debug log
// ??B: RAM ring buffer????????
// ??C: ?? debug log ?? (250ms?5000ms)
```

### P1: INTB ????

- ?? MAX30003 CNFG_GEN ? INTB ??
- ?? INTB ???"?? sample ready"??"FIFO threshold"
- ?????? PB6 ????

### P2: Case A 374?512 Hz

? Case F ??????????????

---

## P0 修复实施: SD Debug Log 非阻塞化 (2026-05-29 实施)

### 修改文件清单

| 文件 | 修改类型 | 说明 |
|------|---------|------|
| `Core/Inc/sd_debug_log.h` | 新增声明 | 添加 DebugLogWriter_Task, RequestStopFlush, IsFlushComplete |
| `Core/Src/sd_debug_log.c` | 重构 | RAM ring buffer + 低优先级 writer task (先前已完�? |
| `Core/Src/freertos.c` | 新增任务 + 移除阻塞日志 | 注册 DebugLogWriterTask, 移除 250ms EN_MIC 日志 |

### 架构设计

```
生产�?(任意任务/ISR)
  �?  �?ring_push()  �?非阻�? O(1)
  �?┌──────────────────────�?�? RAM Ring Buffer     �?�? 32 slots × 192 bytes�?�? = 6KB               �?└──────────────────────�?  �?  �?ring_pop()  �?�?Writer Task 消费
  �?DebugLogWriter_Task
  Priority: osPriorityLow
  �?  �?批量写入 (8 lines/batch)
  �?Mutex timeout: 10ms
  �?SD Card (FatFS)
```

### 关键修改细节

#### 1. sd_debug_log.h �?新增声明

```c
void DebugLogWriter_Task(void *argument);
void SD_DebugLog_RequestStopFlush(void);
uint8_t SD_DebugLog_IsFlushComplete(void);
```

#### 2. freertos.c �?任务注册

```c
/* Definitions for Task_DebugLogWriter */
osThreadId_t Task_DebugLogWriterHandle;
const osThreadAttr_t Task_DebugLogWriter_attributes = {
  .name = "Task_DebugLogWriter",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityLow,
};

// 创建 (在所�?SD writer 之后):
Task_DebugLogWriterHandle = osThreadNew(DebugLogWriter_Task, NULL, 
    &Task_DebugLogWriter_attributes);
if (Task_DebugLogWriterHandle == NULL) g_task_create_error |= (1UL << 9);
```

#### 3. freertos.c �?移除高频阻塞日志

```c
// 250ms 轮询循环�? 注释�?
// SD_DebugLog_WriteLine("EN_MIC_ON_BY_DIAG"); -- removed: too frequent

// 保留初始化阶段的单次日志 (不阻�?
```

#### 4. freertos.c �?停止时触�?flush

```c
// �?SD_DebugLog_WriteSessionSummary() 之前:
SD_DebugLog_RequestStopFlush();
```

### 刷新策略

| 触发条件 | 动作 |
|---------|------|
| Ring buffer > 50% �?| 立即批量写入 8 �?|
| 距上次写�?> 1s | 批量写入 8 �?|
| 录制停止 | 全量 flush + f_close |

### Mutex 策略

- DebugLogWriter �?**osPriorityLow** 运行, 低于 SensorTask (osPriorityAboveNormal) �?AudioTask (osPriorityNormal1)
- SD mutex 超时�?**10ms**, 失败则下个周期重�?- 不阻塞任何实时采集路�?
### 编译验证

```
[8/9] Building C object ... freertos.c.obj
[9/9] Linking C executable L496_0405.elf
Memory region         Used Size  Region Size  %age Used
             RAM:      225944 B       256 KB     86.19%
            RAM2:           0 B        64 KB      0.00%
           FLASH:      399792 B       512 KB     76.25%
```

�?编译通过, 无新增错�?
### 验收标准 (待硬件测�?

| 指标 | 修复�?(Case F) | 目标 (P3 no-debug 基线) |
|------|----------------|------------------------|
| ECG SPS | 130.7 Hz | ~375 Hz |
| diag_task_calls | 1010 | ~2870 |
| fifo_eovf_count | 22 | ~7-8 |
| diag_max_gap_ms | ~1008 | < 50 |

### 任务优先级最终状�?
| 任务 | 优先�?|
|------|--------|
| SensorTask | osPriorityAboveNormal |
| AudioTask | osPriorityNormal1 |
| MultiSensor_SDWriter | osPriorityNormal |
| DebugLogWriter | **osPriorityLow** �?�?|
| PPGDiagWriter | osPriorityNormal |

### 下一�?
1. 烧录固件到硬�?2. 复测 Case F 验收 ECG 是否恢复�?374 Hz
3. 验证 debug log 文件完整�?(无丢�?
4. 监控 ring buffer drop 统计

---

## P0 闭环测试结果 (2026-05-29 20:21~20:35)

### 测试方法

1. 烧录 Case F 固件 (CASE=6, 完整 MIC + SD 写入)
2. OpenOCD + GDB 内存检查读取实时计数器
3. sd_debug.hex 拉取 SD �?debug_log.txt / session 文件验证

### Case A 基线 (SD Debug Log 修复�? seq=10)

| 指标 | �?|
|------|-----|
| ECG SPS | **374.7 Hz** |
| duration_ms | 30003 |
| ecg_samples | 11240 |
| ecg_write_ok | 11240 |
| eovf | 8 |
| diag_max_gap_ms | 1010 |

### Case F 测试 (SD Debug Log 非阻塞修复后)

| 指标 | 修复�?| 修复�?(R1) | 修复�?(R2) | 改善 |
|------|--------|------------|------------|------|
| ECG SPS | 130.7 Hz | **~349 Hz** | **~306 Hz** | **2.7x** |
| diag_task_calls | 1010 | 3128 | 3127 | **3.1x** |
| fifo_eovf_count | 22 | 5 | 5 | **78%�?* |
| diag_max_gap_ms | ~1008 | 1002 | 1002 | �?|
| mic_bytes | ? | 8192 | 8192 | �?|
| g_task_create_error | ? | 0 | 0 | �?|

### 250ms EN_MIC 日志确认

debug_log.txt �?`EN_MIC_ON_BY_DIAG` 仅在初始化时出现一�?(tick~3198)�?**不再�?250ms 重复写入**，验证移除成功�?
### 结论

�?**SD Debug Log 非阻塞化修复成功**
- Case F ECG 速率�?130.7 Hz �?~349 Hz (接近 Case A 基线 374 Hz)
- eovf �?22 �?5 (减少 78%)
- diag_task_calls �?1010 �?~3128 (恢复正常)
- DebugLogWriter 优先�?osPriorityLow，不抢占实时采集

⚠️ **残留问题**
- diag_max_gap_ms 仍为 1002ms (来自录制起始 SD mount，非 debug log 阻塞)
- SD 数据写入偶有失败 (ecg_written_count=0，需进一步排�?SD writer)
- Case A 仍为 374 Hz 而非 512 Hz (下一阶段问题)

### 下一�?
1. 排查 SD 写入偶发失败 (MultiSensor writer / FatFS mount)
2. 排查 diag_max_gap_ms 1002ms 根因
3. Case A 374�?12 Hz 优化 (INTB 通知机制 / FCLK)

---

## P1 修复实施: DebugLogWriter 持久文件句柄 �?close-after-flush (2026-05-29)

### 根因

**DebugLogWriter 打开 `debug_log.txt` 后保持打开状�?*。CSV writer 在每次录制开始时调用 `f_mount(&SDFatFS, SDPath, 1)`，虽�?FatFS 文档说同卷重复挂载是安全的，�?`s_ms_file.obj.id` �?`SDFatFS.id` 不再匹配，导致后�?`f_write` 返回 `FR_INVALID_OBJECT (9)`�?
证据链：
1. `s_last_fresult = 9` (FR_INVALID_OBJECT)
2. `s_csv_fopen_fresult = 0` (f_open OK), `s_csv_fsync_fresult = 0` (f_sync OK)
3. 禁用 DebugLogWriter task �?CSV 正常：`ecg_written_count=11473`
4. DebugLogWriter close-after-flush �?CSV 正常

### 修复

`Core/Src/sd_debug_log.c`：每次批�?flush 后立�?`f_close` + `file_open=0`，下�?flush 时重�?`f_open`�?
```c
// 修复�? 文件保持打开
if (flushed > 0) { f_sync(&file); }

// 修复�? 关闭文件，释放文件句�?if (flushed > 0) {
    f_sync(&file);
    f_close(&file);
    file_open = 0;
}
```

### 附加修复

| 文件 | 修改 |
|------|------|
| `Core/Inc/ecg_record_control.h` | +`diag_max_gap_recording_ms`, +`diag_last_recording_tick`, +7 �?pack/submit 计数�?|
| `Core/Src/max3003.c` | recording 阶段 gap 独立追踪，仅�?`ecg_streaming && RECORDING` 时记�?|
| `Core/Src/sd_debug_log.c` | 移除 `StartNewFile` �?`s_sd_debug_mounted=0`，禁�?writer task �?`f_mount`，close-after-flush |

### Case F 最终结�?
| 指标 | 修复�?| R8 (CASE=0) | R8 (CASE=6) | R9 (CASE=6) |
|------|--------|------------|------------|------------|
| ecg_sample_count | 12226 | 11235 | 11231 | 11720 |
| ecg_written_count | **0** �?| **11235** �?| **11231** �?| **11720** �?|
| sd_write_bytes | 76 | 420908 | 341510 | 357130 |
| fifo_eovf_count | 5 | 8 | 7 | 6 |
| mic_bytes | 8192 | �?| 147456 | 147456 |
| diag_max_gap_recording_ms | 1002 | 1002 | 1003 | 1003 |

### diag_max_gap 结论

`diag_max_gap_recording_ms �?1003ms` 确认发生在录制阶段内�?002ms gap 不是启动/挂载造成，而是录制过程�?SensorTask 被阻�?~1 秒。需下一阶段排查（可能来�?SD 文件系统同步操作�?I2C 超时）�?
### 待处�?
1. diag_max_gap_recording_ms = 1003ms 根因（录制阶段被阻塞 1 秒）
2. Case A 374�?12 Hz（INTB / FCLK / MAX30003 配置�?

---

## P1 3轮验�? Case F 完整计数�?(2026-05-29 最�?

| 指标 | R1 | R2 | R3 |
|------|----|----|-----|
| ecg_sample_count | 11727 | 11234 | 11236 |
| ecg_written_count | **11727** | **11234** | **11236** |
| fifo_valid_count | 11727 | 11234 | 11236 |
| ECG SPS (~30s) | ~391 Hz | ~374 Hz | ~375 Hz |
| fifo_eovf_count | 6 | 7 | 7 |
| diag_max_gap_recording_ms | 1003 | 1003 | 1003 |
| ecg_pack_blocks | 11 | 10 | 10 |
| ecg_pack_drop_blocks | 0 | 0 | 0 |
| ecg_queue_submit_ok | 12 | 11 | 11 |
| ecg_queue_submit_fail | 0 | 0 | 0 |
| ecg_writer_get_blocks | 12 | 11 | 11 |
| mic_bytes | 147456 | 147456 | 147456 |
| sd_write_bytes | 354718 | - | - |
| s_ring_drops (DebugLog) | 119 | 112 | 112 |

### 结论

- ecg_written_count == ecg_sample_count: 3/3 �?
- pack pipeline: pack_drop=0, submit_fail=0 �?数据管线健康
- DebugLog ring drop: 112-119 �?(32 slot ring buffer, acceptable)
- diag_max_gap_recording_ms = 1003ms: recording阶段确认存在 ~1s 阻塞

### 最终判�?

| 问题 | 状�?|
|------|------|
| AudioTask 热循环重�?| 已修�?(P0) |
| 优先级反�?| 已修�?(P0) |
| SD Debug Log 阻塞 ECG | 已修�?(P0) |
| ecg_written_count=0 | 已修�?(P1: close-after-flush) |
| diag_max_gap_recording_ms 拆分 | 已完�?(P1) |
| Case F 写入可靠�?| 3/3 轮验证通过 (P1) |
| diag_max_gap=1003ms 根因 | 待下一阶段 |
| Case A 374->512 Hz | 待下一阶段 |

---

## Phase 2: ECG 512Hz Investigation �� C4 & C5 Results (2026-05-30)

### C4: Normal FIFO Read vs Burst Read

| Metric | Burst (baseline) | Normal Single-Word |
|--------|------------------|---------------------|
| `ecg_sample_count` | ~11727 (30s) | 10154 (~20s) |
| `fifo_valid_count` | == ecg_sample | == ecg_sample ? |
| `fifo_sample_count` | == ecg_sample | == ecg_sample ? |
| `diag_task_calls` | ~2872 | 2549 |
| `diag_max_gap_ms` | 1003 | 1002 |
| `fifo_eovf_count` | 7-8 | 8 |
| `etag_hist[0]` (VALID) | 7511 | 7511 |
| `etag_hist[1]` (FAST) | ~0 | 0 |
| `fifo_empty_count` | minimal | **2541** |
| `burst_us_max` | ~1000��s | 1000��s |
| `burst_read_error` | 0 | 0 |
| `fifo_unknown_etag` | 0 | 0 |

**C4 ����:**
- Normal ���ֶ�ȡ�������������� (ecg_sample == fifo_valid == fifo_sample)
- **Normal read �ն�ȡ����** (empty: 2541 vs burst ����Ϊ0) �� ���ֶ�ȡԶ���� Burst
- Burst read �� SPI Ч������������ Normal read
- ���������߾�Ϊ 0��SPI ͨ�ſɿ�
- **Burst read ����Ϊ�Ƽ���ʽ**

### C5: 512 / 256 / 128 SPS ��������

| Metric | 512 SPS | 256 SPS | 128 SPS |
|--------|---------|---------|---------|
| RATE bits | 00 | 01 | 10 |
| CNFG_ECG | 0x025000 | 0x425000 | 0x825000 |
| `ecg_sample_count` | ~11727 (30s) | 778 (~3.2s) | 2443 (~10s) |
| Estimated SPS | **~375 Hz** | **~243 Hz** | **~244 Hz** |
| Expected SPS | 512 | 256 | 128 |
| Ratio (actual/expected) | **73%** | **95%** | **191%?** ?? |
| `diag_task_calls` | ~2872 | 403 | ~490 |
| `fifo_eovf_count` | 7 | 3 | 3 |
| `fifo_empty_count` | low | 400 | **2502** |
| `ecg_written_count` | == sample | 0 (no SD) | 0 (no SD) |

?? **C5-128 �쳣:** 
- 128 SPS �� etag_hist[2]=2440 (VALID+LAST), empty=2502
- ecg_sample=2443 �� etag_hist[0]=3 �� �󲿷��������� ETAG LAST ��־
- 98% �ն�ȡ��: 128 SPS �� FIFO ÿ 8ms ���ۻ� ~1 ����
- ��Ҫ����ʱ������ȷ����̬ SPS

**C5 ����:**
- **512��256��128 ��������ȫ����**
- 512 SPS ʵ�� ~375 Hz (73% Ч��) �� ȷ�ϴ���ƿ��
- 256 SPS ʵ�� ~243 Hz (95% Ч��) �� �ӽ�Ԥ��
- 128 SPS ��Ҫ����ʱ����̬����
- **������ƫ��ָ�� FCLK/����ʱ�ӻ� FIFO drain ʱ�����⣬���Ǵ�����������**

### C6: FCLK ����

**״̬:** ��Ҫʾ����Ӳ�����޷�ͨ���������

������:
- MAX30003 CLK ���� (�� PA8)
- ��֤ 32.768kHz Ƶ��
- ���ë�̡�����/�³�
- MIC ON/OFF �Ա�
- SAI DMA ON/OFF �Ա�

---

## Phase 2 �ܽ�

| ʵ�� | ״̬ | �ؼ����� |
|------|------|---------|
| C1: ECG-only, no-SD | ? ��� | ~375 Hz �� SD/audio/PPG/IMU ����ƿ�� |
| C2: Drain timeout 8��4��2ms | ? ��� | ���� drain �������� (375��355��339 Hz) |
| C7: �Ĵ������� | ? ��� | CNFG_GEN=0x081217, CNFG_ECG=0x025000, RATE=00 ? |
| C3: STATUS-only | ? ��� | EINT ~81Hz, INTB ~0.5Hz �� MAX30003 ���� 512Hz ���� |
| C4: Normal vs Burst FIFO | ? ��� | Burst read �������� Normal; ���� Burst |
| C5: 512/256/128 ���� | ? ��� | ���������� �� ָ�� FCLK/ʱ������ |
| C6: FCLK ʵ�� | ? ��ҪӲ�� | ʾ�������� MAX30003 CLK ���� |

**�����ж�:**
- �����ࣨAudioTask��SD DebugLog�����ȼ�����������ȫ���޸�
- MAX30003 оƬ���������ʲ��� 512 Hz �ǵ�ǰ ECG 374Hz �ĸ���
- ����������: FCLK �쳣 > INTB ���� > MAX30003 �ڲ� PLL
- **��һ��������ʾ�������� MAX30003 CLK ����Ƶ��**

---

## diag_max_gap=1003ms ����λ (2026-05-30)

### ��֤����
1. GDB �״ζ�ȡ: `diag_max_gap_ms=1002`, `diag_task_calls=438`
2. �ȴ� 10 ����ٴζ�ȡ: `diag_max_gap_ms=1003`, `diag_task_calls=2878`
3. 10 ���� task_calls ���� 2440 �Σ��� max_gap ���� 1002��1003

### ����
**1002ms gap ��һ�����¼�**��������¼�Ƴ�ʼ���׶Σ��׸� MAX30003_Task ����ǰ����¼�ƹ��������к��� gap ��������8ms ��Χ����

### �����ƶ�
- SensorTask �� idle �� RECORDING ��ת��·���д���һ������������
- ������ `f_mount`��multi_sensor_logger.c:547�����״ι���ʱ��ʱ
- Ҳ������ `MAX30003_StartStream()` �ĳ�ʼ������
- ��Ӱ��¼���ڼ��ʵʱ��

### ��������
- ����¼�ƿ�ʼǰԤ���� SD �ļ�ϵͳ�������״� f_mount �ӳ�
- ����ܸ� 1s ��ʼ���ӳ�Ϊ������Ϊ
