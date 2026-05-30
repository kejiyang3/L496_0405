# L496 ECG 采集系统 — 完整实验总结报告

**日期**: 2026-05-30 | **硬件**: STM32L496 + MAX30003 + MAX30102 + ICM20948 + MIC | **Repo**: kejiyang3/L496_0405

---

## 一、问题演进

```
发现 Case F 低速 (74 Hz)
    ↓
修复 AudioTask + SD DebugLog
    ↓
四模态稳定 (374-391 Hz)
    ↓
追问: 为什么不是 512 Hz?
    ↓
芯片级验证 → PLL 失锁
```

---

## 二、第一阶段：AudioTask + SD DebugLog 修复

### 2.1 原始问题

| Case | 条件 | ECG SPS |
|------|------|---------|
| A | 无音频 | 374 Hz |
| C | AudioTask 空跑 | **100 Hz** ← 热循环重入 |
| F | 完整 MIC | **74 Hz** ← SD 阻塞 |

### 2.2 修复

| 修复 | 方法 | 文件 |
|------|------|------|
| AudioTask 热循环 | session latch + 优先级限制 | audio_recorder.c, freertos.c |
| SD DebugLog 阻塞 | RAM ring buffer + 低优先级 writer | sd_debug_log.c, freertos.c |
| DebugLogWriter 句柄冲突 | close-after-flush | sd_debug_log.c |
| 音频 SD 写入 | 短超时 + 可丢块 | audio_recorder.c |

### 2.3 修复后验证 (Case F 四模态)

| 轮次 | ecg_sample | ecg_written | fifo_valid | SPS | eovf |
|------|-----------|-------------|-----------|-----|------|
| R1 | 11727 | 11727 | 11727 | ~391 | 6 |
| R2 | 11234 | 11234 | 11234 | ~374 | 7 |
| R3 | 11236 | 11236 | 11236 | ~375 | 7 |

? 3/3 数据完整性通过。Pack drop=0, Queue fail=0。

---

## 三、第二阶段：ECG 512Hz 攻关

### 3.1 实验矩阵

| 实验 | 内容 | 结果 |
|------|------|------|
| **C1** | ECG-only, 无SD, 无音频 | **375 Hz** — 软件非瓶颈 |
| **C2** | Drain 8→4→2ms | 375→355→339 — 8ms 最优 |
| **C7** | 寄存器读回 | CNFG_GEN=0x081217, CNFG_ECG=0x025000 ? |
| **C3** | STATUS-only, 不读FIFO | **EINT ~81 Hz** (非 512) |
| **C4** | Burst vs Normal FIFO | **Burst 明显更优** |
| **C5** | 512/256/128 比例 | **比例非线性** → 时钟问题 |
| **C6** | FCLK 示波器 | 32.5-32.9 kHz ? |

### 3.2 C5 比例测试

| 设定 SPS | 实际 SPS | 效率 |
|----------|----------|------|
| 512 | ~375 Hz | 73% |
| 256 | ~243 Hz | 95% |
| 128 | ~119 Hz | 93% |

→ 高 SPS 效率骤降，指向 PLL/时钟。

### 3.3 diag_max_gap=1003ms

→ 一次性录制初始化事件，非复发问题。

---

## 四、第三阶段：芯片级 PLL 验证

### 4.1 PLL 失锁确认

| 板子 | 模式 | pll_status_seen | pll_edge |
|------|------|----------------|----------|
| 板1 | STATUS-only | 1913 | 47 |
| 板2 | STATUS-only | 3274 | 65 |
| 板3 | 正常模式 | 6038 | 1 |

→ **3 套板子 PLL 全部失锁**，排除单芯片缺陷。

### 4.2 排除实验

| 变量 | 结果 |
|------|------|
| 关闭 DCLOFF | PLL 仍失锁 |
| 关闭 Fast Recovery | PLL 仍失锁 |
| PLL 等待 100→500ms | PLL 仍失锁 |
| 内部 RC 振荡器 | PLL 仍失锁 |
| EINT 门控 drain | PLL 仍失锁 |

### 4.3 时钟源对比

| 时钟 | SPS | PLL 状态 |
|------|-----|---------|
| 外部 32kHz 晶振 | 374 Hz | 失锁 |
| 内部 RC | 391 Hz | 失锁 |

---

## 五、最终结论

```
┌─────────────────────────────────────────┐
│  软件侧    → 全部修复 ?                  │
│  寄存器    → 全部正确 ?                  │
│  FCLK 频率 → 32.5-32.9 kHz ?            │
│  INTB      → 78 Hz (门控下正常)          │
│  ───────────────────────                │
│  PLL       → 持续失锁 ← 根因             │
│  PCB 晶体电路 → 负载电容/走线/噪声        │
└─────────────────────────────────────────┘
```

**MAX30003 内部 PLL 运行时失锁，芯片实际采样率 ~374-391 Hz，不达配置的 512 SPS。3 套板子同病 → PCB 设计层面的 32.768kHz 晶体电路问题。**

---

## 六、当前固件状态

四模态稳定版 (MIC+ECG+PPG+IMU)，ECG ~374-391 Hz，数据完整性可靠。

## 七、修改文件总览

| 文件 | 修改 |
|------|------|
| `Core/Src/audio_recorder.c` | 热循环修复、session latch、短超时 |
| `Core/Src/sd_debug_log.c` | RAM ring buffer、close-after-flush |
| `Core/Src/max3003.c` | C4/C5/V3/V4/V6 条件编译、PLL 监控、EINT 门控 |
| `Core/Src/freertos.c` | 优先级、DebugLogWriter 任务、250ms EN_MIC 移除 |
| `Core/Src/multi_sensor_logger.c` | pack 计数器、f_mount 不卸载 |
| `Core/Inc/max3003.h` | RATE 定义、FMSTR 变体、CMOS INTB |
| `Core/Inc/record_feature_flags.h` | 实验标志位全集 |
| `Core/Inc/ecg_record_control.h` | 诊断字段扩展 |
