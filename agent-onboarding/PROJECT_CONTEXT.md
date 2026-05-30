# 项目技术上下文

> 本文档提供理解代码所需的全部技术背景。新 Agent 必读。

---

## 一、系统概览

| 属性 | 值 |
|------|-----|
| **项目名** | L496_0405 可穿戴多传感器健康监测设备 |
| **MCU** | STM32L496RET6 (Cortex-M4, 80MHz) |
| **RTOS** | FreeRTOS (CMSIS-RTOS v2 API) |
| **UI 框架** | LVGL v8.3.11 |
| **构建系统** | CMake + Ninja |
| **编译器** | arm-none-eabi-gcc 14.3.1 |
| **语言标准** | C11 |
| **Flash 占用** | ~458 KB / 512 KB (89%) |
| **RAM 占用** | ~222 KB / 320 KB (85%) |

---

## 二、传感器架构

| 传感器 | 型号 | 接口 | 采样率 | 中断引脚 |
|--------|------|------|--------|----------|
| ECG（心电） | MAX30003 | SPI3 | 512 SPS | PB6 (EXTI9_5, 下降沿) |
| PPG（血氧脉搏） | MAX30102 | I2C3 (经 TXS0104) | 200 SPS | PC2 (EXTI2, 下降沿) |
| IMU（惯性） | ICM20948 | I2C3 (经 TXS0104) | 51 Hz | PH1 (EXTI1, 下降沿) |

---

## 三、完整引脚映射

### 显示

| MCU 引脚 | 信号 | 说明 |
|----------|------|------|
| PA2 | LCD_RST | ST7789V 复位 |
| PA5 | SPI1_SCK | 时钟 |
| PA7 | SPI1_MOSI | 数据 |
| PB1 | LCD_BLK | 背光 PWM (TIM3_CH4) |
| PC4 | LCD_CS | 片选 |
| PC5 | LCD_DC | 数据/命令 |

### 触控

| MCU 引脚 | 信号 | 说明 |
|----------|------|------|
| (I2C2) | I2C2_SCL/SDA | CST816 电容触摸 |

### SD 卡 (4-bit SDMMC)

| MCU 引脚 | 信号 |
|----------|------|
| PC8 | SDMMC1_D0 |
| PC9 | SDMMC1_D1 |
| PC10 | SDMMC1_D2 |
| PC11 | SDMMC1_D3 |
| PC12 | SDMMC1_CK |
| PD2 | SDMMC1_CMD |
| PB7 | SD_DETECT (卡检测开关) |

### BLE

| MCU 引脚 | 信号 | 说明 |
|----------|------|------|
| (UART1) | TX/RX | E104-BT5005A (nRF52805) |
| | DMA 接收 | 空闲中断 + DMA |

### I2C3 总线（电平转换后）

| MCU 引脚 | 信号 | 说明 |
|----------|------|------|
| PC0 | I2C3_SCL | 经 TXS0104ERGYR 1.8V↔3.3V |
| PC1 | I2C3_SDA | 经 TXS0104ERGYR 1.8V↔3.3V |

> ⚠️ I2C3 总线上挂 TXS0104 电平转换芯片。PPG (MAX30102) 和 IMU (ICM20948) 都走这条总线。

### 其他

| MCU 引脚 | 信号 | 说明 |
|----------|------|------|
| PA1 | BUTTON | 按键，低电平有效，软件消抖 |
| USB | CDC | 虚拟串口 |

---

## 四、FreeRTOS 任务表

| 任务 | 优先级 | 栈大小 | 功能 |
|------|--------|--------|------|
| `Task_Sensor` | AboveNormal | 8KB | ECG FIFO 读取 + 启停控制 |
| `Task_PPG` | Normal | 4KB | PPG FIFO 读取 |
| `Task_IMU` | Normal | 4KB | IMU 6 轴读取 |
| `Task_MSWriter` | BelowNormal | 8KB | 多传感器 CSV 写入 SD |
| `Task_LVGL` | Low | **8KB** | LVGL UI @ 10ms 刷新 |
| `Task_Button` | BelowNormal | 1KB | PA1 按键消抖 |
| `Task_BLE` | Low7 | 1KB | BLE 命令轮询 |
| `Task_Audio` | Normal1 | 1KB | 音频占位 |

> ⚠️ Task_LVGL 栈曾从 8KB 减到 4KB 导致显示异常。保持 8KB。

---

## 五、数据流

```
硬件中断 (EXTI) 
  → ISR: irq_count++ + xTaskNotifyGive 
    → 传感器任务: 读 FIFO 
      → MultiSensorLogger_AddXXX(): 双缓冲
        → submit_xxx_block(): 打包
          → MS_BlockMsg_t 队列 (深度 12)
            → Task_MSWriter: 写 SD 卡 CSV
```

**关键原则**：
- ISR 只做计数器递增 + 通知任务
- 绝不在 ISR 中操作 I2C/SPI/SD
- 数据通过 FreeRTOS 队列在任务间传递

---

## 六、关键文件地图

### 核心固件

