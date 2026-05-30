# 硬件 — 原理图

> 负责人：你  |  文档维护：Codex（根据你提供的信息更新）

---

## 系统框图

```
STM32L496RET6
├── SPI1 → ST7789V (240x280 LCD)
├── SPI3 → MAX30003 (ECG)
├── I2C2 → CST816 (触控)
├── I2C3 → TXS0104ERGYR → MAX30102 (PPG) + ICM20948 (IMU)
├── SDMMC → TF 卡 (4-bit)
├── UART1 → E104-BT5005A (BLE)
├── SAI → MEMS MIC
├── USB → CDC 虚拟串口
├── TIM3_CH4 → LCD 背光 PWM (PB1)
└── PA1 → 按键 (低电平有效)
```

## 原理图清单

| 模块 | 设计文件 | 版本 | 状态 |
|------|---------|------|------|
| 主控 + 电源 | （待提供） | — | 🔶 |
| ECG (MAX30003) | （待提供） | — | 🔶 |
| PPG (MAX30102) | `hardware/MAX30102_心率采集/` | v1.0 | ✅ |
| IMU (ICM20948) | （待提供） | — | 🔶 |
| 显示 (ST7789V) | （待提供） | — | 🔶 |
| 触控 (CST816) | （待提供） | — | 🔶 |
| BLE (E104-BT5005A) | （待提供） | — | 🔶 |
| SD 卡 | （待提供） | — | 🔶 |
| 电平转换 (TXS0104) | （待提供） | — | 🔶 |

---

## 下一版设计修改计划

| 日期 | 模块 | 变更 | 原因 |
|------|------|------|------|
| 2026-05-30 | ECG 接口 | 导联信号改到 Pin2（MAX 输入脚） | 当前版导联接在 Pin5，下版修正到 Pin2 |

---

## 关键引脚映射

| MCU 引脚 | 信号 | 外设 | 备注 |
|----------|------|------|------|
| PA1 | BUTTON | 按键 | 低有效，软件消抖 |
| PA2 | LCD_RST | ST7789V | |
| PA5 | SPI1_SCK | ST7789V | |
| PA7 | SPI1_MOSI | ST7789V | |
| PB1 | LCD_BLK | 背光 PWM | TIM3_CH4 |
| PB6 | ECG_INT | MAX30003 | EXTI9_5，下降沿 |
| PB7 | SD_DETECT | TF 卡座 | 卡检测开关 |
| PC0 | I2C3_SCL | TXS0104→PPG/IMU | |
| PC1 | I2C3_SDA | TXS0104→PPG/IMU | |
| PC2 | PPG_INT | MAX30102 (经 TXS0104) | EXTI2，下降沿 |
| PC4 | LCD_CS | ST7789V | |
| PC5 | LCD_DC | ST7789V | |
| PC8-12 | SDMMC1_D0-D3,CK | TF 卡 | AF12，4-bit |
| PD2 | SDMMC1_CMD | TF 卡 | |
| PH1 | ICM_INT | ICM20948 (经 TXS0104) | EXTI1，下降沿 |