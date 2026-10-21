# SPDX-License-Identifier: GPL-3.0-or-later
# Build the Wiicompiled runtime with MLB's translated code (run tools/wiicompiled-translate.ps1
# first) and optionally run it for a few seconds. Uses Wiicompiled's pinned LLVM-MinGW from
# local/tools; the runtime runs in portable mode so Config.toml, NAND and caches stay in
# local/wiicompiled-mlb/build/UserData instead of %LOCALAPPDATA%.
param([int]$Jobs = 10, [int]$RunSeconds = 0)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$work = Join-Path $root 'local\wiicompiled-mlb'
$build = Join-Path $work 'build'
$toolchain = Join-Path $root 'local\tools\llvm-mingw-20260616-ucrt-x86_64\bin'
if (-not (Test-Path "$toolchain\clang++.exe")) {
    throw "Missing $toolchain (llvm-mingw-20260616-ucrt-x86_64.zip, sha256 b9b68a4d...)"
}
$env:PATH = "$toolchain;$env:PATH"
$env:TEMP = $env:TMP = Join-Path $root 'local\cache\dotnet-home\tmp'

function Invoke-Checked([string]$label) {
    if ($LASTEXITCODE -ne 0) { throw "$label failed with exit code $LASTEXITCODE" }
}

# cmake reports progress on stderr, which PowerShell 5.1 turns into terminating errors: log it.
$buildLog = Join-Path $root 'local\analysis\wiicompiled-build.log'
if (-not (Test-Path "$build\CMakeCache.txt")) {
    $tc = $toolchain.Replace('\', '/')
    $configure = "cmake -S `"$work\src\runtime`" -B `"$build`" -G Ninja " +
        "-DCMAKE_C_COMPILER=$tc/clang.exe -DCMAKE_CXX_COMPILER=$tc/clang++.exe " +
        "-DCMAKE_RC_COMPILER=$tc/llvm-windres.exe -DCMAKE_BUILD_TYPE=Release " +
        "-DCMAKE_SYSTEM_PROCESSOR=x86_64 -DAURORA_DAWN_PROVIDER=package -DAURORA_SDL3_PROVIDER=vendor " +
        "-DCMAKE_POLICY_DEFAULT_CMP0168=NEW -DAWK:FILEPATH= -DWIICOMPILED_GAME=RL8E54 " +
        "-DMKW_TRANSLATED_COMPILE_JOBS=$Jobs"
    cmd /c "$configure > `"$buildLog`" 2>&1"
    Invoke-Checked "configure (see $buildLog)"
}
cmd /c "cmake --build `"$build`" --target WiiCompiled --parallel $Jobs > `"$buildLog`" 2>&1"
if ($LASTEXITCODE -ne 0) {
    Select-String -Path $buildLog -Pattern 'error' | Select-Object -First 20 | ForEach-Object { $_.Line }
}
Invoke-Checked "build (see $buildLog)"

New-Item -ItemType File -Force "$build\portable.txt" | Out-Null
# Launcher backdrops (MoonInning UI pack art, see tools/launcher/prepare_mooninning_art.py).
New-Item -ItemType Directory -Force "$build\launcher" | Out-Null
Copy-Item (Join-Path $root 'assets\launcher\*.png') "$build\launcher" -Force
New-Item -ItemType Directory -Force "$build\UserData" | Out-Null
$dvd = Join-Path $root 'local\disc\DATA'
# Only point paths.dvd_root at the disc; keep every other setting (video, controls) the user saved.
$config = "$build\UserData\Config.toml"
$dvdLine = "dvd_root = '$dvd'"
$text = if (Test-Path $config) { [IO.File]::ReadAllText($config) } else { '' }
if ($text -match '(?m)^dvd_root\s*=') {
    $text = $text -replace '(?m)^dvd_root\s*=.*$', $dvdLine.Replace('$', '$$')
} elseif ($text -match '(?m)^\[paths\]') {
    $text = $text -replace '(?m)^\[paths\]\r?$', "[paths]`r`n$($dvdLine.Replace('$', '$$'))"
} else {
    $text = "[paths]`r`n$dvdLine`r`n`r`n" + $text
}
[IO.File]::WriteAllText($config, $text)

# Texture Studio next to the game, so the launcher's EDITOR DE TEXTURAS works in this build too.
powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools\texture-studio\build.ps1') | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Warning 'Texture Studio build failed (tools\texture-studio\build.ps1)' }
else { Copy-Item (Join-Path $root 'local\build\texture-studio\TextureStudio.exe') $build -Force }
# Season Studio too (the launcher's EDITOR DE TEMPORADA).
powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools\season-studio\build.ps1') | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Warning 'Season Studio build failed (tools\season-studio\build.ps1)' }
else { Copy-Item (Join-Path $root 'local\build\season-studio\SeasonStudio.exe') $build -Force }

if ($RunSeconds -gt 0) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $log = Join-Path $root "local\analysis\wiicompiled-run-$stamp"
    $process = Start-Process "$build\WiiCompiled.exe" -WorkingDirectory $build -PassThru `
        -RedirectStandardOutput "$log.out.log" -RedirectStandardError "$log.err.log"
    if (-not $process.WaitForExit($RunSeconds * 1000)) { Stop-Process -Id $process.Id -Force }
    Select-String -Path "$log.err.log" -Pattern 'Runtime error|Reason|Address:|Entry|halt|fatal' |
        Select-Object -First 12 | ForEach-Object { $_.Line }
    "logs: $log.*.log"
}
