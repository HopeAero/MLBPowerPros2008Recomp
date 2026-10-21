# SPDX-License-Identifier: GPL-3.0-or-later
# Assembles the redistributable Setup payload (tools/setup/LocalBuild.ps1 describes its layout):
# the Setup window, the pinned toolchain, the patched + rebound runtime sources, the pinned
# dependency sources with the precompiled aurora/third-party closure, and the project files.
# Nothing from the game goes in: the player's PC extracts, translates and compiles it.
#
# -Source is a patched, rebound Wiicompiled tree (tools/wiicompiled-translate.ps1 makes one) whose
# Launcher\artifacts already hold portable-tools, dependencies and dependencies\native_prebuilt
# (Wiicompiled's Prepare-PortableTools.ps1, Prepare-Dependencies.ps1, Prepare-NativePrebuilt.ps1).
param(
    [string]$Source = 'local\wiicompiled-mlb\src',
    [string]$Output = 'local\setup-payload\MLB Power Pros 2008 Setup',
    [switch]$Zip
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $root
function Resolve-Root([string]$path) {
    if ([IO.Path]::IsPathRooted($path)) { $path } else { [IO.Path]::GetFullPath((Join-Path $root $path)) }
}
$Source = Resolve-Root $Source
$Output = Resolve-Root $Output
$artifacts = Join-Path $Source 'Launcher\artifacts'
$tools = Join-Path $artifacts 'portable-tools'
$dependencies = Join-Path $artifacts 'dependencies'
foreach ($required in "$tools\llvm-mingw", "$tools\CMake", "$tools\Ninja", "$dependencies\native_prebuilt",
                      "$Source\runtime\include\game_natives_rebound.h") {
    if (-not (Test-Path $required)) { throw "Missing $required (see the header of this script)." }
}
$env:PATH = "$tools\llvm-mingw\bin;$tools\CMake\bin;$tools\Ninja;$env:PATH"
$env:TEMP = $env:TMP = Join-Path $root 'local\cache\dotnet-home\tmp'
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
function Invoke-Checked([string]$label) { if ($LASTEXITCODE -ne 0) { throw "$label failed ($LASTEXITCODE)" } }
function Copy-Tree([string]$from, [string]$to, [string[]]$excludeDirs = @()) {
    # robocopy: fast, keeps the tree, skips the excluded directory names anywhere below.
    $args = @($from, $to, '/E', '/NFL', '/NDL', '/NJH', '/NJS', '/NP', '/R:1', '/W:1')
    if ($excludeDirs) { $args += '/XD'; $args += $excludeDirs }
    robocopy @args | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy $from failed ($LASTEXITCODE)" }
}

if (Test-Path $Output) { Remove-Item -Recurse -Force $Output }
$toolkit = Join-Path $Output 'toolkit'
$workspace = Join-Path $Output 'workspace'
New-Item -ItemType Directory -Force $toolkit, $workspace, "$workspace\config", "$workspace\src" | Out-Null

Write-Host 'Toolchain...'
Copy-Tree "$tools\llvm-mingw" "$toolkit\llvm-mingw"
# Only x86_64 Windows is built: drop the other targets' sysroots, Linux runtimes and the bundled
# Python (lldb only). Saves ~320 MB.
foreach ($unused in 'i686-w64-mingw32', 'armv7-w64-mingw32', 'arm64ec-w64-mingw32', 'aarch64-w64-mingw32', 'python',
                    'busybox', 'lib\clang\22\lib\linux') {
    $path = Join-Path "$toolkit\llvm-mingw" $unused
    if (Test-Path $path) { Remove-Item -Recurse -Force $path }
}
Copy-Tree "$tools\CMake" "$toolkit\CMake" @('doc', 'man')
Copy-Tree "$tools\Ninja" "$toolkit\Ninja"
Copy-Item "$tools\README-LICENSES.txt" $toolkit
$nodtool = Join-Path $root 'local\tools\nodtool\nodtool.exe'
$nodtoolSha = '1e150e33b88157527f96caea89ad12267aed8b117180c9ebaa89ebe89c1d948d'  # encounter/nod v2.0.0-alpha.10
if (-not (Test-Path $nodtool) -or (Get-FileHash $nodtool).Hash.ToLower() -ne $nodtoolSha) {
    New-Item -ItemType Directory -Force (Split-Path $nodtool) | Out-Null
    Invoke-WebRequest -UseBasicParsing -OutFile $nodtool `
        'https://github.com/encounter/nod/releases/download/v2.0.0-alpha.10/nodtool-windows-x86_64.exe'
    if ((Get-FileHash $nodtool).Hash.ToLower() -ne $nodtoolSha) { throw 'nodtool hash mismatch' }
}
Copy-Item $nodtool $toolkit

Write-Host 'Translator...'
dotnet publish "$Source\translator\src\Translator.Cli\Translator.Cli.csproj" -c Release -r win-x64 `
    --self-contained true -p:PublishSingleFile=true -o "$toolkit\Translator" | Out-Host
Invoke-Checked 'translator publish'

Write-Host 'Workspace...'
Copy-Item "$root\tools\setup\LocalBuild.ps1" $workspace
Copy-Item "$Source\Launcher\NativeBuildFlags.ps1" $workspace
Copy-Item "$root\config\wiicompiled\rl8e54.yml", "$root\config\wiicompiled\rl8e54-functions.map" "$workspace\config"
Copy-Tree "$root\assets\launcher" "$workspace\launcher"
Copy-Tree "$Source\runtime" "$workspace\src\runtime" @('build', 'out', '.git')
Copy-Tree "$Source\aurora-main" "$workspace\src\aurora-main" @('build', 'out', '.git')
Copy-Tree $dependencies "$workspace\Dependencies"

Write-Host 'Roster editor (PowerUp+)...'
# Self-contained, with PowerUp+'s libraries but not its 2007 saves (game content); the launcher
# starts it with the player's own project.bin as the base roster.
& "$root\tools\powerupplus.ps1" -Publish "$workspace\powerupplus" | Out-Host

Write-Host 'Setup window...'
$setupBuild = Join-Path $root 'local\build\setup'
$tc = "$tools\llvm-mingw\bin".Replace('\', '/')
cmake -S "$root\tools\setup" -B $setupBuild -G Ninja -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_C_COMPILER=$tc/clang.exe" "-DCMAKE_CXX_COMPILER=$tc/clang++.exe" "-DCMAKE_RC_COMPILER=$tc/llvm-windres.exe" `
    "-DSDL_SOURCE_DIR=$("$dependencies\SDL".Replace('\', '/'))" "-DIMGUI_SOURCE_DIR=$("$dependencies\imgui".Replace('\', '/'))" `
    "-DUI_INCLUDE_DIR=$("$Source\runtime\include".Replace('\', '/'))" | Out-Host
Invoke-Checked 'setup configure'
cmake --build $setupBuild --parallel | Out-Host
Invoke-Checked 'setup build'
Copy-Item "$setupBuild\MLBPowerPros2008-Setup.exe" $Output

Write-Host 'Texture Studio...'
# Power Pros Wii 2008 Texture Studio (tools/texture-studio): texture mods, installed next to the game.
$studioBuild = Join-Path $root 'local\build\texture-studio'
& "$root\tools\texture-studio\build.ps1" -Source $Source -Output $studioBuild
Copy-Item "$studioBuild\TextureStudio.exe" $workspace

Write-Host 'Season Studio...'
# Power Pros Wii 2008 Season Studio (tools/season-studio): season settings and schedules, next to the game.
$seasonBuild = Join-Path $root 'local\build\season-studio'
& "$root\tools\season-studio\build.ps1" -Source $Source -Output $seasonBuild
Copy-Item "$seasonBuild\SeasonStudio.exe" $workspace

Write-Host 'Licenses...'
New-Item -ItemType Directory -Force "$Output\licenses" | Out-Null
Copy-Item "$root\LICENSE" "$Output\licenses\MLBPowerPros2008Recomp-LICENSE.txt"
Copy-Item "$root\THIRD_PARTY.md" "$Output\licenses\THIRD_PARTY.md"
Copy-Item "$Source\LICENSE" "$Output\licenses\Wiicompiled-LICENSE.txt"
Copy-Item "$Source\THIRD-PARTY-NOTICES.md" "$Output\licenses\Wiicompiled-THIRD-PARTY-NOTICES.md"
# PowerUp+ (GPLv3): its license, the commit it is built from and our patch, the source of the build.
Copy-Item "$root\local\vendor\PowerUpPlus\LICENSE" "$Output\licenses\PowerUpPlus-LICENSE.txt"
Copy-Item "$root\tools\patches\powerupplus-2008.patch" "$Output\licenses\PowerUpPlus-2008.patch"
Set-Content -Encoding utf8 "$Output\licenses\PowerUpPlus-SOURCE.txt" (
    "PowerUp+ (https://github.com/ogura-connection/PowerUpPlus, commit eb1a2c2, GPLv3) with PowerUpPlus-2008.patch " +
    "from the MLB Power Pros 2008 recomp project (tools/patches), built by tools/powerupplus.ps1 -Publish.")
foreach ($license in 'LICENSE-MIT', 'LICENSE-APACHE') {  # nodtool is MIT OR Apache-2.0
    Invoke-WebRequest -UseBasicParsing -OutFile "$Output\licenses\nodtool-$license.txt" `
        "https://raw.githubusercontent.com/encounter/nod/v2.0.0-alpha.10/$license"
}

Write-Host 'Checking that no game content is in the payload...'
$bad = Get-ChildItem $Output -Recurse -File | Where-Object {
    $_.Extension -in '.iso', '.gcm', '.wbfs', '.rvz', '.gcz', '.ciso', '.wia', '.nfs', '.dol', '.rel', '.thp', '.bnr', '.brres', '.tpl', '.arc', '.szs' -or
    $_.Name -in 'main.dol', 'ovl_main.rel', 'project.bin', 'game_data.pack', 'data_sections_init.cpp', 'data_sections_init_blobs.S', 'base_manifest.json' -or
    $_.Name -like '*pm2maus*.dat' -or $_.Name -like '*_Rosters.dat' -or
    $_.FullName -match '\\(generated|build_shards)\\'
}
if ($bad) { $bad | ForEach-Object { $_.FullName }; throw 'Game content found in the payload.' }
$size = (Get-ChildItem $Output -Recurse -File | Measure-Object Length -Sum).Sum
Write-Host ("Payload ready: {0} ({1:N0} MB)" -f $Output, ($size / 1MB))
if ($Zip) {
    $zipPath = "$Output.zip"
    if (Test-Path $zipPath) { Remove-Item $zipPath }
    Compress-Archive -Path $Output -DestinationPath $zipPath -CompressionLevel Optimal
    Write-Host ("Zip: {0} ({1:N0} MB)" -f $zipPath, ((Get-Item $zipPath).Length / 1MB))
}
