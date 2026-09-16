# Installe les presets d'état livrés dans la bibliothèque de l'utilisateur, là où
# le plugin les cherche en premier (%APPDATA%\LascauxLab\Plug\presets).
# Usage : powershell -ExecutionPolicy Bypass -File scripts/install_presets.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$src = Join-Path $root "presets"
$dst = Join-Path $env:APPDATA "LascauxLab\Plug\presets"

if (-not (Test-Path $src)) { throw "Aucun dossier presets dans $root" }
New-Item -ItemType Directory -Force $dst | Out-Null
Copy-Item -Force (Join-Path $src "*.plugstate") $dst
Write-Host "Presets installés dans $dst :"
Get-ChildItem $dst -Filter *.plugstate | ForEach-Object { Write-Host "  $($_.BaseName)" }
