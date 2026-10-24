# SPDX-License-Identifier: GPL-3.0-or-later
# Wiicompiled feasibility probe: translate MLB's main.dol + ovl_main.rel with the
# Wiicompiled translator (docs/wiicompiled-feasibility.md). Everything stays on D:
# under local/: the working copy, NuGet/.NET caches, temp files and outputs.
# -Groups lists the runtime/src/hle groups whose native hooks are rebound to MLB (see
# tools/wiicompiled_rebind.py); -Fresh recreates the working copy from the pinned clone.
# -RegenerateMap rebuilds config/wiicompiled/rl8e54-functions.map from the local dtk/DolRecomp analysis
# (its rel-linked inputs came from tools/prepare_linked_rel.py, kept at git tag moderngekko-era).
# -PrepareOnly stops after patching and rebinding: a source tree without the game, for the Setup
# payload (tools/setup/Make-Release.ps1).
param([switch]$Build, [switch]$Fresh, [switch]$RegenerateMap, [switch]$PrepareOnly, [int]$Threads = 6,
      [string[]]$Groups = @('os', 'audio', 'storage', 'vi', 'gx', 'input', 'ios'))
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$work = Join-Path $root 'local\wiicompiled-mlb'
$src = Join-Path $work 'src'
$home_ = Join-Path $root 'local\cache\dotnet-home'
foreach ($dir in "$home_\AppData", "$home_\LocalAppData", "$home_\tmp") {
    New-Item -ItemType Directory -Force $dir | Out-Null
}
# .NET, NuGet and MSBuild default to the user profile on C:.
$env:NUGET_PACKAGES = Join-Path $root 'local\cache\nuget'
$env:DOTNET_CLI_HOME = $home_
$env:APPDATA = "$home_\AppData"
$env:LOCALAPPDATA = "$home_\LocalAppData"
$env:TEMP = "$home_\tmp"
$env:TMP = "$home_\tmp"
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
$env:DOTNET_NOLOGO = '1'

function Invoke-Checked([string]$label) {
    if ($LASTEXITCODE -ne 0) { throw "$label failed with exit code $LASTEXITCODE" }
}

if ($Fresh -and (Test-Path $src)) {
    cmd /c rmdir "$src\generated" 2>$null
    Remove-Item -Recurse -Force $src
}
if (-not (Test-Path "$src\.git")) {
    git clone -q (Join-Path $root 'local\vendor\Wiicompiled') $src; Invoke-Checked 'git clone'
    $pin = (Get-Content -LiteralPath config\tools.lock.json -Raw | ConvertFrom-Json).wiicompiled.commit
    git -C $src checkout -q $pin; Invoke-Checked "checkout $pin"
    foreach ($patch in 'wiicompiled-rel-bss-address', 'wiicompiled-mlb-bringup', 'wiicompiled-masked-jump-tables', 'wiicompiled-aurora-partial-efb-copy',
             'wiicompiled-dvd-low-completion', 'wiicompiled-scripted-kpad', 'wiicompiled-mlb-overlay-loader',
             'wiicompiled-debug-tools', 'wiicompiled-mlb-nand-es',
             'wiicompiled-mlb-sc-status', 'wiicompiled-tlut-reinit-cache', 'wiicompiled-dl-vtxdesc-live-check',
             'wiicompiled-mlb-keyboard-gcpad', 'wiicompiled-mlb-stm-sda',
             'wiicompiled-byte-index-jump-tables', 'wiicompiled-mlb-pad-stick-range',
             'wiicompiled-pad-radial-deadzone', 'wiicompiled-preemption-point',
             'wiicompiled-copy-texture-pool', 'wiicompiled-perf-instrumentation',
             'wiicompiled-launcher', 'wiicompiled-mods', 'wiicompiled-mlb-identity',
             'wiicompiled-mod-saves', 'wiicompiled-roster-editor', 'wiicompiled-launcher-live-settings', 'wiicompiled-roster-flags',
             'wiicompiled-native-efb-snapshot', 'wiicompiled-match-benchmark', 'wiicompiled-resolution-labels',
             'wiicompiled-audio-queue', 'wiicompiled-audio-depop', 'wiicompiled-periodic-sleep',
             'wiicompiled-benchmark-device-info', 'wiicompiled-launcher-controls', 'wiicompiled-roster-voices', 'wiicompiled-pad-script-cstick',
             'wiicompiled-match-benchmark-gameplay', 'wiicompiled-texture-studio', 'wiicompiled-mlb-disc-identity', 'wiicompiled-graphics-options', 'wiicompiled-new-stadiums', 'wiicompiled-language', 'wiicompiled-mlb-fielding-diagonals', 'wiicompiled-mlb-widescreen', 'wiicompiled-mlb-fill-screen', 'wiicompiled-auto-mipmaps', 'wiicompiled-mlb-mem2-heap', 'wiicompiled-timers-when-hidden', 'wiicompiled-launcher-saves', 'wiicompiled-stadium-viewer', 'wiicompiled-season-probe', 'wiicompiled-translator-immediate-overrides', 'wiicompiled-season-cap', 'wiicompiled-mlb-save-dir', 'wiicompiled-season-schedule', 'wiicompiled-season-start-year', 'wiicompiled-translator-store-overrides', 'wiicompiled-season-universal-dh', 'wiicompiled-season-divisions', 'wiicompiled-mlb-life-packs', 'wiicompiled-season-roster-year', 'wiicompiled-season-modern-leagues', 'wiicompiled-season-schedule-download', 'wiicompiled-season-custom-league', 'wiicompiled-season-studio', 'wiicompiled-player-passwords', 'wiicompiled-two-way-player', 'wiicompiled-life-history-year', 'wiicompiled-opengles', 'wiicompiled-d3d11', 'wiicompiled-staging-size', 'wiicompiled-gpu-pass-list') {
        git -C $src apply (Join-Path $root "tools\patches\$patch.patch"); Invoke-Checked "patch $patch"
    }
    # The runtime CMake reads translator output from <src>/generated.
    cmd /c mklink /J "$src\generated" "$work\generated" | Out-Null
    $Build = $true
}
$translator = "$src\translator\src\Translator.Cli\bin\Release\net8.0\Translator.Cli.dll"
if (-not $PrepareOnly -and ($Build -or -not (Test-Path $translator))) {
    dotnet build "$src\translator\src\Translator.Cli\Translator.Cli.csproj" -c Release | Out-Host
    Invoke-Checked 'dotnet build'
}

