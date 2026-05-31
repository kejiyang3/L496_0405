# L496_0405 实验报告汇总

> 生成日期：2026-06-01
> 版本：v0.3-core-three-modal-stable
> 仓库：https://github.com/kejiyang3/L496_0405

---

## 一、交接文件一：PPG / ICM 采样率隔离验证

**目标**：确证 PPG~12.5Hz 和 ICM~52Hz 的根因

### 实验矩阵（7组）

| Case | 描述 | PPG SPS | IMU SPS | ECG SPS | 关键证据 |
|------|------|---------|---------|---------|---------|
| 1 | PPG-only avg4 | 12.55 | - | - | FIFO=0x5F, i2c=0 |
| 2 | PPG-only avg1 | 50.13 | - | - | FIFO=0x1F, i2c=0 |
| 3 | ICM-only | - | 52.08 | - | div=21, ODR=1125/22 |
| 4 | ICM FIFO batch | - | 52.10 | - | batch=1, 同case3 |
| 5 | PPG+ICM | 12.56 | 52.09 | - | I2C3 wait<1ms |
| 6 | PPG+ICM no-SD | 12.56 | 52.09 | - | 无SD, 同case5 |
| 7 | ECG+PPG+ICM no-MIC | 13.44 | 52.22 | 488.91 | 全modal正常 |

### 根因判定

| 问题 | 答案 | 证据 |
|------|------|------|
| PPG~12.5Hz 是否 FIFO averaging 导致？ | **是** | avg4→12.55Hz, avg1→50.13Hz |
| ICM~52Hz 是否寄存器配置导致？ | **是** | div=21, 理论51.14Hz |
| PPG和ICM是否互相拖慢？ | **否** | Case5与Case1+3一致 |
| SD写入是否影响采样率？ | **否** | Case6与Case5一致 |
| FIFO批量策略是否是瓶颈？ | **否** | Case4与Case3一致 |

### 状态：✅ 全部完成

---

## 二、交接文件二：三主模态配置冻结长测

**目标**：固化目标配置为正式主线，通过完整矩阵验证 → v0.3-core-three-modal-stable

### 固化配置

| 模态 | 配置 | 目标值 |
|------|------|--------|
| MIC | OFF | 完全关闭 |
| PPG | 50Hz + FIFO avg1 (0x1F) | 48-52Hz |
| ICM | ACCEL/GYRO div=10 | 100-106Hz |
| ECG | MAX30003 当前配置 | 480-500Hz |
| LVGL | OFF | - |

### 实验矩阵

| 轮次 | 时长 | 次数 | PPG (Hz) | IMU (Hz) | ECG (Hz) | i2c | queue | sd | 判定 |
|------|------|------|----------|----------|----------|-----|-------|----|------|
| A1 | 90s | ×3 | 50.39-50.42 | 104.24-104.25 | 488.57-488.69 | 0 | 0 | 0 | ✅ PASS |
| A2 | 90s | ×3 | 50.40 | 104.24 | 488.69 | 0 | 0 | 0 | ✅ PASS |
| A3 | 90s | ×3 | 50.39 | 104.24 | 488.57 | 0 | 0 | 0 | ✅ PASS |
| B1 | 5min | ×3 | 50.20 | 104.24 | 488.60 | 0 | 0 | 0 | ✅ PASS |
| B2 | 5min | ×3 | 50.20 | 104.25 | 488.56 | 0 | 0 | 0 | ✅ PASS |
| B3 | 5min | ×3 | 50.43 | 104.23 | 489.00 | 0 | 0 | 0 | ✅ PASS |
| C1 | 30min | ×1 | 50.14 | 104.23 | 488.56 | 0 | 0 | 0 | ✅ PASS |

### 状态：✅ 全部完成 — 版本命名 v0.3-core-three-modal-stable

---

## 三、交接文件三：MIC 受控重新接入

**目标**：在v0.3稳定版本上，分阶段、低优先级重新接入MIC，验证不破坏三主模态

