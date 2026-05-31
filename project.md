# L496_0405 — 可穿戴多传感器健康监测设备
> 最后更新: 2026-05-30  |  分支: master  |  构建: ✅ 通过

---

## 一、硬件
### 1.1 原理图
| 模块 | 文件 | 版本 | 状态 | 备注 |
|------|------|------|------|------|
| 主控 | — | — | 🔶 待录入 | STM32L496RET6 |
| ECG 前端 | — | — | 🔶 待录入 | MAX30003 + SPI3 |
| PPG 前端 | `hardware/MAX30102_心率采集/` | v1.0 | ✅ 已设计 | MAX30102 + TXS0104 |
| IMU | — | — | 🔶 待录入 | ICM20948 + I2C3 |
| 电源 | — | — | 🔶 待录入 | 1.8V / 3.3V LDO |
| BLE | — | — | 🔶 待录入 | E104-BT5005A (nRF52805) |
| 显示+触控 | — | — | 🔶 待录入 | ST7789V + CST816 |
| SD卡 | — | — | 🔶 待录入 | SDMMC + 4-bit |

### 1.2 实物（PCB/装配/BOM 变更）
| 日期 | 变更内容 | 原因 | 验证结果 |
|------|---------|------|---------|
| 2026-05-30 | FCLK R21 0Ω→22Ω | 阻抗匹配 | 🔶 待验证 |
| 2026-05-30 | BLE MOD R5 下拉拆卸 | 应为上拉，错装为下拉 | 🔶 待验证 |
| 2026-05-30 | ECG 3.5mm Pin2-Pin5 短接 | 导联接错到 Pin5，改回 Pin2 | 🔶 待验证 |
| 2026-05-28 | 更换 TF 卡座 | SD 卡无法正常通信 | ✅ 已验证 |

### 1.3 已知硬件问题
| 日期 | 问题 | 状态 |
|------|------|------|
| 05-30 | BLE R5 下拉错装，待补上拉 | 🔶 |
| 05-30 | ECG 导联错接 Pin5→Pin2 | 🔶 |
| 05-30 | LCD 只亮背光（软件侧待验证） | 🔶 |

---

## 二、软件
### 2.1 固件总览

| 属性 | 值 |
|------|-----|
| MCU | STM32L496RET6 (Cortex-M4) |
| RTOS | FreeRTOS (CMSIS-RTOS v2) |
| 构建 | CMake + Ninja + arm-none-eabi-gcc 14.3.1 |
| 工具链 | GNU Tools for STM32 (STM32Cube bundle) |
| UI | LVGL v8.3.11 |
| 当前版本 | **v0.3-core-three-modal-stable** |
| Flash 占用 | ~402 KB / 512 KB (78.5%) |
| RAM 占用 | ~237 KB / 320 KB (90.3%) |

### 2.2 功能模块

| 模块 | 文件 | 状态 | 负责人 | 说明 |
|------|------|------|--------|------|
| ECG 采集 | `Core/Src/max3003.c` | ✅ 正常 | Codex | MAX30003 SPI FIFO 突发读取 |
| PPG 采集 | `Core/Src/Max30102.c` | ✅ 正常 | Codex | MAX30102 I2C FIFO 批量读取 |
| IMU 采集 | `Core/Src/icm20948.c` | ✅ 正常 | Codex | ICM20948 I2C Bank 切换 |
| 多传感器记录 | `Core/Src/multi_sensor_logger.c` | ✅ 正常 | Codex | 2 秒块双缓冲 CSV 写入 |
| SD 卡 | `Core/Src/sd_sensor_logger.c` | ✅ 正常 | Codex | FatFS + SDMMC |
| SD 诊断 | `Core/Src/sd_diag.c` | ✅ 就绪 | Codex | `SD_DIAG_MODE` 启用 |
| **BLE 通信** | `Core/Src/ble_state_machine.c` | 🟡 进行中 | Codex | 命令解析完成(PING/START/STOP/STATUS/FNAME/INFO/SYNC)，待实机验证 |
| 录音控制 | `Core/Src/ecg_record_control.c` | ✅ 正常 | Codex | 启停 + 状态机 |
| 音频 | `Core/Src/audio_recorder.c` | ⚠️ 需排查 | Codex | 94s录音仅9.2s有效音频 |
| LVGL 显示 | `App/LVGL/app_lvgl.c` | ⚠️ 重构中 | Codex | 当前为 2 页基线版本 |
| 触控 | `App/LVGL/lv_port_indev.c` | ✅ 正常 | Codex | CST816 I2C |
| 按键 | `Core/Src/freertos.c:Task_Button` | ✅ 正常 | Codex | PA1 软件消抖 |
| 会话管理 | `Core/Src/session_manager.c` | ✅ 正常 | Codex | Session/Diag/Modality 摘要 |

