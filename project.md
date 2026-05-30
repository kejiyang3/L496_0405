# L496_0405 项目状态总表

> 维护者: codex2 | 最后更新: 2026-05-30

---

## 硬件

| 模块 | 原理图 | 实物 | 最近变更 |
|------|:--:|:--:|------|
| 主控+电源 | 待确认 | 待确认 | — |
| ECG前端(MAX30003) | 待确认 | R27 12Ω + FCLK短接 | R27 0Ω→12Ω (05-28) |
| PPG前端(MAX30102) | v1.0 ✅ | 待确认 | — |
| IMU(ICM20948) | 待确认 | 待确认 | — |
| BLE(E104-BT5005A) | 待确认 | R5上拉待确认 | — |
| 显示+触控 | 待确认 | LCD/触控待确认 | — |
| SD卡 | 待确认 | TF卡座待确认 | — |
| PCB | — | 待确认 | — |

> 详见 hardware/schematic.md / hardware/physical.md

---

## 软件

| 模块 | 文件 | 状态 | 说明 |
|------|------|:--:|------|
| ECG采集 | Core/Src/max3003.c | ✅ | MAX30003 SPI FIFO, 128Hz |
| PPG采集 | Core/Src/Max30102.c | ✅ | MAX30102 I2C FIFO, INT驱动 |
| IMU采集 | Core/Src/icm20948.c | ✅ | ICM20948 I2C, Data Ready中断驱动 |
| 多传感器记录 | Core/Src/multi_sensor_logger.c | ✅ | 2秒块双缓冲CSV |
| SD卡 | Core/Src/sd_sensor_logger.c | ✅ | FatFS+SDMMC |
| LVGL显示 | App/LVGL/app_lvgl.c | ⚠️ | 重构中，当前2页基线 |
| 触控 | App/LVGL/lv_port_indev.c | ✅ | CST816 I2C |
| **BLE通信** | Core/Src/freertos.c | 🔴 | **UART1 DMA框架就绪，状态机空壳** |
| 录音控制 | Core/Src/ecg_record_control.c | ✅ | 启停+状态机 |
| 音频 | Core/Src/audio_recorder.c | ✅ | SAI MIC, 8kHz |
| 按键 | Core/Src/freertos.c | ✅ | PA1软件消抖 |

---

## 当前重点

- [ ] **BLE 状态机实现** ← 当前任务
- [ ] LVGL 显示重构
- [ ] 硬件实物状态确认

---

## 变更记录

| 日期 | 类型 | 摘要 | 结果 |
|------|------|------|:--:|
| 2026-05-30 | 软件 | BLE状态机实现 | 进行中 |