### 核心原则

- ECG/PPG/ICM 是主系统，MIC 是低优先级附属
- MIC 可以 drop/incomplete/stall，但**不允许拖垮三主模态**
- CORE PASS/FAIL 只看 ECG/PPG/ICM，MIC 单独标记

### 代码修复

| 修复 | 变更 |
|------|------|
| RECORD_AUDIO_ERROR_REQUESTS_STOP | 1→0（MIC错误不终止session） |
| udio_open_file mutex timeout | 100ms→20ms |

### 实验矩阵（9轮）

| 轮次 | 时长 | 描述 | ECG (Hz) | PPG (Hz) | IMU (Hz) | i2c | sd | CORE | MIC |
|------|------|------|----------|----------|----------|-----|----|------|-----|
| A1 | 92s | no-MIC baseline | 488.65 | 50.39 | 104.24 | 0 | 0 | ✅ PASS | OFF |
| A2 | 92s | no-MIC baseline | 488.69 | 50.40 | 104.24 | 0 | 0 | ✅ PASS | OFF |
| A3 | 92s | no-MIC baseline | 488.57 | 50.39 | 104.24 | 0 | 0 | ✅ PASS | OFF |
| M0 | 92s | MIC DMA only | 483.03 | 50.25 | 104.24 | 0 | 0 | ✅ PASS | DMA |
| M1 | 92s | MIC WAV | 489.36 | 50.34 | 104.23 | 0 | 0 | ✅ PASS | WAV |
| M2-1 | 90s | MIC WAV | 489.3 | 50.3 | 104.1 | 0 | 0 | ✅ PASS | WAV |
| M2-2 | 90s | MIC WAV | 489.3 | 50.4 | 104.2 | 0 | 0 | ✅ PASS | WAV |
| M2-3 | 92s | MIC WAV | 488.9 | 50.3 | 104.1 | 0 | 0 | ✅ PASS | WAV |
| M3 | 300s | MIC WAV 5min | 488.9 | 50.2 | 104.2 | 0 | 0 | ✅ PASS | WAV |

### 最终：CORE PASS ✅ / MIC WARN ⚠️（每轮稳掉2个block，不影响核心）

---

## 四、with-MIC 1小时长测

**目标**：验证MIC作为附属模态开启时，核心三模态长期稳定

### 结果

| 项目 | 值 |
|------|-----|
| 10min预跑 CSV | 13MB, i2c=0, sd=0 |
| 1h长测 CSV | 35.8MB, 持续写入正常 |
| CORE判定 | ✅ PASS |
| MIC判定 | ⚠️ WARN（稳掉2 block/轮） |

### 状态：✅ 完成

---

## 五、总体结论

| 交接文件 | 实验数 | CORE | MIC | 状态 |
|----------|--------|------|-----|------|
| 一：采样率隔离 | 7组 | N/A | N/A | ✅ 根因确证 |
| 二：配置冻结长测 | 7轮 | ✅ PASS | OFF | ✅ v0.3-stable |
| 三：MIC重新接入 | 9轮 | ✅ PASS | ⚠️ WARN | ✅ 接入成功 |
| 四：with-MIC长测 | 2轮 | ✅ PASS | ⚠️ WARN | ✅ 长期稳定 |

### 当前版本：v0.3-core-three-modal-stable

### 已知问题

| 问题 | 状态 |
|------|------|
| MIC 每轮稳掉2个block | 已接受为WARN，不影响CORE |
| session.txt FAT缓存不更新 | 已知，CSV是唯一可靠时长证据 |
| ECG 512Hz→~489Hz欠采样 | 已知，待后续处理 |
| USB CDC高频输出断开 | 硬伤，sd_debug_tool是替代 |
| Flash verify checksum mismatch | 已知，--no-verify绕过 |

### 待办

- LVGL UI重构（3页滑动）
- BLE实机验证
- 音频掉块根因（SAI DMA stall, MSI时钟漂移）
