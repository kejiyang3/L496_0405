# SD 卡调试工具 v2

需要读取板子上的 SD 卡时，烧录此固件，用 PC 脚本操作，然后烧回主固件。

## 快速使用

### 1. 安装依赖（首次）

```powershell
pip install pyserial
```

### 2. 烧录

```powershell
.\flash.ps1
```

### 3. 操作 SD 卡

```powershell
# 列出文件
python pc\usb_sd_pull.py list

# 下载单个文件
python pc\usb_sd_pull.py get 文件名 -o 输出路径

# 批量下载全部文件
python pc\usb_sd_pull.py get-all -o sd_pull

# 查看文本文件
python pc\usb_sd_pull.py cat log.txt

# 清空 SD 卡所有文件
python pc\usb_sd_pull.py clear

# 手动断点续传
python pc\usb_sd_pull.py get 文件名 --resume 1024
```

> 不指定 `--port` 时自动查找 USB CDC 串口。

### 4. 烧回主固件

使用你主工程自己的烧录方式。

## 特性

- **ACK + CRC16 重传** — 每行带 CRC 校验，丢失自动重发
- **断点续传** — 中断后从断点继续
- **进度条** — 大文件实时显示进度
- **批量下载** — `get-all` 一键全拉
- **清空 SD** — `clear` 删除根目录所有文件

## 文件说明

| 文件 | 用途 |
|------|------|
| `sd_debug.hex` | 预编译固件 (~250KB) |
| `flash.ps1` | 一键烧录 |
| `pc/usb_sd_pull.py` | PC 端工具 |

## 芯片要求

- STM32L496
- ST-Link
- USB CDC