# MAX30003 512Hz 最终攻关实验指导

## 0. 给 Agent 的文字目标

请读取并严格执行 `MAX30003_FINAL_512HZ_ATTACK_PLAN.md`。当前不要再排查 SD、AudioTask、DebugLog、CSV writer、Pack/Queue 或 FreeRTOS 调度；这些问题已经通过前期实验基本修复或排除。本任务目标是验证 MAX30003 在 FCLK 平均频率 32.5~32.9 kHz、寄存器读回正确、软件 FIFO drain 正常的情况下，为什么 512 SPS 配置只能得到约 374~391 Hz。请重点验证：PLLINT/PLL status 是否为真实失锁还是 latch/误统计；内部 RC/内部时钟模式是否真的生效；MAX30003 芯片脚 FCLK 是否存在毛刺、双边沿、阈值抖动或地弹；内部 calibration 模式是否能达到 512 SPS；以及 lead-off/fast recovery/EMUX 是否影响 512 SPS。每次实验只改变一个变量，最终判断是 PLL/FCLK/电源问题、INTB/EINT 管理问题、模拟输入路径问题、数字采样引擎问题，还是当前板级条件下 512 SPS 不可靠。

---

## 1. 当前事实基线

根据最新报告，当前状态如下：

1. 四模态链路已经基本稳定：MIC + ECG + PPG + IMU 全开时，ECG 可稳定在约 374~391 Hz，数据写入可靠。
2. AudioTask 热循环、优先级反转、SD DebugLog 阻塞、DebugLogWriter 文件句柄导致 CSV `FR_INVALID_OBJECT`、`ecg_written_count=0` 偶发失败、STATUS_ONLY 遗留开关均已修复。
3. STATUS_ONLY 修复后，FIFO 数据恢复，但 ECG 仍约 391 Hz。
4. `notify_wakes` 很少，系统主要依赖 timeout 轮询 drain。
5. `pll_status_seen` 很高，`pll_edge` 有跳变。
6. 三套板子同病，排除单颗 MAX30003 损坏的概率较高。
7. 用户用示波器确认 FCLK 平均频率为 32.5~32.9 kHz。

注意：FCLK 平均频率正确，不等价于 FCLK 输入质量完全正确；仍需确认芯片脚处毛刺、双边沿、过冲/下冲、占空比、阈值抖动和电源地弹。

---

## 2. 绝对禁止事项

### 2.1 禁止回退已修复内容

不得回退：

- AudioTask 条件创建；
- AudioTask session latch；
- AudioTask 优先级修复；
- SD DebugLog RAM ring buffer；
- DebugLogWriter close-after-flush；
- 禁止实时任务直接 SD debug 写入；
- `RECORD_TEST_ECG_STATUS_ONLY=0`；
- MAX30003 始终 drain 策略。

### 2.2 禁止把 EINT 频率直接当作采样率

EINT 受 FIFO threshold / `MNGR_INT.EFIT` / interrupt manager 影响。

最终采样率必须以：

```text
fifo_valid_count / duration_ms
```

为准。

### 2.3 禁止一次改变多个变量

每次实验只能改变一个变量。例如：

- 只清 PLL 状态；
- 只关 PA8 MCO；
- 只启用内部时钟；
- 只开 internal calibration；
- 只改 lead-off；
- 只改 fast recovery；
- 只测 FCLK 波形；
- 只改 RATE。

### 2.4 禁止破坏四模态稳定版

建议建立新分支：

```text
v0.3-max30003-512sps-investigation
```

保留稳定版：

```text
v0.2-four-modal-stable-374hz
```

---

## 3. 实验总览

请按顺序执行：

```text
P0：冻结当前稳定基线
P1：验证 PLLINT / PLL status 是否 latch 或误统计
P2：确认内部 RC / 内部时钟模式是否真正生效
P3：复测 FCLK 芯片脚边沿质量
P4：内部 calibration 模式 512 SPS 验证
P5：Lead-off / Fast Recovery / EMUX 排除
P6：512 / 256 / 128 复测
P7：500 / 250 / 125 验证
P8：电源 / 复位 / 去耦验证
P9：最终决策
```

---

## 4. P0：冻结当前稳定基线

### 4.1 目标

确认当前分支仍然处于稳定状态，不要在攻关前破坏四模态稳定版。

### 4.2 配置

```c
#define RECORD_ENABLE_AUDIO 1
#define RECORD_TEST_ECG_STATUS_ONLY 0
#define RECORD_ECG_SAMPLE_RATE_HZ 512U
```

