# 飞书 CLI 安装与配置

## 一、安装

一行命令：

```bash
npx @larksuite/cli@latest install
```

验证：

```bash
lark-cli --version
# 应输出: lark-cli version x.x.x
```

## 二、申请飞书应用

1. 打开 [飞书开放平台](https://open.feishu.cn)
2. 创建自建应用 → 获取 **App ID** 和 **App Secret**
3. 开通以下权限（权限管理 → 批量开通）：

| 权限 scope | 用途 |
|------------|------|
| `base:app:create` | 创建多维表格 |
| `base:table` | 管理数据表/字段 |
| `base:base` | 读写数据 |
| `base:record` | 读写记录 |
| `base:field` | 管理字段 |
| `base:view` | 管理视图 |

## 三、配置 CLI

```bash
# 初始化配置（交互式）
lark-cli config init --new

# 按提示输入：
#   brand: feishu（国内版）
#   App ID: 你的 App ID
#   App Secret: 你的 App Secret
```

## 四、用户认证

CLI 有两种身份：

| 身份 | 用途 | 命令 |
|------|------|------|
| Bot（应用身份） | 调用 API | 配置后自动可用 |
| User（用户身份） | 在飞书 UI 中编辑表格 | `lark-cli auth login` |

```bash
# 登录用户身份（浏览器弹窗扫码）
lark-cli auth login

# 检查状态
lark-cli auth status
# Bot 和 User 都应该显示 ready
```

## 五、项目表格

本项目飞书多维表格：

```
URL:  https://zcno949fvw6r.feishu.cn/base/MD7tbxn1JasfKFsRidPcKjgNncg
base_token: MD7tbxn1JasfKFsRidPcKjgNncg
```

三个数据表：

| 表名 | table_id | 用途 |
|------|----------|------|
| 硬件 | `tblEoO9jxdRfs9zc` | 原理图 + 实物状态 |
| 软件 | `tblkoStalzndqyqO` | 固件模块状态 |
| 变更记录 | `tblBG9rNbAvC6zUk` | 变更历史 |

## 六、常用 CLI 命令速查

```bash
# 查看表格列表
lark-cli base +table-list --base-token MD7tbxn1JasfKFsRidPcKjgNncg

# 查询记录
lark-cli base +record-list --base-token MD7tbxn1JasfKFsRidPcKjgNncg --table-id tblkoStalzndqyqO

# 更新一条记录（upsert：存在则更新，不存在则创建）
lark-cli base +record-upsert --base-token MD7tbxn1JasfKFsRidPcKjgNncg --table-id tblkoStalzndqyqO --json @data.json

# 批量创建
lark-cli base +record-batch-create --base-token MD7tbxn1JasfKFsRidPcKjgNncg --table-id tblBG9rNbAvC6zUk --json @changes.json
```

## 七、JSON 文件格式

飞书 CLI 的 `--json` 参数支持两种方式：

```bash
# 方式 1：直接传 JSON 字符串
lark-cli base +field-create --json '{"field_name":"模块","type":"text"}'

# 方式 2：引用文件（注意 @ 前缀，文件路径相对于当前目录）
lark-cli base +field-create --json @field.json
```

⚠️ 文件必须使用 **UTF-8 无 BOM** 编码。PowerShell 用户注意不要用 `Out-File -Encoding UTF8`（会加 BOM），用：

```powershell
[System.IO.File]::WriteAllText("path.json", 'json content')
```

### 记录批量创建格式

```json
{
  "fields": ["模块", "状态", "负责人"],
  "rows": [
    ["BLE通信", "未开始", "Codex"],
    ["LVGL显示", "异常", "Codex"]
  ]
}
```
