# 硬件 — 原理图

> 负责人：你  |  文档维护：Codex（根据你提供的信息更新）
> 数据来源：`hardware/V1/sasV9.tel`（主板）+ `hardware/V1/sasV9bio.tel`（生物传感器子板）

---

## 系统框图

```
STM32L496RET6 (U1, LQFP-64)
│
├── 电源
│   ├── CN1 → U8 (QFN-16 充电管理) → BAT_RAW
│   ├── U9 (SOT-23-5 LDO) → SYS
│   ├── U23 (SOT-23-5 使能控制) → EN_3
│   └── L1 (2.2μH 电感)
│
├── SPI1 → FPC1 (18P) → LCD ST7789V + 触控 CST816
├── SPI3 → U12 (MAX30003 ECG, QFN-28)
├── I2C2 → FPC1 → CST816 (触控)
├── I2C3 → FPC2 (10P) → 生物子板 (TXS0104 → MAX30102 PPG + ICM20948 IMU)
├── SDMMC → CARD1 (TF卡座, 4-bit)
├── UART1 → U7 (E104-BT5005A BLE模块)
├── SAI1 → U14 (ICS-43432 MEMS MIC)
├── USB → D3 (SOT-23-6 ESD) → H1 (4P排针)
├── AUDIO1 (PJ-316A-6A 3.5mm) → ECG导联输入
├── SW2 → PA1 (按键, 低有效)
└── SW1 → NRST (复位键)
```

---

## 原理图清单

| 模块 | 来源 | 版本 | 状态 |
|------|------|------|------|
| 主板 (V1) | `hardware/V1/sasV9.tel` | V9 | ✅ 已解析 |
| 生物子板 | `hardware/V1/sasV9bio.tel` | V9 | ✅ 已解析 |
| PPG (MAX30102) | `hardware/MAX30102_心率采集/` | v1.0 | ✅ |

---

## 完整 BOM（从网表解析）

### 主板 (sasV9)

