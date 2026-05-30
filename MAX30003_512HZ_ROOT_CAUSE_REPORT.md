# MAX30003 512Hz 根因排查总报告

**日期**: 2026-05-30 | **硬件**: STM32L496 + MAX30003 | **固件**: 10594ff

---

## 1. 问题

配置 512 SPS → 实际 ~375 Hz（效率 73%）

---

## 2. 已排除

| # | 嫌疑 | 证据 |
|---|------|------|
| 1 | 软件阻塞 | C1 无SD无音频 = 375Hz |
| 2 | AudioTask | 已修复，Case C 正常 |
| 3 | SD DebugLog | P3 no-debug 恢复 |
| 4 | Burst读取 | C4 Burst最优 |
| 5 | Drain频率 | C2 8ms最优 |
| 6 | RATE配置 | C7 CNFG_ECG=0x025000 ? |
| 7 | **FCLK频率** | **示波器 32.5-32.9kHz ?** |

---

## 3. 已确认的寄存器配置

| 寄存器 | 值 | 含义 |
|--------|-----|------|
| CNFG_GEN | 0x081217 | EN_ECG + FMSTR=32K + DCLOFF + RBIAS |
| CNFG_ECG | 0x025000 | RATE=00(512SPS) + GAIN=80x + DHPF=0.5Hz + DLPF=40Hz |
| EN_INT (init) | 0x000002 | IDLE: EINT/EOVF 禁能, INTB_TYPE=10 |
| EN_INT (stream) | 0xC00002 | EN_EINT=1 + EN_EOVF=1 + INTB_TYPE=10 |
| MNGR_INT | 0x800000 | EFIT=4 (每4样本EINT) |
| MNGR_DYN | 0xBF0000 | Auto Fast Recovery |

---

## 4. 芯片行为异常

| 现象 | 预期 | 实际 |
|------|------|------|
| EINT翻转 | 512 Hz (EFIT=1) | **~81 Hz** |
| INTB触发 | 每EINT翻转 | **~1 Hz** |
| FIFO有效样本 | 512/s | **~375/s** |
| 512/256比例 | 2:1 | **1.54:1** (非线性) |

---

## 5. INTB_TYPE 关键发现

EN_INT = 0xC00002 → **INTB_TYPE = 10 = Open-Drain 无内部上拉**

这意味着 PCB 上必须有一个外部上拉电阻。若无，INTB 引脚浮空 → 电平不确定 → 可能被误读。

---

## 6. 剩余待查（需要板子连接）

| P | 项目 | 方法 |
|----|------|------|
| **P0** | PLL 锁定状态 | GDB: `g_ecg_rec.last_status & 0x000100` |
| **P0** | 持续PLL监控 | 每秒 diag log 新增 pll= 字段 |
| **P1** | INTB外部上拉 | 万用表测 PB6 电压 / 示波器波形 |
| **P1** | EN_INT读回 | GDB确认写入 0xC00002 成功 |
| **P2** | INTB引脚直连 | 示波器直接测 MAX30003 INTB 脚 |

---

## 7. 诊断命令（板子连上后执行）

```
# 1. 烧录
openocd -f interface/stlink.cfg -f target/stm32l4x.cfg -c "program build/L496_0405.elf verify reset exit"

# 2. GDB 批量诊断
arm-none-eabi-gdb -q -batch -x max3003_chip_diag.gdb build/L496_0405.elf
```

---

## 8. 当前判断

```
FCLK ? → 配置 ? → 软件 ?
         ↓
    MAX30003 芯片内部问题
    ├─ PLL 可能未锁定 (最可疑)
    ├─ INTB_TYPE=10 缺外部上拉
    └─ 或芯片本身缺陷
```