关闭所有临时实验宏。

### 4.3 运行 30 秒完整四模态

记录：

```text
duration_ms
fifo_valid_count
ecg_sample_count
ecg_written_count
ECG_SPS
fifo_eovf_count
notify_wakes
notify_timeouts
pll_status_seen
pll_edge
last_status
mic_bytes
pack_drop
queue_fail
```

### 4.4 验收

```text
ECG SPS = 374~391 Hz
ecg_written_count == ecg_sample_count == fifo_valid_count
pack_drop = 0
queue_fail = 0
```

如果不满足，先恢复稳定版，不进入后续攻关。

---

## 5. P1：PLLINT / PLL status 真实性验证

### 5.1 目标

确认 `pll_status_seen` 是否代表真实反复失锁，还是状态位 latch 后被重复统计。

### 5.2 实验 P1-A：连续读 STATUS

设置：

```text
ECG-only
无 SD
无 Audio
无 PPG/IMU
RATE=512
```

操作：

```text
1. 初始化 MAX30003。
2. 读 STATUS 一次，记录 status0。
3. 立即连续读 STATUS 10 次，记录 status1~status10。
4. 间隔 100ms，再连续读 STATUS 10 次。
5. 重复 30 秒。
```

记录：

```text
STATUS_SEQ,tick,index,status=0x...,PLL=,EINT=,EOVF=,DCLOFF=,LDOFF_FLAGS=
```

### 5.3 实验 P1-B：清除 STATUS / FIFO_RST / SYNCH 后再读

操作：

```text
1. 读 STATUS。
2. 按 datasheet 要求执行清除动作。
3. 执行 MAX30003_FifoReset()。
4. 执行 MAX30003_Synch()。
5. 等待 100ms。
6. 再读 STATUS。
```

必须提交：

```text
清除前 STATUS
清除后 STATUS
100ms 后 STATUS
1s 后 STATUS
PLLINT 是否重新置位
```

### 5.4 判定

| 结果 | 结论 |
|---|---|
| PLL 位清除后不再出现 | 之前是 latch 误统计 |
| PLL 位清除后很快重新置位 | PLL/时钟/电源确实异常 |
| PLL 位一直为 1 且无法清除 | 需确认位定义是否解码错误 |
| PLL edge 少但 seen 高 | 不得写成“每秒失锁 210 次”，只能写“PLL 状态长期异常” |

---

## 6. P2：确认内部 RC / 内部时钟模式是否真正生效

### 6.1 目标

报告中提到“内部 RC 振荡器模式下 PLL 仍失锁，391 Hz”。这个结论非常关键，但必须确认内部模式真的生效。

### 6.2 必须提交的证据

```text
1. 内部时钟模式下 CNFG_GEN 完整读回值；
2. FMSTR 解码值；
3. 关闭 STM32 PA8 MCO 后，MAX30003 是否仍能产样；
4. 关闭 PA8 MCO 后，fifo_valid_sps；
5. 关闭 PA8 MCO 后，STATUS / PLL / EINT / EOVF；
6. 重新打开 PA8 MCO 后，fifo_valid_sps 是否变化。
```

### 6.3 实验 P2-A：当前外部 FCLK 基线

```text
PA8 MCO ON
FMSTR=00
RATE=512
ECG-only
30s
```

记录：

```text
fifo_valid_sps
PLL status
STATUS
CNFG_GEN
```

### 6.4 实验 P2-B：尝试内部时钟配置

按当前 `max3003.h` 与 datasheet 配置内部时钟 / 内部 RC 模式。

记录：

```text
CNFG_GEN before
CNFG_GEN after
FMSTR decoded
RATE decoded
fifo_valid_sps
PLL status
```

### 6.5 实验 P2-C：关闭 PA8 MCO

关闭 STM32 PA8 MCO 输出：

```text
PA8 MCO OFF
MAX30003 内部时钟模式保持不变
RATE=512
ECG-only
30s
```

判定：

| 结果 | 结论 |
|---|---|
| PA8 OFF 后仍有 ~391Hz ECG | 内部时钟模式可能真的生效 |
| PA8 OFF 后 ECG 停止 | 所谓内部模式未生效，仍依赖外部 FCLK |
| PA8 OFF 后状态变化但仍低速 | 需重新查 FMSTR / 时钟源配置 |
| PA8 ON/OFF 对 SPS 完全无影响 | 外部 FCLK 可能没有被芯片使用或配置错误 |