| 文件 | 内容 |
|------|------|
| `Core/Src/main.c` | HAL 初始化、GPIO EXTI 回调、IRQ 计数器 |
| `Core/Src/freertos.c` | 所有 RTOS 任务定义、传感器初始化 |
| `Core/Src/max3003.c` | ECG SPI 驱动、FIFO 突发读取 |
| `Core/Src/Max30102.c` | PPG I2C 驱动、FIFO 批量读取 |
| `Core/Src/icm20948.c` | IMU I2C 驱动、Bank 切换 |
| `Core/Src/multi_sensor_logger.c` | 2 秒块双缓冲 + 多传感器 CSV 写入 |
| `Core/Src/sd_sensor_logger.c` | PPG 中断诊断队列 + SD 写入 |
| `Core/Src/sd_debug_log.c` | SD 卡调试日志 |
| `Core/Src/ecg_record_control.c` | ECG 录音启停控制 |
| `Core/Src/ecg_usb_dump.c` | ECG 数据 USB 导出 |
| `Core/Src/sd_diag.c` | SD 卡底层诊断（`SD_DIAG_MODE`） |
| `App/LVGL/app_lvgl.c` | LVGL 2 页面 UI（主页 + 诊断页） |

### 关键头文件

| 文件 | 关键内容 |
|------|---------|
| `Core/Inc/main.h` | 引脚宏定义、`SD_DIAG_MODE`、`ICM_INT_PULLDOWN_TEST_ENABLE` |
| `Core/Inc/FreeRTOSConfig.h` | RTOS 配置、堆栈大小 |
| `Core/Inc/sensor_record.h` | 传感器记录联合体 |
| `Core/Inc/app_log.h` | `USB_LOG_ENABLE` 宏 |
| `Core/Inc/sd_diag.h` | SD 诊断接口 |

### 硬件设计

| 目录/文件 | 内容 |
|-----------|------|
| `hardware/MAX30102_心率采集/` | MAX30102 电路设计（原理图/EasyEDA） |

### 文档

| 文件 | 内容 |
|------|------|
| `CLAUDE.md` | 最详细的架构参考（引脚、中断链、任务优先级） |
| `DEBUG_NOTES.md` | 电平转换调试笔记、断点状态码、预期日志 |
| `AGENTS.md` | 项目级 Agent 指令 |
| `project.md` | 项目总表（硬件/软件/外壳状态） |
| `INBOX.md` | 随手记录，Agent 定期分拣 |

---

## 七、关键技术细节 / 陷阱

### 7.1 MAX30102 INT 协议（重要！）

```
MAX30102 INT 引脚是开漏输出，低电平有效。
中断触发后，必须读取 INTERRUPT_STATUS1 寄存器并清空 FIFO，
INT 引脚才会释放回高电平。

如果不读 STATUS1/FIFO，INT 保持低电平，EXTI 只触发一次。
```

### 7.2 USB CDC 不稳定

高频持续输出时 USB CDC 虚拟串口会断开。
- 使用 `USB_LOG_ENABLE` 宏控制输出
- 仅用于短时诊断，不做为主要通信手段

### 7.3 SD 卡挂载

多个写入器（`multi_sensor_logger`、`sd_sensor_logger`、`sd_debug_log`）
共享同一个 FatFS 卷。**不要在写入路径中调用 `f_mount(NULL, ...)`**。

### 7.4 Flash Verify 校验和不匹配

烧录时偶尔出现 verify checksum mismatch。
- 这是已知问题，不是你的代码导致的
- 加 `--no-verify` 参数跳过验证
- 闪存内容通常正确写入

### 7.5 I2C3 电平转换

I2C3 总线经过 TXS0104ERGYR 做 1.8V ↔ 3.3V 电平转换。
PPG (MAX30102) 和 IMU (ICM20948) 都挂在这条总线上。
注意 TXS0104 的方向自动检测特性，以及上拉电阻配置。

### 7.6 构建警告

以下文件的编译警告是**预期的，不要修复**：
- `Middlewares/Third_Party/FatFs/` — `-Wunused-parameter`
- `Drivers/STM32L4xx_HAL_Driver/` — `-Wunused-parameter`

---

## 八、已知问题

| 问题 | 状态 | 备注 |
|------|------|------|
| USB CDC 高频输出断开 | 待解决 | 硬件/驱动层面 |
| Flash verify checksum mismatch | 已知 | `--no-verify` 绕过 |
| LVGL 显示：背光亮无内容 | 已诊断 | 栈大小已修复为 8KB，待 ST-Link 重连后烧录验证 |
| BLE 通信未实现 | 最高优先 | Task_BLE 空壳，待开发协议 |
| ST-Link 状态 Unknown | 待处理 | 需物理 USB 重新插拔 |

---

## 九、常用调试命令

```bash
# 闭环验证
python skills\embed-ai-tool\workflow\scripts\workflow_runner.py --run build-flash-monitor --build-system cmake --project . --flash-target target/stm32l4x.cfg --port COM7

# 仅编译
ninja -C build

# SD 卡拉取文件列表
cd sd_debug_tool && python pc\usb_sd_pull.py --port COM7 list

# SD 卡清空
cd sd_debug_tool && python pc\usb_sd_pull.py --port COM7 clear

# 飞书 CLI 查询
lark-cli base +record-list --base-token MD7tbxn1JasfKFsRidPcKjgNncg --table-id tblkoStalzndqyqO

# 飞书 CLI 批量更新
lark-cli base +record-upsert --base-token MD7tbxn1JasfKFsRidPcKjgNncg --table-id tblkoStalzndqyqO --json @update.json
```