# Python: $env:PY, else the pyenv 3.12 install (the pyenv shim fails under PowerShell here), else PATH.
$pyenv = Join-Path $env:USERPROFILE '.pyenv\pyenv-win\versions\3.12.0\python.exe'
$py = if ($env:PY) { $env:PY } elseif (Test-Path $pyenv) { $pyenv } else { 'python' }
$map = 'config\wiicompiled\rl8e54-functions.map'
if ($RegenerateMap) {
    # Needs the dtk inventories, DolRecomp's linked REL layout and Dolphin's signature database.
    $dtk = @('local\analysis\dol-functions.csv', 'local\analysis\rel-functions.csv',
             'local\generated\dolrecomp\rel-linked\layout.json')
    $dol = 'local\disc\DATA\sys\main.dol'
    $linked = 'local\generated\dolrecomp\rel-linked\linked-analysis.dol'
    # SDK names: Dolphin signatures first, then link-order alignment against Wiicompiled's MKW map
    # (which needs the signature names as anchors), then the final map with both.
    & $py tools\identify_sdk_functions.py local\vendor\ModernGekko\vendor\dolphin\Data\Sys\totaldb.dsy `
        $dol $linked $dtk[0] $dtk[1] $dtk[2] local\analysis\sdk-symbols.csv
    Invoke-Checked 'signature identification'
    & $py tools\dtk_to_wiicompiled_map.py @dtk $map local\analysis\sdk-symbols.csv
    Invoke-Checked 'map conversion'
    & $py tools\align_sdk_by_anchors.py local\vendor\Wiicompiled\projects\mkwii\MAP.txt $map `
        local\analysis\sdk-symbols-aligned.csv
    Invoke-Checked 'anchor alignment'
    & $py tools\dtk_to_wiicompiled_map.py @dtk $map local\analysis\sdk-symbols.csv `
        local\analysis\sdk-symbols-aligned.csv
    Invoke-Checked 'map conversion'
}

if ($Fresh -or -not (Test-Path "$src\runtime\include\game_natives_rebound.h")) {
    & $py tools\wiicompiled_rebind.py "$src\runtime" local\vendor\Wiicompiled\projects\mkwii\MAP.txt `
        $map config\wiicompiled\rl8e54-data.csv @Groups
    Invoke-Checked 'rebind'
}
if ($PrepareOnly) { return }

$project = @('--project', 'config\wiicompiled\rl8e54.yml')
$gen = "$work\generated"
dotnet $translator translate-recursive 0x8000403C @project --outdir "$gen\functions" `
    --output-metadata "$gen\translation-metadata.json" --threads $Threads --prune-stale | Out-Host
Invoke-Checked 'translate-recursive'
dotnet $translator generate-data-init @project | Out-Host
Invoke-Checked 'generate-data-init'
dotnet $translator emit-build-shards @project --base-metadata "$gen\translation-metadata.json" `
    --base-functions-dir "$gen\functions" --native-source-dir "$src\runtime\src" --out "$gen\build_shards" | Out-Host
Invoke-Checked 'emit-build-shards'
