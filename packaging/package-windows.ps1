# Builds qMdict and bundles it with the Qt runtime into a zip that runs on a
# clean Windows machine with nothing installed.
#
# Usage: packaging\package-windows.ps1 -QtPrefix C:\Qt\6.8.3\msvc2022_64

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$QtPrefix,
    [string]$Generator = "Ninja",
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "build-package"

$version = (Select-String -Path (Join-Path $root "CMakeLists.txt") -Pattern '^\s+VERSION\s+([0-9.]+)$' |
    Select-Object -First 1).Matches[0].Groups[1].Value
$name = "qMdict-$version-windows-x86_64"
$stage = Join-Path $root "dist\$name"

Write-Host ">> configuring"
cmake -S $root -B $buildDir -G $Generator `
    -DCMAKE_BUILD_TYPE=$Configuration `
    -DQMDICT_BUILD_TESTS=ON `
    -DCMAKE_PREFIX_PATH="$QtPrefix"
if ($LASTEXITCODE -ne 0) { throw "configure failed" }

Write-Host ">> building"
cmake --build $buildDir --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "build failed" }

Write-Host ">> testing"
$tests = Get-ChildItem -Path $buildDir -Filter "qmdict_tests.exe" -Recurse | Select-Object -First 1
& $tests.FullName
if ($LASTEXITCODE -ne 0) { throw "tests failed" }

Write-Host ">> staging into $stage"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path $stage, (Join-Path $stage "data") | Out-Null

$exe = Get-ChildItem -Path $buildDir -Filter "qMdict.exe" -Recurse | Select-Object -First 1
Copy-Item $exe.FullName $stage

# windeployqt copies the Qt DLLs and plugins the binary actually references.
& (Join-Path $QtPrefix "bin\windeployqt.exe") `
    --release --no-translations --no-system-d3d-compiler --no-opengl-sw `
    --no-compiler-runtime --dir $stage (Join-Path $stage "qMdict.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

# On Windows the executable sits at the top level, so data\ is beside it and
# --portable resolves there without a launcher.
@"
qMdict $version - offline MDict (.mdx/.mdd) dictionary reader

Run qMdict.exe, then choose File > Open Dictionary Folder and pick the folder
holding your dictionaries. Sub-folders are scanned recursively.

Start it via qMdict-portable.cmd to keep settings and the headword index cache
in the data\ folder here instead of your Windows user profile.
"@ | Set-Content -Path (Join-Path $stage "README.txt") -Encoding UTF8

@"
@echo off
start "" "%~dp0qMdict.exe" --portable %*
"@ | Set-Content -Path (Join-Path $stage "qMdict-portable.cmd") -Encoding ASCII

Write-Host ">> zipping"
$zip = Join-Path $root "dist\$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal

Write-Host ">> done: dist\$name.zip"