| 位号 | 规格 | 封装 | 功能 |
|------|------|------|------|
| U1 | STM32L496RET6 | LQFP-64 (10x10) | 主控 |
| U7 | E104-BT5005A | 模块 | BLE |
| U8 | 充电管理芯片 | QFN-16 (3x3) | 电池充电 + 电源路径 |
| U9 | LDO | SOT-23-5 | 系统电源稳压 |
| U12 | MAX30003 | QFN-28 (5x5) | ECG 前端 |
| U14 | ICS-43432 | 传感器-SMD | MEMS 麦克风 |
| U23 | 使能控制 | SOT-23-5 | EN_3 电源使能 |
| D1, D2 | ESD 保护 | USON-10 (2.5x1.0) | SD 卡信号 ESD |
| D3 | USB ESD | SOT-23-6 | USB 保护 |
| D4, D5 | 二极管 | SOD-323 | ECG 输入保护 |
| D6 | 二极管 | SOD-323 | 电池反向保护 |
| Q1 | MOSFET | SOT-23-3 | LCD 背光控制 |
| U10, U11 | LED 红色 | LED0402 | 充电/状态指示 |
| R1 | 10kΩ | R0402 | NRST 上拉 |
| R2, R25 | 10kΩ | R0603 | BOOT0 |
| R3 | 22Ω | R0402 | SD_CLK 串联 |
| R4 | 100kΩ | R0402 | (下拉) |
| R5 | 10kΩ | R0402 | BLE MOD 下拉 (→待改上拉) |
| R6 | 4.7kΩ ×8 | 阵列-0402×8 | I2C2 触控 排阻 |
| R7 | 100Ω | R0402 | LCD_BLK |
| R8 | 10kΩ | R0402 | (上拉) |
| R9, R10 | 22Ω | R0402 | USB D+/D- 串联 |
| R11 | 3.3kΩ | R0402 | |
| R12 | 2.2kΩ | R0402 | |
| R13 | 100kΩ | R0402 | 电池分压 |
| R14 | 10kΩ | R0402 | |
| R15 | 10kΩ | R0402 | |
| R16 | 453kΩ | R0402 | LDO 反馈 |
| R17, R18 | 100kΩ | R0402 | |
| R19, R20 | 2.2kΩ | R0402 | LED 限流 (U10, U11) |
| R22, R24 | 1kΩ | R0402 | ECG 输入限流 |
| R23 | 4.7kΩ | R0402 | ECG_INT 上拉 |
| R26 | 499kΩ | R0402 | ECG 偏置 |
| R27 | 0Ω | R0402 | ECG_FCLK 串联 (→待改22Ω R21) |
| RN1, RN2 | 47kΩ ×4 | 阵列-0402×4 | SD 数据线 上拉 |
| C1, C2 | 12pF | C0402 | 32.768k 晶振负载 |
| C3-C7, C9, C10, C13, C19, C31, C32, C35, C36 | 100nF | C0402 | 去耦 |
| C8, C11, C12, C15-C17, C21, C22, C33 | 10μF | C0402 | 去耦/滤波 |
| C14 | 1μF | C0402 | VIN_5V 滤波 |
| C18 | 22μF | C0402 | 大容量滤波 |
| C20 | 1μF | C0402 | |
| C23, C28 | 10pF | C0402 | ECG 滤波 |
| C24 | 1μF | C0603 | ECG 滤波 |
| C25 | 1μF | C0402 | |
| C26 | 1nF | C0402 | |
| C27 | 1μF | C0402 | |
| C29 | 1μF | C0402 | |
| C30 | 1μF | C0402 | VCC1V8 |
| C34 | 1μF | C0402 | VCC_LDO |
| L1 | 2.2μH | IND-2520 | DC-DC 电感 |
| L2, L3 | — | L0402 | 磁珠/电感 |
| X1 | 32.768kHz | 晶振-2012 | RTC 时钟 |
| SW1 | — | KAN1542 | NRST 复位键 |
| SW2 | — | KAN1542 | KEY 按键 |
| CN1 | — | 1.5mm-2P | 电池插座 |
| FPC1 | — | 18P-0.50mm | LCD+触控 FPC |
| FPC2 | — | 10P-0.50mm | 生物子板 FPC |
| CARD1 | — | TF-PUSH | TF 卡座 |
| H1 | — | 4P-2.54mm | USB 排针 |
| H2 | — | 2P-2.54mm | 电池 排针 |
| AUDIO1 | PJ-316A-6A | 3.5mm-6P | ECG 导联接口 |
| U2-U6 | — | testtest | 测试点 |

### 生物子板 (sasV9bio)

| 位号 | 规格 | 封装 | 功能 |
|------|------|------|------|
| U2 | ICM20948 | QFN-24 (3x3) | IMU 9轴 |
| U3 | MAX30102 | OESIP-14 (5.6x3.3) | PPG 血氧 |
| U5 | TXS0104ERGYR | VQFN-14 (3.5x3.5) | I2C 电平转换 |
| FPC1, FPC2 | — | 10P-0.50mm | 到主板 FPC2 连接器 |
| R4 | 100kΩ | R0603 | |
| R5-R8, R10 | 0Ω | R0603 | 跳线配置 (I2C/INT) |
| R9 | 4.7kΩ | R0603 | I2C 上拉 (3.3V侧) |
| L1 | — | L0603 | 磁珠 |
| C10, C12, C15, C18, C19 | 100nF | C0603 | 去耦 |
| C13, C17, C20 | 10μF | C0603 | 去耦 |

---

## 关键引脚映射

### 主控 MCU → 外设

