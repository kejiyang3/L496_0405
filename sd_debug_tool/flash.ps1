$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# Change to script directory so relative paths work (fixes Chinese path issues with OpenOCD)
Push-Location $scriptDir

try {
    Write-Host "=== Flash SD Debug Firmware ===" -ForegroundColor Cyan
    openocd -f interface/stlink.cfg -f target/stm32l4x.cfg -c "program sd_debug.hex verify reset exit"

    if ($LASTEXITCODE -ne 0) {
        Write-Host "Flash failed! Check ST-Link connection." -ForegroundColor Red
        exit 1
    }

    Write-Host "=== Flash Complete ===" -ForegroundColor Green
    Write-Host ""
    Write-Host "Now pull SD card files:" -ForegroundColor Yellow
    Write-Host "  python pc\usb_sd_pull.py list"
    Write-Host "  python pc\usb_sd_pull.py get FILENAME -o OUTPUT"
    Write-Host "  python pc\usb_sd_pull.py get-all -o sd_pull"
} finally {
    Pop-Location
}