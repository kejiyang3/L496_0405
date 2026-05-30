# 代码约定与规范

> 本项目的一切代码约定。遵守这些约定比写代码本身更重要。

---

## 一、注释语言

**所有代码注释必须用中文。** 没有例外。

```c
// ✅ 正确
void StartTask_BLE(void *argument) {
    /* 初始化 UART1 DMA 接收 */
}

// ❌ 错误
void StartTask_BLE(void *argument) {
    /* Initialize UART1 DMA receive */
}
```

回答用户时也尽量用中文。

---

## 二、Git 策略

### 分支命名

所有工作分支必须以 `codex/` 为前缀：

```bash
git checkout -b codex/简短描述
```

示例：
- `codex/ble-cmd-parser`
- `codex/fix-lcd-backlight`
- `codex/refactor-sd-logger`

### Commit 类型

| 前缀 | 含义 | 示例 |
|------|------|------|
| `fix:` | 修复 bug | `fix: LCD 背光亮但无显示` |
| `feat:` | 新功能 | `feat: BLE 命令解析 SYNC/PING` |
| `refactor:` | 重构 | `refactor: SD 日志双缓冲` |
| `docs:` | 文档 | `docs: 更新 AGENTS.md 配置` |
| `chore:` | 杂项 | `chore: 清理调试输出` |

### 提交粒度

**一个 commit 只做一件事。** 别把 10 个改动塞进一个 commit。

```bash
# ✅ 好的节奏
git commit -m "fix: 增大 LVGL 任务栈到 8KB"
git commit -m "feat: LVGL 新增诊断页面"

# ❌ 不要这样
git commit -m "各种改动"
```

### 合并回 master

闭环验收通过后合并：

```bash
git checkout master
git merge codex/你的分支
```

---

## 三、代码风格

### 语言标准

C11 (`-std=c11`)，使用 CMSIS-RTOS v2 API。

### CubeMX 生成代码

CubeMX 生成的代码中 `USER CODE BEGIN` / `USER CODE END` 之间的内容：
- **不要移动这些标记**
- **不要在标记之间删除 HAL 生成的初始化代码**
- 只在标记区域内添加自己的代码

```c
/* USER CODE BEGIN 0 */
// ✅ 在这里写自己的代码
/* USER CODE END 0 */

HAL_Init(); // ← CubeMX 生成的，别动
```

### 变量命名

- 不用单字母变量名（循环变量 `i`、`j` 除外）
- 不用拼音
- 任务函数前缀：`Task_`（如 `Task_Sensor`、`Task_LVGL`）
- ISR 回调前缀：`HAL_` 系统回调保持原名

---

## 四、禁止事项

以下事情**绝对不能做**：

| 禁止 | 原因 |
|------|------|
| `f_mount(NULL, ...)` 在 SD 写入路径中 | 多个写入器共享同一卷，重复挂载会破坏文件系统 |
| 在 ISR 中操作 I2C/SPI/SD | ISR 只做：计数器++ + 通知任务 |
| 添加版权/许可证头 | 除非用户明确要求 |
| 修复 HAL/ThirdParty/FatFs 的编译警告 | 这是上游代码，`-Wunused-parameter` 等警告是预期的 |
| 修改 CubeMX 生成的非 USER CODE 区域 | CubeMX 重新生成时会覆盖 |
| 在中断里长时间阻塞 | 中断服务函数应 < 10μs |

---

## 五、必须遵守

| 规则 | 说明 |
|------|------|
| 改动前写变更卡片 | `changes/YYYY-MM-DD-简短描述.md` |
| 改完做闭环验证 | `build-flash-monitor` 一条命令跑通 |
| 验证通过同步飞书 | 更新软件表和变更记录表 |
| INBOX 分拣 | 每次开始工作前读 INBOX |
| 只改相关的代码 | 不顺手修无关 bug |
| 不加不必要的复杂度 | 最小改动原则 |

---

## 六、飞书 CLI 注意事项

### JSON 文件编码

```powershell
# ✅ 正确：UTF-8 无 BOM
[System.IO.File]::WriteAllText("data.json", '{"key":"value"}')

# ❌ 错误：会加 BOM，飞书 API 无法解析
Out-File -Encoding UTF8 data.json
```

### 相对路径

`--json @file.json` 中的路径必须是**相对于当前工作目录**的相对路径，不能是绝对路径。

---

## 七、构建与烧录

```bash
# 编译
ninja -C build

# 闭环（编译 + 烧录 + 串口监控）
python skills\embed-ai-tool\workflow\scripts\workflow_runner.py --run build-flash-monitor --build-system cmake --project . --flash-target target/stm32l4x.cfg --port COM7
```

编译通过标准：
- 0 错误
- HAL/ThirdParty/FatFs 的 `-Wunused-parameter` 警告忽略
- 其他警告需关注

烧录注意事项：
- 调试器：ST-Link V2 (SWD)，API v2
- 烧录时加 `--no-verify` 可避免 Flash verify checksum 不匹配问题
- 烧录后需等待 2 秒再重启/监控

---

## 八、文档维护

每次改动后，Codex 自动更新以下文件：

| 文件 | 更新时机 |
|------|---------|
| `project.md` | 任何模块状态变更 |
| `hardware/schematic.md` | 你提供硬件信息后 |
| `hardware/physical.md` | 你提供实物变更后 |
| `enclosure.md` | 你提供外壳信息后 |
| `changes/` | 每次改动前创建卡片，完成后更新结果 |
| `INBOX.md` | 定期分拣 |
| 飞书多维表格 | 每次改动验证通过后同步 |