| MCU 引脚 | 信号名 | 连接目标 | 备注 |
|----------|--------|----------|------|
| PA1 (U1.15) | KEY | SW2 按键 | 低有效 |
| PA2 (U1.16) | LCD_RST | FPC1.8 → LCD | |
| PA4 (U1.24) | SPI1_NS | FPC1.11 → LCD CS | |
| PA5 (U1.21) | SPI1_CLK | FPC1.10 → LCD | |
| PA7 (U1.23) | SPI1_MOSI | FPC1.9 → LCD | |
| PB1 (U1.27) | LCD_BLK | R7 → Q1 → FPC1.17 | 背光 PWM |
| PC4 (U1.25) | LCD_DC | FPC1.12 | |
| PB6 (U1.58) | ECG_INT | R23 上拉 → U12.21 | EXTI 下降沿 |
| PB7 (U1.59) | SD_DETECT | CARD1.CD + RN1 | 卡检测 |
| PB11 (U1.30) | SDA2_TOUCH | R6 → FPC1.5 | I2C2 触控 |
| PB13 (U1.34) | SCL2_TOUCH | R6 → FPC1.6 | I2C2 触控 |
| PB14 (U1.17) | I2C2_INT | R6 → FPC1.3 | 触控中断 |
| PA12 (U1.37) | LINK | U7.13 | BLE 连接状态 |
| PA13 (U1.46) | SWDIO | U4 测试点 | SWD 调试 |
| PA14 (U1.49) | SWCLK | U3 测试点 | SWD 调试 |
| PA15 (U1.38) | RST_BLUE | U7.3 | BLE 模块复位 |
| PC0 (U1.8) | I2C3_SCL | FPC2.9 → 生物板 | |
| PC1 (U1.9) | I2C3_SDA | FPC2.8 → 生物板 | |
| PC2 (U1.10) | PPG_INT | FPC2.7 → 生物板 | EXTI 下降沿 |
| PC3 (U1.11) | SAI1_SD | U14.7 | MIC 数据 |
| PC6 (U1.29) | SAI1_SCK | U14.6 | MIC 时钟 |
| PC7 (U1.62) | SAI1_FS | U14.5 | MIC 帧同步 |
| PC8 (U1.39) | SD_D0 | CARD1.7 + RN1 | SDMMC D0 |
| PC9 (U1.40) | SD_D1 | CARD1.8 + RN1 | SDMMC D1 |
| PC10 (U1.51) | SD_D2 | CARD1.1 + RN2 | SDMMC D2 |
| PC11 (U1.52) | SD_D3 | CARD1.2 + RN2 | SDMMC D3 |
| PC12 (U1.53) | SD_CLK | R3 → CARD1.5 | SDMMC CLK |
| PD2 (U1.54) | SD_CMD | CARD1.3 + RN2 | SDMMC CMD |
| PD5 (U1.50) | MOD | R5 → GND / U7.9 | BLE 模式 (下拉, 待改上拉) |
| PD6 (U1.42) | UART1_TXD | U7.11 | BLE 发送 |
| PD7 (U1.43) | UART1_RXD | U7.12 | BLE 接收 |
| PD8 (U1.44) | USB_DM1 | R10 → D3 | USB DM |
| PD9 (U1.45) | USB_DP1 | R9 → D3 | USB DP |
| PE6 (U1.2) | SPI3_CS | U12.15 | ECG CS |
| PE7 (U1.41) | ECG_FCLK | R27 → U12.14 | ECG 参考时钟 |
| PF12 (U1.55) | SPI3_CLK | U12.16 | ECG SCLK |
| PF13 (U1.56) | SPI3_MISO | U12.18 | ECG SDO |
| PF14 (U1.57) | SPI3_MOSI | U12.17 | ECG SDI |
| PH1 (U1.6) | ICM_INT | FPC2.6 → 生物板 | EXTI 下降沿 |
| PC14 (U1.3) | OSC32_IN | X1.2 + C1 | RTC 晶振 |
| PC15 (U1.4) | OSC32_OUT | X1.1 + C2 | RTC 晶振 |
| BOOT0 (U1.60) | — | R2 + R25 分压 | 启动选择 |
| NRST (U1.7) | — | R1 上拉 + SW1 | 复位 |

### 3.5mm 音频接口 (AUDIO1 PJ-316A-6A)

