@echo off
REM STM32L496 + ST-Link 独立调试器 烧录脚本
REM 使用方法: flash.bat [.elf文件路径]
REM 默认使用 build/L496_0405.elf

set ELF=%~1
if "%ELF%"=="" set ELF=build\L496_0405.elf

echo Flashing %ELF% ...
openocd -f interface/stlink.cfg -f target/stm32l4x.cfg -c "program %ELF% verify reset exit"
if %ERRORLEVEL% equ 0 (
    echo [OK] 烧录成功
) else (
    echo [FAIL] 烧录失败
    exit /b %ERRORLEVEL%
)
