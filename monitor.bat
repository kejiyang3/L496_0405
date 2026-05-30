@echo off
REM STM32 CDC Monitor 启动脚本
REM 自动在 logs\ 下生成带时间戳的日志文件

set LOG_DIR=logs
if not exist %LOG_DIR% mkdir %LOG_DIR%

REM 生成时间戳: YYYY-MM-DD_HH-MM-SS
for /f %%i in ('wmic os get localtime ^| findstr ^[0-9]') do set DT=%%i
set TS=%DT:~0,4%-%DT:~4,2%-%DT:~6,2%_%DT:~8,2%-%DT:~10,2%-%DT:~12,2%
set LOG_FILE=%LOG_DIR%\cdc_%TS%.log

echo 日志文件: %LOG_FILE%
python python\stm32_cdc_monitor.py -l %LOG_FILE% %*