---

## 7. P3：FCLK 芯片脚边沿质量复测

### 7.1 目标

用户已确认 FCLK 平均频率为 32.5~32.9 kHz。本实验不再验证频率，而是验证波形质量。

### 7.2 测量点

必须测：

```text
MAX30003 CLK / FCLK 引脚
```

不要只测 STM32 PA8 或电阻前。

### 7.3 示波器设置

```text
1. 边沿触发；
2. 触发电平设为 VDD/2 附近；
3. Persistence / 余辉打开；
4. Glitch trigger 抓窄脉冲；
5. Single-shot 捕获异常边沿；
6. 时间尺度同时看 32k 周期和窄毛刺；
7. 探头地线尽量短。
```

### 7.4 测量项目

```text
频率
占空比
Vhigh
Vlow
上升沿时间
下降沿时间
过冲
下冲
毛刺宽度
毛刺幅度
是否有双边沿
周期抖动
```

### 7.5 场景矩阵

| 场景 | EN_MIC | SAI DMA | SD active | 说明 |
|---|---|---|---|---|
| FCLK-A | OFF | OFF | OFF | 最干净基线 |
| FCLK-B | ON | OFF | OFF | 只 MIC 上电 |
| FCLK-C | ON | ON | OFF | SAI 活动 |
| FCLK-D | ON | ON | ON | 完整四模态 |

### 7.6 判定

| 结果 | 结论 |
|---|---|
| 平均频率正确但有毛刺/双边沿 | PLL/采样异常可能由 FCLK 输入质量导致 |
| MIC/SAI ON 后毛刺明显增加 | 走线串扰 |
| 完整四模态下才出现异常 | SD/SAI/电源噪声耦合到 FCLK |
| 所有场景干净 | FCLK 边沿质量基本排除 |

---

## 8. P4：内部 calibration 模式验证

### 8.1 目标

排除人体电极、模拟输入、lead-off、fast recovery 对 512 SPS 的影响。

### 8.2 实验设置

根据 datasheet 和当前 `max3003.h`：

```text
1. 开启 CNFG_CAL 内部 calibration signal；
2. CNFG_EMUX 切到 internal calibration input；
3. CNFG_ECG RATE=512；
4. 保持 FCLK 当前配置；
5. ECG-only，无 SD；
6. 记录 30 秒。
```

### 8.3 记录

```text
fifo_valid_sps
etag_hist[0..7]
fifo_empty
fifo_eovf
unknown_etag
PLL status
DCLOFF/LDOFF flags
CNFG_CAL
CNFG_EMUX
CNFG_ECG
```

### 8.4 判定

| 结果 | 结论 |
|---|---|
| calibration 仍约 374~391Hz | 数字采样/PLL/时钟/RATE/FIFO 管理问题 |
| calibration 达到 512Hz | 外部模拟输入/lead-off/fast recovery/EMUX 影响 |
| calibration 无数据 | CNFG_CAL/EMUX 配置错误，先修配置 |
| calibration 下 PLL 状态正常 | 外部输入相关因素影响 PLL/status |

---

## 9. P5：Lead-off / Fast Recovery / EMUX 排除

### 9.1 目标

确认 DC lead-off、fast recovery、dynamic manager 是否影响 512 SPS。

### 9.2 实验 P5-A：关闭 DC Lead-off

仅修改 DC lead-off 相关 enable 位。保持 RATE=512、FCLK 不变。

记录：

```text
fifo_valid_sps
DCLOFFINT
LDOFF flags
PLL status
```

### 9.3 实验 P5-B：关闭 Fast Recovery / MNGR_DYN

```text
MNGR_DYN = 0
或禁用 fast recovery 相关字段
```

记录：

```text
fifo_valid_sps
fast_count
PLL status
STATUS
```

### 9.4 实验 P5-C：EMUX shorted input / test input

如果支持：

```text
EMUX 切到 shorted input 或 test input
RATE=512
```

### 9.5 判定

| 结果 | 结论 |
|---|---|
| 关闭 lead-off 后 512 | lead-off 干扰 |
| 关闭 fast recovery 后 512 | dynamic recovery 干扰 |
| 内部输入/短接输入都 374 | 数字/时钟/PLL 路径问题 |

---

## 10. P6：512 / 256 / 128 复测

### 10.1 目标

确认非线性比例稳定复现。

### 10.2 设置

```text
ECG-only
无 SD
无 Audio
无 PPG/IMU
每组 30s
```

