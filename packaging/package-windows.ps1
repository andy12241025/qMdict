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

# Arguments are built as an array of fully-quoted strings and splatted.
# Bare `-DFOO=$Var` arguments are not reliably expanded when the script is
# dot-sourced the way GitHub Actions invokes it, and CMake then receives the
# literal text "$Var" as the build type.
$configureArgs = @(
    "-S", "$root"
    "-B", "$buildDir"
    "-G", "$Generator"
    "-DCMAKE_BUILD_TYPE=$Configuration"
    "-DQMDICT_BUILD_TESTS=ON"
    "-DCMAKE_PREFIX_PATH=$QtPrefix"
)

Write-Host ">> configuring: cmake $($configureArgs -join ' ')"
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "configure failed" }

# --config only means something to multi-config generators; passing it to
# Ninja is harmless but passing it to nothing at all is clearer.
$buildArgs = @("--build", "$buildDir")
if ($Generator -like "Visual Studio*") {
    $buildArgs += @("--config", "$Configuration")
}

Write-Host ">> building: cmake $($buildArgs -join ' ')"
& cmake @buildArgs
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

# windeployqt is deliberately generous. A dictionary reader never opens a
# socket, a database or a Direct3D device, so whole plugin families and the
# libraries behind them are dead weight.
$before = (Get-ChildItem $stage -Recurse -File | Measure-Object Length -Sum).Sum
foreach ($group in @("networkinformation", "tls", "sqldrivers", "generic", "iconengines")) {
    $path = Join-Path $stage $group
    if (Test-Path $path) { Remove-Item -Recurse -Force $path }
}

# Anything left that nothing imports goes too. Walking the import tables keeps
# this honest: a DLL is only removed when no shipped binary references it.
function Get-Imports($file) {
    (& dumpbin /nologo /dependents $file) |
        Select-String -Pattern '^\s{4}(\S+\.dll)$' |
        ForEach-Object { $_.Matches[0].Groups[1].Value.ToLower() }
}

$roots = @(Join-Path $stage "qMdict.exe")
$roots += Get-ChildItem -Path $stage -Recurse -Filter *.dll |
    Where-Object { $_.DirectoryName -ne $stage } | ForEach-Object { $_.FullName }

$needed = [System.Collections.Generic.HashSet[string]]::new()
$queue = [System.Collections.Generic.Queue[string]]::new()
foreach ($r in $roots) { $queue.Enqueue($r) }

while ($queue.Count -gt 0) {
    $current = $queue.Dequeue()
    foreach ($import in Get-Imports $current) {
        if ($needed.Add($import)) {
            $candidate = Join-Path $stage $import
            if (Test-Path $candidate) { $queue.Enqueue($candidate) }
        }
    }
}

foreach ($dll in Get-ChildItem -Path $stage -Filter *.dll -File) {
    if (-not $needed.Contains($dll.Name.ToLower())) {
        Write-Host "   dropping unused $($dll.Name)"
        Remove-Item -Force $dll.FullName
    }
}

# Nothing may be missing after that pruning.
$missing = @()
foreach ($binary in @(Join-Path $stage "qMdict.exe") +
                    (Get-ChildItem -Path $stage -Recurse -Filter *.dll | ForEach-Object { $_.FullName })) {
    foreach ($import in Get-Imports $binary) {
        if (Test-Path (Join-Path $QtPrefix "bin\$import")) {
            if (-not (Test-Path (Join-Path $stage $import))) { $missing += $import }
        }
    }
}
if ($missing.Count -gt 0) { throw "pruning removed still-needed libraries: $($missing -join ', ')" }

$after = (Get-ChildItem $stage -Recurse -File | Measure-Object Length -Sum).Sum
Write-Host (">> trimmed {0:N1} MB to {1:N1} MB" -f ($before / 1MB), ($after / 1MB))

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
