# 交接摘要 — L496_0405 项目

> 给下一个接手 Agent 的快速上下文。看完 2 分钟即可上手。

---

## 我是谁

L496_0405 是一块**可穿戴多传感器健康监测手表**，跑 STM32L496 + FreeRTOS + LVGL，项目由一名学生独立开发（硬件 + 软件 + 外壳）。

---

## 当前状态快照

| 维度 | 状态 |
|------|------|
| 编译 | ✅ 通过 |
| 烧录 | ✅ ST-Link SWD 正常 |
| SD 卡 | ✅ 56 个文件，读写验证通过 |
| ECG 采集 | ✅ 正常 (512 SPS) |
| PPG 采集 | ✅ 正常 (200 SPS) |
| IMU 采集 | ✅ 正常 (51 Hz) |
| 多传感器记录 | ✅ 正常 (CSV) |
| LVGL 显示 | ⚠️ 背光亮但无内容（栈已修复为 8KB，待 ST-Link 重连后验证） |
| BLE 通信 | ❌ 未实现（最高优先级） |
| 外壳 | 🔶 待设计 |

---

## 我的成果（上次会话）

1. ✅ 闭环开发流程跑通（build-flash-monitor 一条命令）
2. ✅ SD 卡硬件验证通过
3. ✅ LCD 显示问题诊断（根因：Task_LVGL 栈 8KB→4KB + app_lvgl.c 不兼容）
4. ✅ 项目管理结构建立（project.md / INBOX.md / hardware/ / enclosure/ / changes/）
5. ✅ 飞书 CLI 安装 + 多维表格创建（3 张表已填充初始数据）
6. ✅ agent-onboarding/ 文件夹创建（5 篇文档，新 Agent 入职指南）

---

## 待办（优先级排序）

| 优先级 | 任务 | 阻塞 |
|--------|------|------|
| 🔴 P0 | ST-Link 物理重连 → 烧录 LCD 修复固件 → 验证 | 硬件操作 |
| 🔴 P0 | BLE 命令协议实现（SYNC/FNAME/PING） | 无 |
| 🟡 P1 | LCD 新 UI 渐进引入（3 页滑屏 → .claude/LVGL_UI_PLAN.md） | P0 LCD 修复后 |
| 🟡 P1 | Flash verify checksum mismatch 根因排查 | 无 |
| 🟢 P2 | DMP 文件清理 (~14KB flash) | 确认不需要后 |
| 🟢 P2 | 外壳设计启动 | 用户提供需求 |

---

## 关键文件速查

| 想看什么 | 去哪里 |
|----------|--------|
| 最详细的架构说明 | `CLAUDE.md` |
| 项目总表 | `project.md` |
| 代码约定 | `agent-onboarding/CONVENTIONS.md` |
| 技术上下文 | `agent-onboarding/PROJECT_CONTEXT.md` |
| 日常工作流 | `agent-onboarding/WORKFLOW.md` |
| 飞书配置 | `agent-onboarding/FEISHU_SETUP.md` |
| 表结构 | `agent-onboarding/TABLE_SCHEMA.md` |
| 飞书多维表格 | https://zcno949fvw6r.feishu.cn/base/MD7tbxn1JasfKFsRidPcKjgNncg |