### 2.3 FreeRTOS 任务

| 任务 | 优先级 | 栈 | 功能 |
|------|--------|-----|------|
| Task_Sensor | AboveNormal | 8KB | ECG FIFO 读取 + 启停控制 |
| Task_PPG | Normal | 4KB | PPG FIFO 读取 |
| Task_IMU | Normal | 4KB | IMU 6 轴读取 |
| Task_MSWriter | BelowNormal | 8KB | 多传感器 CSV 写入 |
| Task_LVGL | Low | 8KB | LVGL UI @ 10ms |
| Task_Button | BelowNormal | 1KB | PA1 消抖 |
| Task_BLE | Low7 | 1KB | BLE 命令解析 |
| Task_Audio | Normal1 | 1KB | 音频占位 |

### 2.4 构建与烧录
```powershell
# 编译
ninja -C build

# 烧录 (CubeIDE OpenOCD)
C:\ST\STM32CubeIDE_1.16.1\...\openocd.exe -s ...\st_scripts -f interface/stlink-dap.cfg -f target/stm32l4x.cfg -c "program build/L496_0405.elf verify reset exit"

# SD 卡拉取
cd sd_debug_tool && python pc\usb_sd_pull.py --port COM12 list
python pc\usb_sd_pull.py --port COM12 cat session.txt
```

### 2.5 待开发
| 优先级 | 模块 | 说明 |
|--------|------|------|
| 🔴 高 | BLE 实机验证 | 蓝牙串口工具发 PING，验 +PONG |
| 🔴 高 | 音频掉块修复 | 94s 录音仅 9.2s 有效音频 |
| 🔴 高 | LVGL UI 重构 | 三页滑动：表盘→ECG波形→设备信息 |
| 🟡 中 | ECG 512Hz 欠采样 | 实际 ~374Hz，96% 超时轮询 |
| 🟡 中 | USB CDC 稳定性 | 高频输出断开问题 |
| 🟢 低 | Flash 校验 | checksum mismatch 根因 |

---

## 三、外壳
| 属性 | 值 |
|------|-----|
| 状态 | 🔶 待设计 |
| 材料 | — |
| 3D 文件 | — |
| 备注 | — |

---

## 四、变更记录
近期变更卡片见 [`changes/`](changes/) 目录。
| 日期 | 卡片 | 类型 | 摘要 |
|------|------|------|------|
| 2026-05-30 | `2026-05-30-BLE状态机实现.md` | 软件 | BLE 命令解析 7条命令 ✅ |
| 2026-05-30 | `2026-05-30-硬件4项修改.md` | 硬件 | FCLK/BLE/ECG 4项修改 |
| 2026-05-30 | — | 软件 | 闭环开发流程跑通 |
| 2026-05-28 | — | 硬件 | 换板：四模态全部正常 |

---

## 飞书多维表格

| 属性 | 值 |
|------|-----|
| URL | https://zcno949fvw6r.feishu.cn/base/MD7tbxn1JasfKFsRidPcKjgNncg |
| base_token | `MD7tbxn1JasfKFsRidPcKjgNncg` |

### 数据表
| 表名 | table_id |
|------|----------|
| 硬件 | `tblEoO9jxdRfs9zc` |
| 软件 | `tblkoStalzndqyqO` |
| 变更记录 | `tblBG9rNbAvC6zUk` |