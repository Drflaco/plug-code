# Installe le VST3 compilé dans le dossier commun, là où Live le charge, PUIS
# vérifie que ce qui est installé est bien ce qui vient d'être compilé.
#
# Ce script existe à cause d'un incident réel (17/09) : Live a joué pendant des
# heures un binaire vieux d'un jour, qui chargeait encore un état d'échafaudage.
# Le symptôme — des répétitions après chaque transitoire — a été pris pour un
# défaut du moteur. À appeler à chaque clôture.
#
# Usage : powershell -ExecutionPolicy Bypass -File scripts/install_plugin.ps1 [-Config Release]

param([string]$Config = "Release")

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root "build\Plug_artefacts\$Config\VST3\Plug.vst3"
$dst  = "C:\Program Files\Common Files\VST3\Plug.vst3"
$srcBin = Join-Path $src "Contents\x86_64-win\Plug.vst3"
$dstBin = Join-Path $dst "Contents\x86_64-win\Plug.vst3"

if (-not (Test-Path $srcBin)) { throw "Rien à installer : $srcBin absent. Compiler d'abord (scripts/build.ps1)." }

# Live garde le module chargé : une copie par-dessus échouerait à moitié.
$live = Get-Process -Name "Ableton Live*" -ErrorAction SilentlyContinue
if ($live) { throw "Ableton Live est ouvert ($($live.Count) processus). Le fermer avant d'installer : Windows garde le module VST3 verrouillé." }

$srcHash = (Get-FileHash $srcBin).Hash
$srcTime = (Get-Item $srcBin).LastWriteTime

if (Test-Path $dstBin) {
    $oldHash = (Get-FileHash $dstBin).Hash
    if ($oldHash -eq $srcHash) {
        Write-Host "Déjà à jour : le binaire installé est celui du build ($($srcTime))."
        exit 0
    }
    Write-Host "Installé actuellement : $((Get-Item $dstBin).LastWriteTime) — remplacé."
}

# Le dossier commun VST3 demande l'élévation.
$cmd = "Remove-Item -Recurse -Force '$dst' -ErrorAction SilentlyContinue; Copy-Item -Recurse -Force '$src' '$dst'"
$p = Start-Process powershell -Verb RunAs -ArgumentList "-NoProfile", "-Command", $cmd -PassThru -Wait

if (-not (Test-Path $dstBin)) { throw "Installation échouée : $dstBin absent après copie (code $($p.ExitCode))." }

# La vérification qui donne son sens au script : installé == compilé, au bit près.
$newHash = (Get-FileHash $dstBin).Hash
$newTime = (Get-Item $dstBin).LastWriteTime
if ($newHash -ne $srcHash) {
    throw "INSTALLATION INCOHÉRENTE : le binaire installé diffère du build.`n  build   : $srcHash ($srcTime)`n  installé: $newHash ($newTime)"
}

Write-Host ""
Write-Host "Installé et vérifié :"
Write-Host "  $dstBin"
Write-Host "  empreinte $($srcHash.Substring(0,16))…  compilé le $srcTime"
Write-Host ""
Write-Host "Live relira le plugin à son prochain démarrage. Les instances déjà"
Write-Host "présentes dans un Set gardent leur état : les retirer et les recharger"
Write-Host "pour repartir du binaire neuf."