### 10.3 组别

```text
RATE=512
RATE=256
RATE=128
```

每组都必须读回：

```text
CNFG_GEN
CNFG_ECG
FMSTR
RATE
STATUS
```

### 10.4 判定

| 结果 | 结论 |
|---|---|
| 512→374, 256→243, 128→119 | 高速档特异异常 |
| 512→374, 256→187, 128→94 | 比例性时钟/采样缩放问题 |
| 512→512, 256→256, 128→128 | 已由某配置修复 |
| 512/256/128 全部低 | 总时钟/配置问题 |

---

## 11. P7：500 / 250 / 125 验证

### 11.1 目标

如果支持 32k / FMSTR=01 模式，验证 500 SPS 高速档是否也异常。

### 11.2 设置

```text
FMSTR=01
对应 500 / 250 / 125 SPS
```

如果硬件不支持 32k FCLK，记录原因并跳过。

### 11.3 判定

| 结果 | 结论 |
|---|---|
| 500 也低效 | 高速档共性问题 |
| 500 正常但 512 异常 | 32.768k 模式 / 分频特异 |
| 250/125 正常 | 低速路径正常 |

---

## 12. P8：电源 / 复位 / 去耦验证

### 12.1 目标

如果 PLL/status 确认异常，而 FCLK 频率和边沿都干净，则查电源。

### 12.2 测量

示波器测：

```text
MAX30003 AVDD
MAX30003 DVDD
MAX30003 VREF / internal reference if accessible
GND bounce
EN_MIC ON/OFF
SAI DMA ON/OFF
SD active/inactive
```

### 12.3 判断

| 结果 | 结论 |
|---|---|
| AVDD/DVDD 有明显纹波或跌落 | 去耦/电源问题 |
| SD/MIC 活动时电源噪声增大 | 四模态耦合问题 |
| 电源干净但 PLL 异常 | 芯片配置/时钟输入/设计限制 |

---

## 13. P9：最终判定表

| 结果 | 判断 |
|---|---|
| PLL 位清除后不再出现 | 之前是 latch 误统计 |
| PLL 位清除后快速重新置位 | PLL/时钟/电源异常 |
| PA8 MCO OFF 后内部模式仍产样 | 内部模式生效 |
| PA8 MCO OFF 后停止产样 | 内部模式未生效或仍依赖外部 FCLK |
| FCLK 有毛刺/双边沿 | FCLK 边沿质量问题 |
| calibration 达 512 | 外部输入/lead-off/fast recovery 问题 |
| calibration 仍 374~391 | 数字采样/PLL/时钟/RATE 管理问题 |
| 500 正常但 512 异常 | 32.768k 模式特异 |
| 500 也异常 | 高速档共性异常 |
| 所有实验无法恢复 512 | 当前板级条件下 512 SPS 不可靠 |

---

## 14. 工程决策建议

### 14.1 稳定版本

冻结：

```text
v0.2-four-modal-stable-374hz
```

规格：

```text
四模态可用，ECG 实际 374~391 Hz，数据完整。
```

### 14.2 攻关版本

分支：

```text
v0.3-max30003-512sps-investigation
```

仅用于本任务中的实验，不得破坏稳定版。

### 14.3 如果必须严格 512

硬件建议：

```text
1. 独立低抖动 32.768k 晶振直供 MAX30003；
2. FCLK 源端串阻，而不是只在芯片端串阻；
3. FCLK 远离 SAI / SDMMC / SPI；
4. MAX30003 AVDD/DVDD 增强去耦；
5. INTB 加明确上拉并示波器验证；
6. 对照官方 EVKIT 或换批次芯片；
7. 增加一块纯 MAX30003 最小系统板对照。
```

---

## 15. 最终提交物

请提交：

```text
1. 当前 commit
2. P0 稳定基线结果
3. P1 PLL status latch/clear 实验结果
4. P2 内部时钟模式是否真生效
5. P3 FCLK 芯片脚波形记录
6. P4 内部 calibration 结果
7. P5 lead-off / fast recovery / EMUX 排除结果
8. P6 512/256/128 比例表
9. P7 500/250/125 结果或跳过原因
10. P8 电源/去耦测量结果
11. 最终判断
12. 工程建议：接受 374~391 / 继续攻关 / 硬件改版
```

最终判断格式：

```text
最终根因判断：

证据：
1.
2.
3.

已排除：
1.
2.
3.

仍不确定：
1.
2.
3.

建议：
1.
2.
3.
```
