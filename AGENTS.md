# AGENTS.md

本文件为�?Codex / 其他 AI agent 环境中工作的代码代理提供项目级指导�?

## 项目概览

- **项目名称**: L496_0405（可穿戴多传感器健康监测设备�?
- **MCU**: STM32L496 (Cortex-M4)
- **RTOS**: FreeRTOS
- **构建系统**: CMake + Ninja + arm-none-eabi-gcc
- **UI框架**: LVGL (v8.x)
- **编程语言**: C (C11), Python (辅助工具), 少量汇编

### 传感器架�?
| 传感�?| 型号 | 接口 | 采样�?|
|--------|------|------|--------|
| ECG (心电) | MAX30003 | SPI3 | 512 SPS |
| PPG (血氧脉�? | MAX30102 | I2C3 | 200 SPS |
| IMU (惯�? | ICM20948 | I2C3 | 51 Hz |

### 其他外设
- ST7789V 240x280 LCD (SPI1 + DMA)，背�?PWM
- CST816 电容触摸 (I2C2)
- USB CDC 虚拟串口 (不稳定，高频输出时易�?
- BLE (UART1 + DMA, 空闲中断)
- SD�?(SDMMC + FatFS)
- 按键 (PA1, 低电平有�? 软件消抖)
- 电平转换芯片 TXS0104ERGYR (I2C3 总线, 1.8V�?.3V)

## 构建命令

```bash
# 配置 (首次�?CMakeLists.txt 变更�?
cd build && cmake .. -G Ninja

# 编译
cd build && ninja

# 从项目根编译
ninja -C build

# 烧录
.\flash.bat

# 串口监控
.\monitor.bat
```

## 代码约定 重要

1. **所有代码注释用中文**，回答尽量用中文
2. **CLAUDE.md** 是项目最详细的参考文档，包含完整的硬件引脚映射、中断链、数据流、任务优先级等信息。遇到架构问题先查阅 CLAUDE.md
3. **DEBUG_NOTES.md** 包含电平转换调试笔记、断点状态码和预期日志输�?
4. 项目使用 CubeMX 生成 HAL 层代码，�?CubeMX 生成后不会再重新生成 CMakeLists.txt 中的用户代码�?(USER CODE BEGIN/END)
5. 不要�?SD 写入路径中使�?`f_mount(NULL, ...)` —�?多个写入器共享同一个卷

## 关键文件地图

| 文件 | 内容 |
|------|------|
| `Core/Src/freertos.c` | 所�?RTOS 任务定义、传感器初始化、PPG诊断、ICM自检 |
| `Core/Src/main.c` | HAL初始化、GPIO EXTI回调、IRQ计数�?|
| `Core/Src/max3003.c` | ECG SPI驱动、FIFO突发读取 |
| `Core/Src/Max30102.c` | PPG I2C驱动、FIFO批量读取、中断控�?|
| `Core/Src/icm20948.c` | IMU I2C驱动、Bank切换�?轴读�?|
| `Core/Src/multi_sensor_logger.c` | 2秒块双缓�?多传感器CSV写入 |
| `Core/Src/sd_sensor_logger.c` | PPG中断诊断队列+写入 |
| `Core/Src/sd_debug_log.c` | SD卡调试日�?|
| `Core/Src/ecg_record_control.c` | ECG录音启停控制 |
| `Core/Src/ecg_usb_dump.c` | ECG数据USB导出 |
| `App/LVGL/app_lvgl.c` | LVGL 2页面UI (主页+诊断�? |
| `Core/Src/sd_diag.c` | SD 卡底层诊断固�?(SD_DIAG_MODE) |
| `Core/Inc/sd_diag.h` | SD 诊断头文�?|
| `hardware/MAX30102_心率采集/` | MAX30102 电路设计 (原理�?netlist/EasyEDA) |
| `Core/Inc/sensor_record.h` | 传感器记录联合体定义 |
| `Core/Inc/app_log.h` | USB日志开�?`USB_LOG_ENABLE` |
| `Core/Inc/main.h` | 引脚宏、`ICM_INT_LINE_PULLDOWN_TEST_ENABLE` |
| `Core/Inc/FreeRTOSConfig.h` | RTOS配置、堆栈大�?(�?1920字节) |

## Python 工具 (辅助分析)

| 文件 | 用�?|
|------|------|
| `python/analyze_ecg.py` | ECG CSV批量/单文件分�?|
| `python/plot_ecg_folder.py` | ECG CSV快速绘�?|
| `python/check_ecg_cal_1hz.py` | 1Hz校准检�?|
| `python/stm32_cdc_monitor.py` | USB CDC 串口监控（支持重连） |
| `python/stm32_simple_monitor.py` | 最小化USB CDC串口监控 |
| `python/dev_loop.py` | 开发循环脚�?|

�?`_` 开头的 python 文件是一次性调�?修复脚本，不应再修改�?

## 数据流架�?

```
中断 (EXTI) �?ISR (irq_count++) �?xTaskNotifyGive �?传感器任�?
�?MultiSensorLogger_AddXXX() �?2秒双缓冲�?�?submit_xxx_block()
�?MS_BlockMsg_t 队列 (深度12) �?Task_MSWriter �?SD�?CSV
```

## 重要注意

- **USB CDC 不稳�?*: 高频持续输出�?USB CDC 会断开，仅用于短时诊断输出。使�?`USB_LOG_ENABLE` 宏控�?
- **中断仅在 ISR 中递增计数�?+ 通知任务**，不�?ISR 中操�?I2C/SPI/SD
- **MAX30102 INT 协议**: INT 是开漏低电平有效，读�?INTERRUPT_STATUS1 �?FIFO 才能释放 INT 引脚回到高电平。不读则 EXTI 只触发一�?
- **构建警告**: HAL/ThirdParty/FatFs 中有预期�?`-Wunused-parameter` 等警告，不是你需要修复的
- **PDF/图片处理**: 不可直接读取二进制PDF/图片，需先运�?`python scripts/ingest_file.py <文件路径>` 转换�?markdown 再分�?

## Git 信息

- 当前分支: `master`
- 有大量未提交修改和未跟踪文件（`_` 前缀�?python 脚本为一次性调试用�?
- 新分支命名建议前缀: `codex/`

## 新装技�?(2026-05-28)

| 技�?| 来源 | 用�?|
|------|------|------|
| embed-ai-tool | LeoKemp223/embed-ai-tool | 24 子技能：编译/烧录/调试/串口/RTOS/静态分�?workflow |
| embedded-development | Aidankong/embedded-development-skill | STM32 CubeMX/FreeRTOS/HAL/DMA/功�?|
| embedded-engineering | 2456018331lby-dev/embedded-engineering-skill | 硬件设计/立创EDA/KiCad/PCB/BOM |
| superpowers | obra/superpowers | 开发全流程：brainstorming→TDD→debug→review |
| anysearch-skill | anysearch-ai/anysearch-skill | 联网搜索 |

## MCP 服务

- `findskills`: npx findskills-mcp（技能搜索引擎）
- `codegraph`: npx @cartographai/mcp-server-codegraph（代码依赖图�?

## 闭环开�?

使用 `embed-ai-tool/workflow` 替代旧的 bat/python 脚本�?

```powershell
# 编译 + 烧录 + 串口监控 一条命�?
python skills\embed-ai-tool\workflow\scripts\workflow_runner.py --build-system cmake --project-path . --flash-method openocd --monitor
```

旧脚�?(`flash.bat`, `monitor.bat`, `stm32_cdc_monitor.py`, `stm32_simple_monitor.py`, `dev_loop.py`) 已被替代，保留但不再主动使用�?

## SD 卡诊�?

- 启用: `Core/Inc/main.h` �?`#define SD_DIAG_MODE`
- 代码: `Core/Src/sd_diag.c` + `Core/Inc/sd_diag.h`
- 运行�?Sensor 任务�?`SD_DebugLog_Init()` 之后
- 输出�?USB CDC，逐步报告 SDMMC 初始化、引脚电平、卡检测、挂载结�?
- 已知: USB CDC 枚举正常 (COM7) 但输出未到达——待排查

## codegraph (代码依赖�?

- 安装: 
px github:colbymchenry/codegraph build .
- 查询: 
px github:colbymchenry/codegraph <命令>
- MCP: 已配置在 %USERPROFILE%\.codex\.mcp.json，指�?D:\code\bihuan\L496_0405\.codegraph\graph.db

### 常用命令

| 命令 | 用�?|
|------|------|
| stats | 图概�?(节点/�?热点) |
| deps <file> | 文件导入导出关系 |
| impact <file> | 传递依赖影响分�?|
| complexity | 函数复杂度指�?|
| rief <file> | 文件符号摘要 |
| uild . | 重建�?(代码变更�? |
| mcp -d .codegraph/graph.db | 启动MCP服务�?|



## �����ٲ飨2026-05-29 ��֤��
- **ICM_INT = PH1** �� EXTI1��ע�⣺gpio.c ע��д���ˣ��� main.h �궨��� .ioc Ϊ׼��
- **PPG_INT = PC2** �� EXTI2
- stm32l4xx_it.c �� EXTI1��ICM_INT_Pin, EXTI2��PPG_INT_Pin�������������� ICM �жϲ�����
- �ջ�����ʱ��� `python pc\usb_sd_pull.py --port COMx clear` ���SD���ļ�
---

## 项目文档结构（Codex 自动维护）

每次改动后，Codex 自动更新以下文件：

| 文件 | 内容 | 维护方 |
|------|------|--------|
| `project.md` | **项目总表**：硬件/软件/外壳三模块状态一览 | Codex |
| `hardware/schematic.md` | 原理图 + 引脚映射 | 你提供→Codex更新 |
| `hardware/physical.md` | 实物 BOM + PCB + 变更记录 | 你提供→Codex更新 |
| `enclosure.md` | 外壳设计 + 版本 | 你提供→Codex更新 |
| `changes/` | 变更卡片（每次改动手前写） | Codex |

### 变更卡片命名：`changes/YYYY-MM-DD-简短描述.md`
## INBOX 整理规则

`INBOX.md` 是随手记录文件。Codex 在以下时机处理：
1. 每次用户提到新信息时，主动查看 INBOX 是否有待整理内容
2. 每次改动完成后，检查 INBOX 是否有相关条目需要关联

整理时按内容分拣：
- 硬件问题/想法 → `hardware/physical.md` 或 `hardware/schematic.md`
- 软件问题/想法 → `project.md` 软件部分
- 外壳问题/想法 → `enclosure.md`
- 综合性问题 → `project.md` 变更记录
- 处理完的条目移到 INBOX 底部「整理记录」表格