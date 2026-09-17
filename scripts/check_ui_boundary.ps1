# Frontière v1/v2 de l'interface — la règle du §5 rendue EXÉCUTABLE (J4b a).
#
# Exigence pilote du 17/09 : « l'interface aura plusieurs vies ». Une v2 doit
# pouvoir remplacer src/ui/v1/ sans toucher au moteur ni à l'état. Cela ne tient
# que si aucun widget ne connaît le moteur : ce script le vérifie au lieu de
# l'espérer. Une règle qu'on peut oublier doit devenir une règle qui s'exécute
# (REGIME §3). À lancer avant chaque commit qui touche à src/ui/.
#
# Usage : powershell -ExecutionPolicy Bypass -File scripts/check_ui_boundary.ps1
# Code de retour 0 si la frontière tient.

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dir  = Join-Path $root "src\ui\v1"

if (-not (Test-Path $dir)) { throw "Dossier absent : $dir" }

# Les seuls en-têtes de la couche de présentation qu'un widget a le droit de voir.
$allowedLayer = @("../ViewTypes.h", "../Presenter.h", "../Format.h", "../Prefs.h")

$files = Get-ChildItem -Path $dir -Recurse -Include *.h, *.cpp
$bad = @()
$count = 0

foreach ($f in $files) {
    $lineNo = 0
    foreach ($line in (Get-Content -LiteralPath $f.FullName -Encoding UTF8)) {
        $lineNo++
        if ($line -notmatch '^\s*#\s*include\s*([<"])([^>"]+)[>"]') { continue }
        $count++
        $bracket = $Matches[1]
        $inc = $Matches[2]
        $ok = $false

        if ($bracket -eq "<") {
            # JUCE (juce_xxx/juce_xxx.h) ou en-tête standard (<memory>, <cmath>…).
            if ($inc -like "juce_*") { $ok = $true }
            elseif ($inc -notmatch "[/\\]") { $ok = $true }
        } else {
            $normalised = $inc -replace "\\", "/"
            if ($allowedLayer -contains $normalised) { $ok = $true }
            # Un fichier du dossier v1 lui-même (PlugEditor.h, Knob.h, LookAndFeel.h…).
            elseif ($normalised -notmatch "/" -and (Test-Path (Join-Path $dir $normalised))) { $ok = $true }
        }

        if (-not $ok) {
            $close = if ($bracket -eq "<") { ">" } else { '"' }
            $bad += "  $($f.Name):$lineNo  #include $bracket$inc$close"
        }
    }
}

Write-Host "Frontiere v1 : $($files.Count) fichier(s), $count inclusion(s) verifiee(s)."

if ($bad.Count -gt 0) {
    Write-Host ""
    Write-Host "FRONTIERE ROMPUE - un widget de src/ui/v1/ inclut autre chose que"
    Write-Host "ViewTypes.h, Presenter.h, Format.h, Prefs.h, JUCE ou la bibliotheque standard :"
    $bad | ForEach-Object { Write-Host $_ }
    Write-Host ""
    Write-Host "Ce qui manque appartient a la couche de presentation : l'ajouter au"
    Write-Host "Presenter ou aux ViewTypes, jamais au widget."
    exit 1
}

Write-Host "OK : la frontiere tient."
exit 0
