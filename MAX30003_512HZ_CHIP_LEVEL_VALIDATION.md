# MAX30003 512Hz 芯片级验证方案

**状态**: 待板子连接 | **固件**: e167c75 | **上一阶段**: FCLK 32.5-32.9kHz ?

---

## 已排除（不再回头查）

SD、AudioTask、DebugLog、CSV writer、Pack/Queue、FreeRTOS 调度、FCLK 频率、RATE 配置、FIFO 读取方式、Drain 频率

---

## 验证矩阵

| V# | 变量 | 修改方式 | 判断标准 | 状态 |
|----|------|---------|---------|------|
| V1 | PLL 锁定 | 读 STATUS[PLLINT]，每秒记录 | PLLINT=1 → 失锁 | ?? 固件已备 |
| V2 | EN_INT/MNGR | GDB 读寄存器 | 0xC00002/0x800000 | ? 待执行 |
| V3 | Lead-off/FastRec | 关闭 DCLOFF + FastRec | 比例是否恢复 | ? 待编码 |
| V4 | FMSTR 分频 | FMSTR=01/10/11 | 预期比例变化 | ? 待编码 |
| V5 | 512/256/128 | 稳态 60s 测试 | 精确比例关系 | ? 待执行 |
| V6 | INTB 上拉 | EN_INT 改 INTB_TYPE=00(CMOS) | INTB 频率恢复 | ? 待编码 |

---

## V1: PLL 长时间监控

**修改**: 已添加到 `MAX30003_DiagLog_Run()`，每秒输出 `pll=` 字段

**固件模式**: STATUS-only (RECORD_TEST_ECG_STATUS_ONLY=1)

**执行**:
```
openocd -f interface/stlink.cfg -f target/stm32l4x.cfg -c "program build/L496_0405.elf verify reset exit"
# 等待 30s，然后:
arm-none-eabi-gdb -q -batch -x max3003_chip_diag.gdb build/L496_0405.elf
```

**读取**:
- `g_ecg_rec.pll_status_seen_count` — PLLINT 置位次数
- `g_ecg_rec.pll_edge_count` — PLL 失锁→锁定翻转次数
- `g_ecg_rec.last_status & 0x000100` — 当前 PLL 锁定位

**判断**:
- pll_seen > 0 → PLL 曾经失锁 → PLL 异常
- pll_seen = 0 → PLL 一直锁定 → 非 PLL 问题

---

## V2: 全寄存器读回

**执行**: 板子运行 30s 后 GDB 读

```
arm-none-eabi-gdb -q -batch -x max3003_chip_diag.gdb build/L496_0405.elf
```

**关键寄存器已缓存**:
| 变量 | 寄存器 | 期望值 |
|------|--------|--------|
| `g_max30003_init_value` | CNFG_GEN | 0x081217 |
| `g_max30003_start_status1/2/3` | STATUS | 运行时状态 |
| `g_ecg_rec.last_status` | STATUS | 最新状态 |

---

## V3: Lead-off / Fast Recovery 关闭测试

**修改**: `record_feature_flags.h` 添加:
```c
#define RECORD_TEST_MAX30003_NO_DCLOFF  0  // 设1关闭 DC lead-off
#define RECORD_TEST_MAX30003_NO_FASTREC 0  // 设1关闭 Fast Recovery
```

**CNFG_GEN 变体**:
- 正常: 0x081217 (EN_DCLOFF + EN_RBIAS)
- 无DCLOFF: 0x080017 (仅 EN_ECG + EN_RBIAS)
- 无FASTREC: MNGR_DYN = 0x000000

**交叉测试矩阵**:

| 测试 | DCLOFF | FastRec | 预期 |
|------|--------|---------|------|
| V3a | ON | ON | 基线 ~375 |
| V3b | OFF | ON | 无lead-off干扰 |
| V3c | ON | OFF | 无FastRec抢占 |
| V3d | OFF | OFF | 最简配置 |

---

## V4: FMSTR 分频测试

**修改**: `record_feature_flags.h`:
```c
#define RECORD_TEST_MAX30003_FMSTR 0  // 0=32k, 1=16k, 2=8k, 3=4k
```

**CNFG_GEN 变体**:

| FMSTR | CNFG_GEN | fMSTR | RATE=00 ECG |
|-------|----------|-------|-------------|
| 00 | 0x081217 | 32.768k | 512 SPS |
| 01 | 0x101217 | 16.384k | 256 SPS |
| 10 | 0x201217 | 8.192k | 128 SPS |
| 11 | 0x301217 | 4.096k | 64 SPS |

**判断**: 若 FMSTR=01 时实际 SPS 不等于基线的一半 → 时钟链问题

---

## V5: 512/256/128 稳态比例

**方法**: 每组 RATE 跑 60s，用 GDB 读 `fifo_valid_count`，计算精确 SPS

| RATE | 设定 | 期望比例 | 实际(之前) | 需验证 |
|------|------|---------|-----------|--------|
| 00 | 512 | 1.00× | ~375 | 精确值 |
| 01 | 256 | 0.50× | ~243 | 精确值 |
| 10 | 128 | 0.25× | ~119 | 精确值 |

**代码已备**: `RECORD_TEST_ECG_RATE_SELECT` 0/1/2

---

## V6: INTB 验证

**修改**: `record_feature_flags.h`:
```c
#define RECORD_TEST_MAX30003_INTB_CMOS 0  // 设1=CMOS输出
```

EN_INT 变体:
- 正常: 0xC00002 (INTB_TYPE=10 Open-Drain)
- CMOS: 0xC00000 (INTB_TYPE=00 CMOS)

**硬件验证**:
- 万用表测 PB6: 正常上拉时应为高电平，脉冲时为低
- 示波器测 MAX30003 INTB 脚直达波形

---

## 当前已准备就绪

| 文件 | 用途 |
|------|------|
| `build/L496_0405.elf` | STATUS-only 固件，含 PLL 监控 |
| `max3003_chip_diag.gdb` | 一键全寄存器诊断脚本 |
| `Core/Inc/record_feature_flags.h` | RECORD_TEST_ECG_RATE_SELECT 等标志 |
| `Core/Src/max3003.c` | PLL 监控 + 条件编译分支 |

## 执行顺序

```
板子接上 →
  1. 烧录 + V1 PLL 监控 (30s)
  2. V2 全寄存器读回
  3. 根据 V1/V2 结果决定下一步:
     - PLL 失锁 → 查 PLL 电路
     - PLL 正常 → V3 lead-off/fastrec
     - 寄存器异常 → 查 SPI 写入
  4. V5 稳态比例确认
  5. V6 INTB 硬件验证
```
