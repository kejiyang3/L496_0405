# 日常工作流

> 每一步都有记录，每一步都可追溯。

---

## 概览

```
INBOX 收集 → 变更卡片 → Git 分支 → 编码 → 闭环验收 → 飞书同步
(人类写)    (Agent写)   (Agent做)  (Agent做) (Agent做)  (Agent做)
```

---

## 第一步：检查 INBOX

项目根目录有 `INBOX.md`，是人类随手写的想法/问题/需求。

```markdown
# INBOX.md（示例）
- 屏幕排线好像接触不良
- BLE 协议要不要加个电量上报？
```

**Agent 的职责**：
1. 每次开始工作前先读 `INBOX.md`
2. 将条目分拣到对应位置：
   - 硬件问题 → 飞书"硬件"表新增记录
   - 软件问题 → 飞书"软件"表更新状态
   - 需要动手改的 → 创建变更卡片
   - 已处理的 → 移到 INBOX 底部"整理记录"

---

## 第二步：写变更卡片

在 `changes/` 目录创建 `YYYY-MM-DD-简短描述.md`：

```markdown
# 变更：[一句话描述]

## 原因
[为什么改？根因是什么？]

## 改动范围
- [ ] `文件路径` — 改动说明
- [ ] `文件路径` — 改动说明

## 影响评估
- 硬件：[是/否]
- 固件：[是/否]
- 外壳：[是/否]

## 验证方法
- [ ] 编译通过
- [ ] 烧录正常
- [ ] 功能验证：[具体描述]

## 回滚方案
[怎么回到改之前的状态]

## 结果
- [ ] 成功 / 失败 / 进行中
```

模板文件：`changes/_TEMPLATE.md`

---

## 第三步：开 Git 分支 + 编码

```bash
# 从 master 开分支
git checkout -b codex/简短描述

# 小步提交，每改一组相关文件提交一次
git add 改动的文件
git commit -m "类型: 简短说明"

# commit 类型约定
# fix:  修复 bug
# feat: 新功能
# refactor: 重构
# docs: 文档
# chore: 杂项
```

**原则**：一个 commit 只做一件事。别攒 10 个改动一起提交。

---

## 第四步：闭环验收

```bash
# 编译
ninja -C build

# 编译 + 烧录 + 串口监控（一条命令）
python skills\embed-ai-tool\workflow\scripts\workflow_runner.py --run build-flash-monitor --build-system cmake --project . --flash-target target/stm32l4x.cfg --port COM7
```

验收通过标准：
- 编译零错误（HAL/FatFS 的 `-Wunused-parameter` 警告忽略）
- 烧录成功
- 串口输出符合预期

---

## 第五步：同步飞书

### 更新软件表

```bash
# 用 upsert 更新一条记录的状态
lark-cli base +record-upsert --base-token MD7tbxn1JasfKFsRidPcKjgNncg --table-id tblkoStalzndqyqO --json @update.json
```

upsert JSON 格式（根据"模块"字段匹配）：

```json
{
  "fields": ["模块", "状态", "说明"],
  "rows": [["BLE通信", "正常", "命令解析完成"]]
}
```

### 新增变更记录

```bash
lark-cli base +record-batch-create --base-token MD7tbxn1JasfKFsRidPcKjgNncg --table-id tblBG9rNbAvC6zUk --json @change.json
```

```json
{
  "fields": ["日期", "类型", "摘要", "结果"],
  "rows": [["2026-05-30", "软件", "闭环开发流程跑通", "成功"]]
}
```

---

## 第六步：更新变更卡片结果

把卡片里的 `## 结果` 从 `- [ ] 进行中` 改为 `- [x] 成功` 或 `- [x] 失败`。

---

## 日常节奏

| 频率 | 动作 |
|------|------|
| 每次开始工作 | 读 INBOX，分拣 |
| 每次动手改代码前 | 写变更卡片，开分支 |
| 每改完一组文件 | git commit |
| 改完验证通过后 | 闭环验收，飞书同步 |
| 人类提供新信息时 | 更新飞书对应表格 |

---

## 一个完整示例

```
1. 读 INBOX → 发现"BLE协议需要实现"
2. 写变更卡片 → changes/2026-05-30-BLE命令解析.md
3. git checkout -b codex/ble-cmd-parser
4. 改 freertos.c StartTask_BLE → 加 SYNC/FNAME/PING 解析
5. git commit -m "feat: BLE 命令解析 SYNC/FNAME/PING"
6. ninja -C build → ✅
7. 闭环验收 → 编译✅ 烧录✅ 串口有+PONG输出✅
8. 飞书：软件表 BLE通信 状态→正常，变更记录表 新增一行
9. 变更卡片 结果→成功
10. git checkout master && git merge codex/ble-cmd-parser
```
