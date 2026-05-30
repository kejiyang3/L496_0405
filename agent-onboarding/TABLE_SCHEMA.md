# 飞书多维表格 — 表结构定义

> 飞书表格：https://zcno949fvw6r.feishu.cn/base/MD7tbxn1JasfKFsRidPcKjgNncg
> base_token: `MD7tbxn1JasfKFsRidPcKjgNncg`

---

## 表 1：硬件 (tblEoO9jxdRfs9zc)

每个模块 **原理图** 和 **实物** 各一行，1:1 对应。

| 字段 | 类型 | 说明 | 示例 |
|------|------|------|------|
| 模块 | text | 模块名称（原理图/实物用相同名称） | `ECG前端(MAX30003)` |
| 类型 | text | **原理图** 或 **实物** | `实物` |
| 版本 | text | 版本号 | `v1.0` |
| 状态 | text | 正常/进行中/异常/未开始 | `进行中` |
| 最近变更 | text | 最近一次改了什么 | `R27 0Ω→22Ω; Pin2-Pin5短接(05-30)` |
| 更新时间 | datetime | 自动记录 | `2026/05/30` |

### 模块列表（设计图 + 实物 一一对应）

| 模块 | 原理图 | 实物 |
|------|:--:|:--:|
| 主控+电源 | 设计进度 | 焊接/装配进度 |
| ECG前端(MAX30003) | 设计/网表 | R27/FCLK/接口 |
| PPG前端(MAX30102) | v1.0 | 焊接/验证 |
| IMU(ICM20948) | 设计进度 | 焊接/验证 |
| BLE(E104-BT5005A) | 设计进度 | R5上拉/模块 |
| 显示+触控 | 设计进度 | LCD/触控状态 |
| SD卡 | 设计进度 | TF卡座状态 |
| PCB | — | 总PCB状态 |

### 更新规则

- **实物变更** → 更新对应模块的"实物"行 + "变更记录"表 + `hardware/physical.md`
- **原理图变更** → 更新对应模块的"原理图"行 + `hardware/schematic.md`
- 两者都改 → 两边都更新

---

## 表 2：软件 (tblkoStalzndqyqO)

| 字段 | 类型 | 说明 | 示例 |
|------|------|------|------|
| 模块 | text | 功能模块名称 | `ECG采集` |
| 文件 | text | 主要源文件 | `Core/Src/max3003.c` |
| 状态 | text | 正常/异常/进行中/未开始 | `正常` |
| 负责人 | text | 谁在维护 | `Codex` |
| 说明 | text | 一句话描述 | `MAX30003 SPI FIFO` |
| 更新时间 | datetime | 自动记录 | `2026/05/30` |

### 当前数据（11 条）

| 模块 | 文件 | 状态 |
|------|------|------|
| ECG采集 | `Core/Src/max3003.c` | 正常 |
| PPG采集 | `Core/Src/Max30102.c` | 正常 |
| IMU采集 | `Core/Src/icm20948.c` | 正常 |
| 多传感器记录 | `Core/Src/multi_sensor_logger.c` | 正常 |
| SD卡 | `Core/Src/sd_sensor_logger.c` | 正常 |
| LVGL显示 | `App/LVGL/app_lvgl.c` | 异常 |
| 触控 | `App/LVGL/lv_port_indev.c` | 正常 |
| BLE通信 | `Core/Src/freertos.c` | 未开始 |
| 录音控制 | `Core/Src/ecg_record_control.c` | 正常 |
| 音频 | `Core/Src/audio_recorder.c` | 正常 |
| 按键 | `Core/Src/freertos.c` | 正常 |

---

## 表 3：变更记录 (tblBG9rNbAvC6zUk)

| 字段 | 类型 | 说明 | 示例 |
|------|------|------|------|
| 日期 | datetime | 变更日期 | `2026/05/30` |
| 类型 | text | 硬件/软件/外壳/综合 | `硬件` |
| 摘要 | text | 一句话描述 | `ECG_FCLK R27 0Ω→22Ω` |
| 结果 | text | 成功/失败/进行中 | `进行中` |
| 链接 | url | 变更卡片文件路径（可选） | `changes/2026-05-30-xxx.md` |

---

## 状态值约定

| 状态 | 含义 | 何时使用 |
|------|------|---------|
| 正常 | 功能完整，验证通过 | 闭环验收通过后 |
| 异常 | 有问题，待修复 | 发现 bug 时 |
| 进行中 | 正在开发/调试/修改 | 开工时 |
| 未开始 | 尚未动工 | 初始状态 |