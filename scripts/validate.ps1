# Validation pluginval — niveau de sévérité 5 (compatibilité hôte).
# Usage : powershell -ExecutionPolicy Bypass -File scripts/validate.ps1 [-Strictness 5] [-Config Release]
param([int]$Strictness = 5, [string]$Config = "Release")

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$pluginval = "$root\tools\pluginval\pluginval.exe"
$vst3 = "$root\build\Plug_artefacts\$Config\VST3\Plug.vst3"
$out = "$root\measure\raw"

if (-not (Test-Path $pluginval)) { throw "pluginval absent : $pluginval (release v1.0.4, pluginval_Windows.zip)" }
if (-not (Test-Path $vst3)) { throw "VST3 absent : $vst3 (lancer scripts/build.ps1)" }
New-Item -ItemType Directory -Force $out | Out-Null

& $pluginval --strictness-level $Strictness --validate-in-process --output-dir $out --validate $vst3
$code = $LASTEXITCODE
Write-Host ""
Write-Host "pluginval niveau $Strictness : code de retour $code (0 = sans erreur)"
exit $code
