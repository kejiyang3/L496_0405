# 飞书 Skill 包 — L496 项目管理 + 飞书集成

> 给另一台电脑的 Agent 用的便携安装包。

## 包含内容

### 项目管理
| Skill | 用途 |
|-------|------|
| `l496-pm` | L496 项目管理 (codex2)：INBOX分拣、变更卡、飞书同步、闭环验证 |

### 飞书集成 (26 个)
| Skill | 用途 |
|-------|------|
| `lark-base` | 多维表格：建表、字段管理、记录读写 |
| `lark-shared` | 飞书 CLI 安装、认证、身份切换 |
| `lark-doc` | 飞书文档 |
| `lark-sheets` | 电子表格 |
| `lark-drive` | 云空间（云盘） |
| `lark-im` | 即时通讯 |
| `lark-calendar` | 日历/日程/会议室 |
| `lark-task` | 任务管理 |
| `lark-approval` | 审批 |
| `lark-contact` | 通讯录 |
| `lark-mail` | 邮箱 |
| `lark-minutes` | 妙记（音视频纪要） |
| `lark-vc` | 视频会议 |
| `lark-vc-agent` | 视频会议机器人 |
| `lark-okr` | OKR 管理 |
| `lark-wiki` | 知识库 |
| `lark-slides` | 幻灯片 |
| `lark-markdown` | Markdown 文件 |
| `lark-whiteboard` | 画板 |
| `lark-event` | 实时事件订阅 |
| `lark-attendance` | 考勤打卡 |
| `lark-apps` | 妙搭（HTML部署） |
| `lark-openapi-explorer` | 原生 OpenAPI 探索 |
| `lark-skill-maker` | 自定义 Skill 创建 |
| `lark-workflow-meeting-summary` | 会议纪要整理工作流 |
| `lark-workflow-standup-report` | 日程待办摘要 |

## 安装

### 方式 1：直接复制

```powershell
# 将所有 lark-* 和 l496-pm 文件夹复制到 skills 目录
Copy-Item -Recurse feishu-skills\lark-* C:\Users\$env:USERNAME\.agents\skills\
Copy-Item -Recurse feishu-skills\l496-pm C:\Users\$env:USERNAME\.codex\skills\
```

### 方式 2：使用 skill-installer

如果那台电脑已有 skill-installer，可以用它安装。

## 安装后配置

```bash
# 1. 安装飞书 CLI
npx @larksuite/cli@latest install

# 2. 配置飞书应用
lark-cli config init --new
# 按提示输入 App ID 和 App Secret

# 3. 用户认证
lark-cli auth login
```

## 项目表格

```
base_token: MD7tbxn1JasfKFsRidPcKjgNncg
URL: https://zcno949fvw6r.feishu.cn/base/MD7tbxn1JasfKFsRidPcKjgNncg
```

## 使用

安装后，Agent 会自动识别这些 skill。核心触发方式：

- `$l496-pm` — 激活项目管理模式 (codex2)
- 提到"飞书表格"、"多维表格"、"同步"等 — 自动触发 lark-base
- 提到"飞书文档" — 自动触发 lark-doc

建议先从 `agent-onboarding/` 文件夹阅读项目上下文，再开始工作。