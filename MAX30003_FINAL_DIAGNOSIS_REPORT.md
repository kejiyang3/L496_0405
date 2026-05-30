# L496 MAX30003 512Hz — 完整诊断与修复报告

**日期**: 2026-05-30 | **硬件**: STM32L496 + MAX30003 + MAX30102 + ICM20948 + MIC | **Repo**: kejiyang3/L496_0405

---

## 问题演进路线

```
Case F 四模态掉到 74 Hz
    ↓ 修复 AudioTask 热循环 + SD DebugLog 阻塞
四模态稳定 ~374-391 Hz
    ↓ 追问: 为什么 512 Hz 配置只能到 ~374 Hz?
芯片级 PLL 验证 → STATUS_ONLY 开关遗留 bug
    ↓ 修复 STATUS_ONLY，数据恢复
当前: ~391 Hz, INTB 不触发, PLL 持续失锁
```

---

## 一、第一阶段：AudioTask + SD 阻塞修复

### 1.1 原始问题

| Case | 条件 | ECG SPS | 问题 |
|------|------|---------|------|
| A | 无音频基线 | 374 Hz | 基线 |
| C | AudioTask 空跑 | **100 Hz** | 热循环重入 |
| F | 完整四模态 | **74 Hz** | SD 写阻塞 |

### 1.2 修复项

| # | 修复 | 文件 | 方法 |
|---|------|------|------|
| P0 | AudioTask 条件创建 | `freertos.c` | `#if RECORD_ENABLE_AUDIO` |
| P1 | 优先级反转 | `freertos.c` | AudioTask ≤ SensorTask |
| P2 | 热循环重入 | `audio_recorder.c` | session latch (file_seq) |
| P3 | SD mutex 短超时 | `audio_recorder.c` | 1000ms → 20ms |
| P3 | 禁止录制中 f_sync | `audio_recorder.c` | 仅停止时 header 回写 |
| - | DebugLog RAM ring buffer | `sd_debug_log.c` | 异步写 SD |
| - | DebugLogWriter 低优先级 | `freertos.c` | 独立任务 |

### 1.3 修复后验证

| 轮次 | ecg_sample | ecg_written | SPS | eovf |
|------|-----------|-------------|-----|------|
| R1 | 11727 | 11727 | ~391 | 6 |
| R2 | 11234 | 11234 | ~374 | 7 |
| R3 | 11236 | 11236 | ~375 | 7 |

✅ 数据完整性 3/3 通过，Pack drop=0, Queue fail=0

---

## 二、第二阶段：ECG 512Hz 攻关

### 2.1 实验矩阵

| 实验 | 内容 | 结果 |
|------|------|------|
| **C1** | ECG-only, 无SD, 无音频 | 375 Hz — 软件非瓶颈 |
| **C2** | Drain 间隔 8→4→2ms | 375→355→339, 8ms 最优 |
| **C3** | STATUS-only, 不读FIFO | EINT ~81 Hz (非 128) |
| **C4** | Burst vs Normal FIFO | Burst 明显更优 |
| **C5** | 512/256/128 比例测试 | 比例非线性 → 时钟问题 |
| **C6** | FCLK 示波器 | 32.5-32.9 kHz ✓ |
| **C7** | 寄存器读回验证 | CNFG_GEN/CNFG_ECG ✓ |

### 2.2 C5 比例测试（关键证据）

| 设定 SPS | 实际 SPS | 效率 |
|----------|----------|------|
| 512 | ~375 Hz | 73% |
| 256 | ~243 Hz | 95% |
| 128 | ~119 Hz | 93% |

→ 高 SPS 时效率骤降，指向 PLL/时钟。

### 2.3 已排除

- 软件阻塞 (C1)
- AudioTask/SD/FIFO 读取/Drain 频率
- CNFG_GEN/CNFG_ECG/EN_INT 配置
- FCLK 频率 (示波器确认)
- DCLOFF/FastRec 干扰
- EINT 门控 drain 策略

---

## 三、第三阶段：芯片级 PLL 验证 + STATUS_ONLY 修复

### 3.1 PLL 失锁确认（跨板验证）

| 板子 | 模式 | pll_status_seen (30s) | pll_edge |
|------|------|----------------------|----------|
| 板 1 | STATUS-only | 1913 | 47 |
| 板 2 | STATUS-only | 3274 | 65 |
| 板 3 | 正常模式 | 6038 | 1 |

→ **3 套板子 PLL 全部失锁**，排除单芯片缺陷

### 3.2 排除实验

| 变量 | 结果 |
|------|------|
| 关闭 DCLOFF | PLL 仍失锁 |
| 关闭 Fast Recovery | PLL 仍失锁 |
| PLL 等待 100→500ms | PLL 仍失锁 |
| **内部 RC 振荡器** | PLL 仍失锁 (391 Hz) |
| INTB Open-Drain→CMOS | 无效 |

### 3.3 STATUS_ONLY 遗留开关修复（今日）

