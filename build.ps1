# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 OpenOLEDShifter contributors

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
dotnet publish .\OpenOLEDShifter.csproj -c Release -r win-x64 -o .\release\windows-x64 --self-contained true `
    -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true `
    -p:DebugType=None -p:DebugSymbols=false --nologo
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
# Some native NuGet packages publish their own debug symbols.
Get-ChildItem -LiteralPath .\release\windows-x64 -File -Filter *.pdb |
    ForEach-Object { Remove-Item -LiteralPath $_.FullName }
Copy-Item -LiteralPath .\README.md -Destination .\release\windows-x64\README.md -Force
Copy-Item -LiteralPath .\LICENSE -Destination .\release\windows-x64\LICENSE -Force
Write-Host 'Ready: release\windows-x64\OpenOLEDShifter.exe'
