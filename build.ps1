# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 OpenOLEDShifter contributors

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
dotnet publish .\OpenOLEDShifter.csproj -c Release -o .\release --self-contained false --nologo
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
Copy-Item -LiteralPath .\README.md -Destination .\release\README.md -Force
Copy-Item -LiteralPath .\LICENSE -Destination .\release\LICENSE -Force
Write-Host 'Ready: release\OpenOLEDShifter.exe'