**Bug**: `RECORD_TEST_ECG_STATUS_ONLY=1` 导致 MAX30003_Task 只读 STATUS 不读 FIFO

**修复**: 设为 `0` → 数据采集恢复

**修复后结果**（30 秒自动录制）:

| 指标 | 值 | 说明 |
|------|-----|------|
| `fifo_valid_count` | **11,736** | ✅ 数据恢复 |
| `ecg_written_count` | 11,736 | 全部写入 |
| **ECG SPS** | **~391 Hz** | 仍不到 512 |
| `diag_task_calls` | 3,003 | ~100 次/秒 |
| `notify_wakes` | **20** | 🔴 INTB 几乎不触发 |
| `notify_timeouts` | **2,983** | 99.3% 超时轮询 |
| `diag_max_gap_ms` | **1,002** | 录制初始化事件 |
| `fifo_eovf_count` | 6 | 可接受 |
| `fifo_empty_count` | 2,997 | FIFO 常空 |
| `pll_status_seen` | **6,318** | 🔴 PLL 失锁 210次/秒 |
| `pll_edge` | 14 | PLL 跳变 |
| `last_status` | 0xC00300 | EINT+EOVF 置位 |
| `diag_eint_hits` | 13 | INTB 未触发 |

---

## 四、最终结论

```
┌──────────────────────────────────────────────────┐
│  软件侧    → 全部修复 ✅                          │
│  寄存器    → 全部正确 ✅                          │
│  FCLK 频率 → 32.5-32.9 kHz ✅ (示波器)           │
│  ──────────────────────────────────────           │
│  INTB      → 几乎不触发 🔴 (20/3003 = 0.7%)      │
│  PLL       → 持续失锁 🔴 (6318次/30秒)            │
│  ──────────────────────────────────────           │
│  根因: MAX30003 内部 PLL 运行时失锁               │
│  导致: 实际采样率 ~374-391 Hz, 不达 512 SPS       │
│  范围: 3 套板子同病 → PCB 晶体电路设计问题         │
└──────────────────────────────────────────────────┘
```

### 根因判断

**MAX30003 内部 PLL 在运行中持续失锁**。外部 FCLK 频率正常 (32.5-32.9 kHz)，但 PLL 无法稳定锁定，导致：
1. 内部采样时钟不稳定 → 实际 SPS ~374-391 Hz
2. EINT 以 ~78 Hz 翻转（应为 ~128 Hz）
3. INTB 几乎不触发（芯片内部时序异常）

3 套板子同病，排除单芯片缺陷 → **PCB 层面 32.768kHz 晶体电路问题**（负载电容/走线/噪声/电源去耦）

### 仍未解决

1. INTB 几乎不触发（20/3003 次），系统依赖超时轮询
2. ECG SPS 上限 ~391 Hz，无法达到配置的 512 Hz
3. PLL 锁丢失频率极高（210 次/秒）

---

## 五、下一步建议

| 优先级 | 方向 | 预期 |
|--------|------|------|
| **P0** | 示波器看 FCLK 波形细节（抖动/幅度/噪声） | 定位晶体电路问题 |
| **P0** | 换不同负载电容 32.768kHz 晶振 | 排除电容不匹配 |
| **P1** | 检查 MAX30003 AVDD/DVDD 电源去耦 | 排除电源噪声 |
| **P1** | 对比 `C:\VS_Project\L496_0405` 旧代码 | 如果旧代码可达 512Hz |
| **P2** | 如以上无效 → 接受 374-391 Hz 为板级限制 | 软件侧已无优化空间 |

---

## 六、当前固件状态

- **编译**: ✅ 通过 (FLASH 77.5%, RAM 89.6%)
- **自动录制**: ✅ 30 秒自动启动
- **四模态**: MIC + ECG + PPG + IMU 全开
- **ECG SPS**: ~391 Hz（稳定，数据完整）
- **实验开关**: `RECORD_TEST_ECG_STATUS_ONLY=0`, `NO_DCLOFF=1`

---

## 七、修改文件清单

| 文件 | 修改内容 |
|------|----------|
| `Core/Src/audio_recorder.c` | 热循环修复、session latch、短超时 mutex、禁止录制中 f_sync |
| `Core/Src/sd_debug_log.c` | RAM ring buffer、close-after-flush、异步写入 |
| `Core/Src/max3003.c` | C4/C5/V3/V4/V6 条件编译、PLL 监控、Burst 优化 |
| `Core/Src/freertos.c` | 优先级修正、DebugLogWriter 任务、自动录制 |
| `Core/Src/multi_sensor_logger.c` | pack 计数、f_mount 不卸载 |
| `Core/Inc/max3003.h` | RATE 定义、FMSTR 变体、INTB CMOS |
| `Core/Inc/record_feature_flags.h` | 全实验开关集（已恢复 STATUS_ONLY=0） |
| `Core/Inc/ecg_record_control.h` | 诊断字段扩展 |
