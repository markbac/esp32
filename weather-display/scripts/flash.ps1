# Build and flash the board, handling the ESP32-S3 USB re-enumeration quirk.
#
# Uploading via the app's USB-serial port (Adafruit VID 239A) makes the board
# reset into its ROM bootloader, which appears as a *different* COM port
# (Espressif VID 303A). esptool then fails on the old port, so we retry once
# on whichever bootloader port appears.
#
# Usage:  powershell -File scripts\flash.ps1

$ErrorActionPreference = 'Continue'
Set-Location (Split-Path $PSScriptRoot -Parent)

function Get-BoardPort([string]$vid) {
    $dev = Get-CimInstance Win32_PnPEntity |
        Where-Object { $_.DeviceID -match "VID_$vid" -and $_.Name -match '\(COM\d+\)' } |
        Select-Object -First 1
    if ($dev) { return ($dev.Name -replace '.*\((COM\d+)\).*', '$1') }
    return $null
}

$port = Get-BoardPort '303A'
if (-not $port) { $port = Get-BoardPort '239A' }
if (-not $port) { Write-Error 'Board not found on USB'; exit 1 }

Write-Host "Uploading via $port"
python -m platformio run -t upload --upload-port $port
if ($LASTEXITCODE -eq 0) { exit 0 }

Write-Host 'Upload failed - waiting for the bootloader port...'
for ($i = 0; $i -lt 20; $i++) {
    Start-Sleep -Milliseconds 500
    $boot = Get-BoardPort '303A'
    if ($boot) { break }
}
if (-not $boot) { Write-Error 'Bootloader port did not appear'; exit 1 }
Write-Host "Retrying via bootloader port $boot"
python -m platformio run -t upload --upload-port $boot
exit $LASTEXITCODE
