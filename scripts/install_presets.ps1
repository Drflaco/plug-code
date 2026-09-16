# Installe les presets livrés, dans les deux formats.
#   .plugstate  -> %APPDATA%\LascauxLab\Plug\presets, lu par les boutons du plugin
#   .vstpreset  -> bibliothèque utilisateur de Live, seul endroit où son navigateur les voit
# Usage : powershell -ExecutionPolicy Bypass -File scripts/install_presets.ps1

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$src = Join-Path $root "presets"
$dst = Join-Path $env:APPDATA "LascauxLab\Plug\presets"

if (-not (Test-Path $src)) { throw "Aucun dossier presets dans $root" }
New-Item -ItemType Directory -Force $dst | Out-Null
Copy-Item -Force (Join-Path $src "*.plugstate") $dst
Write-Host "Presets .plugstate installés dans $dst :"
Get-ChildItem $dst -Filter *.plugstate | ForEach-Object { Write-Host "  $($_.BaseName)" }

# La bibliothèque utilisateur de Live est déclarée dans ses préférences. On prend
# la version la plus récente présente sur la machine.
$vst = Get-ChildItem (Join-Path $src "*.vstpreset") -ErrorAction SilentlyContinue
if (-not $vst) { Write-Host ""; Write-Host "Aucun .vstpreset a installer."; exit 0 }

$prefRoot = Join-Path $env:APPDATA "Ableton"
$cfg = Get-ChildItem $prefRoot -Directory -ErrorAction SilentlyContinue |
       Where-Object { $_.Name -like "Live 12*" } |
       Sort-Object Name -Descending |
       ForEach-Object { Join-Path $_.FullName "Preferences\Library.cfg" } |
       Where-Object { Test-Path $_ } |
       Select-Object -First 1

$userLib = $null
if ($cfg) {
    # Library.cfg est binaire : on le lit en latin-1 pour y chercher le chemin en clair.
    $bytes = [System.IO.File]::ReadAllBytes($cfg)
    $joined = [System.Text.Encoding]::GetEncoding('iso-8859-1').GetString($bytes)
    if ($joined -match 'ProjectPath Value="([^"]+)"') { $userLib = $matches[1] -replace '/', '\' }
}

if ($userLib -and (Test-Path $userLib)) {
    $target = Join-Path $userLib "User Library"
    if (-not (Test-Path $target)) { $target = $userLib }
    Copy-Item -Force (Join-Path $src "*.vstpreset") $target
    Write-Host ""
    Write-Host "Presets .vstpreset installés dans $target :"
    Get-ChildItem $target -Filter *.vstpreset | ForEach-Object { Write-Host "  $($_.BaseName)" }
} else {
    Copy-Item -Force (Join-Path $src "*.vstpreset") $dst
    Write-Host ""
    Write-Host "Bibliotheque utilisateur de Live introuvable : .vstpreset deposes dans $dst."
    Write-Host "Les glisser a la main dans la bibliotheque utilisateur pour que Live les liste."
}
