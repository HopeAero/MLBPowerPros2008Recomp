# SPDX-License-Identifier: GPL-3.0-or-later
# Configures and builds Power Pros Wii 2008 Callname Studio with the pinned llvm-mingw, CMake and Ninja
# from a prepared Wiicompiled tree (tools/wiicompiled-translate.ps1 makes one). Output: <Output>\CallnameStudio.exe
param(
    [string]$Source = 'local\wiicompiled-mlb\src',
    [string]$Output = 'local\build\callname-studio'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
function Resolve-Root([string]$path) { if ([IO.Path]::IsPathRooted($path)) { $path } else { [IO.Path]::GetFullPath((Join-Path $root $path)) } }
$Source = Resolve-Root $Source
$Output = Resolve-Root $Output
$tools = Join-Path $Source 'Launcher\artifacts\portable-tools'
$deps = Join-Path $Source 'Launcher\artifacts\dependencies'
if (-not (Test-Path "$tools\llvm-mingw\bin\clang++.exe") -or -not (Test-Path "$Source\runtime\src\callnames\audio.cpp")) {
    throw 'Callname Studio needs a prepared Wiicompiled source tree (-Source).'
}
$env:PATH = "$tools\llvm-mingw\bin;$tools\CMake\bin;$tools\Ninja;$env:PATH"
$tc = "$tools\llvm-mingw\bin".Replace('\', '/')
$ninja = if (Test-Path 'C:\Strawberry\c\bin\ninja.exe') { 'C:/Strawberry/c/bin/ninja.exe' } else { "$tools/Ninja/ninja.exe" }
function Fwd([string]$p) { $p.Replace('\', '/') }
cmake -S $PSScriptRoot -B $Output -G Ninja -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_C_COMPILER=$tc/clang.exe" "-DCMAKE_CXX_COMPILER=$tc/clang++.exe" "-DCMAKE_RC_COMPILER=$tc/llvm-windres.exe" `
    "-DSDL_SOURCE_DIR=$(Fwd "$deps\SDL")" "-DIMGUI_SOURCE_DIR=$(Fwd "$deps\imgui")" `
    "-DUI_INCLUDE_DIR=$(Fwd "$Source\runtime\include")" "-DTOML_INCLUDE_DIR=$(Fwd "$Source\runtime\third_party\toml11")" | Out-Host
if ($LASTEXITCODE -ne 0) { throw "callname studio configure failed ($LASTEXITCODE)" }
cmake --build $Output --parallel | Out-Host
if ($LASTEXITCODE -ne 0) { throw "callname studio build failed ($LASTEXITCODE)" }

