# SPDX-License-Identifier: GPL-3.0-or-later
# Configures and builds Power Pros Wii 2008 Uniform Studio with the pinned llvm-mingw, CMake and Ninja
# from a prepared Wiicompiled tree (tools/wiicompiled-translate.ps1 makes one). Output: <Output>\UniformStudio.exe
# It also copies the Python bridge (studio_bridge/bake/import_sources/packwii) next to the exe. The body
# mould is drawn by the editor itself; a precise, game-derived region map is never shipped.
param(
    [string]$Source = 'local\wiicompiled-mlb\src',
    [string]$Output = 'local\build\uniform-studio'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
function Resolve-Root([string]$path) { if ([IO.Path]::IsPathRooted($path)) { $path } else { [IO.Path]::GetFullPath((Join-Path $root $path)) } }
$Source = Resolve-Root $Source
$Output = Resolve-Root $Output
$tools = Join-Path $Source 'Launcher\artifacts\portable-tools'
$deps = Join-Path $Source 'Launcher\artifacts\dependencies'
$env:PATH = "$tools\llvm-mingw\bin;$tools\CMake\bin;$tools\Ninja;$env:PATH"
$tc = "$tools\llvm-mingw\bin".Replace('\', '/')
function Fwd([string]$p) { $p.Replace('\', '/') }
cmake -S $PSScriptRoot -B $Output -G Ninja -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_C_COMPILER=$tc/clang.exe" "-DCMAKE_CXX_COMPILER=$tc/clang++.exe" "-DCMAKE_RC_COMPILER=$tc/llvm-windres.exe" `
    "-DSDL_SOURCE_DIR=$(Fwd "$deps\SDL")" "-DIMGUI_SOURCE_DIR=$(Fwd "$deps\imgui")" `
    "-DUI_INCLUDE_DIR=$(Fwd "$Source\runtime\include")" | Out-Host
if ($LASTEXITCODE -ne 0) { throw "uniform studio configure failed ($LASTEXITCODE)" }
cmake --build $Output --parallel | Out-Host
if ($LASTEXITCODE -ne 0) { throw "uniform studio build failed ($LASTEXITCODE)" }
# Python bridge beside the exe so it runs from the build folder.
foreach ($f in 'studio_bridge.py', 'bake.py', 'import_sources.py', 'game_doll.py') {
    Copy-Item (Join-Path $PSScriptRoot $f) $Output -Force
}
Copy-Item (Join-Path $root 'tools\packwii.py') $Output -Force
Copy-Item (Join-Path $root 'tools\projectbin.py') $Output -Force  # the "gamebase" op reads the player's project.bin
Copy-Item (Join-Path $root 'tools\blender\powerpros_stadium\pj7m.py') $Output -Force  # game_doll.py reads the figure
