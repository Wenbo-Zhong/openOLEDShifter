# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 OpenOLEDShifter contributors
param(
    [string]$WxSource,
    [string]$BuildDirectory = 'artifacts/native-windows',
    [string]$OutputDirectory = 'release/windows-x64'
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
if ($WxSource) {
    $arguments = @('-S', 'NativeApp', '-B', $BuildDirectory, '-DOPENOLED_BUILD_UI=ON',
        '-DCMAKE_BUILD_TYPE=Release', "-DOPENOLED_WX_SOURCE=$((Resolve-Path -LiteralPath $WxSource).Path)")
    if (!(Test-Path (Join-Path $BuildDirectory 'CMakeCache.txt'))) { $arguments += @('-A', 'x64') }
    cmake @arguments
    if ($LASTEXITCODE) { throw 'CMake configuration failed' }
} elseif (!(Test-Path (Join-Path $BuildDirectory 'CMakeCache.txt'))) {
    throw 'Pass -WxSource with the path to a wxWidgets source checkout including its submodules.'
}
cmake --build $BuildDirectory --config Release --parallel
if ($LASTEXITCODE) { throw 'Native build failed' }
ctest --test-dir $BuildDirectory -C Release --output-on-failure
if ($LASTEXITCODE) { throw 'Native tests failed' }

$exe = Join-Path $BuildDirectory 'Release/OpenOLEDShifter.exe'
if (!(Test-Path $exe)) { $exe = Join-Path $BuildDirectory 'OpenOLEDShifter.exe' }
$wx = Select-String -LiteralPath (Join-Path $BuildDirectory 'CMakeCache.txt') -Pattern '^OPENOLED_WX_SOURCE:PATH=(.+)$'
if (!$wx) { throw 'Configure the build with -WxSource to include the wxWidgets license.' }
$destination = New-Item -ItemType Directory -Path $OutputDirectory -Force
Copy-Item -LiteralPath $exe,LICENSE,README.md -Destination $destination.FullName
Copy-Item -LiteralPath (Join-Path $wx.Matches[0].Groups[1].Value 'docs/licence.txt') -Destination (Join-Path $destination.FullName 'wxWidgets-LICENSE.txt')
Write-Host "Ready: $(Join-Path $destination.FullName 'OpenOLEDShifter.exe')"
