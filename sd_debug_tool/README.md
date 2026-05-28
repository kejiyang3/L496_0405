# SD 卡调试工具 v2

需要读取板子上的 SD 卡时，烧录此固件，用 PC 脚本拉取文件，然后烧回主固件。

## 新特性 (v2)

- **ACK + CRC16 重传** — 每行数据带 CRC 校验，丢失自动重发
- **断点续传** — 中断后自动从断点继续，最多重试 3 次
- **进度条** — 大文件传输实时显示进度
- **批量下载** — `get-all` 一键拉取全部文件

## 快速使用

### 1. 安装依赖（首次）

```powershell
pip install pyserial
```

### 2. 烧录

```powershell
.\flash.ps1
```

### 3. 拉取文件

```powershell
# 列出文件
python pc\usb_sd_pull.py list

# 下载单个文件
python pc\usb_sd_pull.py get 文件名 -o 输出路径

# 批量下载全部文件
python pc\usb_sd_pull.py get-all -o sd_pull

# 查看文本文件
python pc\usb_sd_pull.py cat log.txt

# 手动断点续传
python pc\usb_sd_pull.py get 文件名 --resume 1024
```

> 不指定 `--port` 时自动查找 USB CDC 串口。

### 4. 烧回主固件

使用你主工程自己的烧录方式即可。

## 文件说明

| 文件 | 用途 |
|------|------|
| `sd_debug.hex` | 预编译固件 (STM32L496, ~247KB) |
| `flash.ps1` | 一键烧录脚本 (支持中文路径) |
| `pc/usb_sd_pull.py` | PC 端文件拉取工具 v2 |

## 协议说明

GET 传输使用问答式协议：

```
固件: [HEX 00000000 <hex_data> <crc16>]
 PC : ACK / NAK
固件: (重发或继续)
 ...
固件: [GET_END filename size]
 PC : ACK
```

- CRC: CRC-16/XMODEM
- 固件行超时: 500ms
- 最大重试: 3 次

## 芯片要求

- STM32L496 (其他 STM32L4 需重新编译)
- ST-Link 烧录器
- USB CDC 接口连接 PC