# Construction reproductible — MSVC 2022 x64, Release.
# Usage : powershell -ExecutionPolicy Bypass -File scripts/build.ps1 [-Config Release|Debug]
param([string]$Config = "Release")

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

$cmake = "C:\Program Files\CMake\bin\cmake.exe"
if (-not (Test-Path $cmake)) {
    $cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
}
if (-not (Test-Path $cmake)) { throw "CMake introuvable." }

& $cmake -S $root -B "$root\build" -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { throw "Configuration CMake échouée." }

& $cmake --build "$root\build" --config $Config --parallel
if ($LASTEXITCODE -ne 0) { throw "Compilation échouée." }

Write-Host ""
Write-Host "VST3  : $root\build\Plug_artefacts\$Config\VST3\Plug.vst3"
Write-Host "Bench : $root\build\PlugBench_artefacts\$Config\PlugBench.exe"