| 引脚 | 连接 | 功能 |
|------|------|------|
| Pin1 | R26 (499kΩ) → VCM (U12.25) | ECG 偏置电压 |
| Pin3 | D4 → R24 (1kΩ) → MAX_ECGN (U12.6) | ECG 负输入 ⚠️ 实物短接到 Pin2 |
| Pin5 | D5 → R22 (1kΩ) → MAX_ECGP (U12.7) | ECG 正输入 ⚠️ 原理图错接 |

> **已知问题**：Pin2 未连接，导联线插头实际接触在 Pin5 而非 Pin2。实物已短接 Pin2-Pin5。下版原理图应把导联改到 Pin2。

### FPC1 (18P LCD+触控)

| 引脚 | 信号 | 备注 |
|------|------|------|
| 1 | GND | |
| 2 | VCC | |
| 3 | I2C2_INT (PB14) | 触控中断 |
| 4 | RST_TOUCH | 触控复位 |
| 5 | SDA2_TOUCH (PB11) | I2C2 数据 |
| 6 | SCL2_TOUCH (PB13) | I2C2 时钟 |
| 7 | GND | |
| 8 | LCD_RST | LCD 复位 |
| 9 | SPI1_MOSI | |
| 10 | SPI1_CLK | |
| 11 | SPI1_NS | CS |
| 12 | LCD_DC | |
| 13-14 | GND | |
| 15-16 | VCC | |
| 17 | Q1 (MOSFET → 背光) | 背光控制 |
| 18-20 | GND | |

### FPC2 (10P 生物子板)

| 引脚 | 信号 |
|------|------|
| 1-2 | GND |
| 4-5 | VCC (3.3V) |
| 6 | ICM_INT |
| 7 | PPG_INT |
| 8 | I2C3_SDA |
| 9 | I2C3_SCL |
| 10 | VCC1V8 |
| 11-12 | GND |

### 电源树

```
VIN_5V (USB H1.4)
  ├── D3 (USB ESD)
  └── U8 (充电管理 QFN-16)
        ├── CN1 → 锂电池
        ├── SYS → U9 (LDO SOT-23-5) → VCC (3.3V)
        ├── BAT_RAW → R13/R17 分压 → U1.26 (ADC)
        ├── CHG_N → U11 LED (充电指示)
        ├── PGOOD_N → U10 LED (电源好指示)
        └── U9 反馈: R16/R18 (453k/100k)
        
VCC (3.3V)
  ├── MCU, BLE, LCD, Touch, SD, MIC
  └── FPC2 → 生物子板

U23 (SOT-23-5 使能): EN_3 → U1.33 (控制)

生物子板:
  VCC (3.3V) → U5 TXS0104 B侧
  VCC → U5 → VCC1V8 (1.8V) → U5 A侧
  VCC1V8 → ICM20948 + MAX30102 (核心电压)
  VCC → MAX30102 LED供电
```

---

## 已知设计问题

| 日期 | 问题 | 当前状态 | 下版修改 |
|------|------|----------|----------|
| 05-30 | BLE MOD R5 下拉至 GND，应为上拉 | 实物：R5 已拆卸 | 原理图：R5 改上拉到 VCC |
| 05-30 | ECG 导联接在 AUDIO1 Pin5，Pin2 未连 | 实物：短接 Pin2-Pin5 | 原理图：导联直接接 Pin2 |
| 05-30 | ECG_FCLK R27=0Ω，需 22Ω 阻抗匹配 | 实物：换 22Ω (R21) | 原理图：更新 R27 为 22Ω |

---

## 下一版设计修改计划

| 模块 | 变更 | 原因 |
|------|------|------|
| ECG 接口 | 导联信号改到 AUDIO1 Pin2 | Pin5 不是 MAX 输入脚 |
| BLE | R5 改为上拉 (10kΩ → VCC) | MOD 脚需高电平进入正常模式 |
| FCLK | R27 改为 22Ω | 阻抗匹配 |