# SPDX-License-Identifier: GPL-3.0-or-later
# Runs on the player's PC, inside an unpacked Setup payload:
#   <payload>\toolkit\   nodtool.exe, Translator\Translator.Cli.exe, llvm-mingw\, CMake\, Ninja\
#   <payload>\workspace\ LocalBuild.ps1 (this file), NativeBuildFlags.ps1 (Wiicompiled's shared
#                        configure flags), src\runtime + src\aurora-main (patched and rebound),
#                        Dependencies\ (pinned sources + precompiled aurora/third-party closure),
#                        config\ (project file, function map)
# It checks the player's disc image, extracts it, translates main.dol + ovl_main.rel and compiles
# the native game, then installs it into -Install. Nothing from the game ships with the payload.
# Progress for the Setup window goes to stdout as "@@ <percent> <message>"; everything else is log.
# -Lang picks the language of those messages (the Setup passes its own): es (default) or en.
param(
    [Parameter(Mandatory)] [string]$Iso,
    [Parameter(Mandatory)] [string]$Install,
    [int]$Jobs = [Environment]::ProcessorCount,
    [switch]$KeepWorkspace,
    [switch]$Shortcut,
    [ValidateSet('es', 'en')] [string]$Lang = 'es'
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
# The text in the Setup's language.
function T([string]$es, [string]$en) { if ($Lang -eq 'en') { $en } else { $es } }
# The Setup window shows this line instead of PowerShell's error dump.
trap { Write-Output "@@ERROR $($_.Exception.Message)"; exit 1 }
$workspace = $PSScriptRoot
$toolkit = Join-Path (Split-Path -Parent $workspace) 'toolkit'
$nodtool = Join-Path $toolkit 'nodtool.exe'
$translator = Join-Path $toolkit 'Translator\Translator.Cli.exe'
$llvm = Join-Path $toolkit 'llvm-mingw\bin'
$cmake = Join-Path $toolkit 'CMake\bin\cmake.exe'
. (Join-Path $workspace 'NativeBuildFlags.ps1')
$env:PATH = Get-MkwToolchainPath $toolkit
# The bundled clang must be the only toolchain the build can see.
foreach ($name in 'INCLUDE', 'LIB', 'LIBPATH', 'VSINSTALLDIR', 'VCToolsInstallDir', 'WindowsSdkDir', 'CC', 'CXX') {
    Remove-Item "Env:$name" -ErrorAction SilentlyContinue
}
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'

function Progress([int]$percent, [string]$message) { Write-Output "@@ $percent $message" }
function Invoke-Tool([string]$label, [string]$exe, [string[]]$arguments, [scriptblock]$onLine = { $args[0] }) {
    # Windows PowerShell 5.1 turns every stderr line of a native tool (CMake warnings, nodtool's
    # log) into an error record; with 'Stop' that aborts the install. Treat it as plain log text
    # and judge the tool by its exit code only.
    $ErrorActionPreference = 'Continue'
    & $exe @arguments 2>&1 | ForEach-Object { & $onLine "$_" }
    if ($LASTEXITCODE -ne 0) { throw (T "$label falló (código $LASTEXITCODE)." "$label failed (code $LASTEXITCODE).") }
}
function Get-Pin([string]$key) {
    # sha256 under inputs.dol / inputs.rel in the project file.
    $text = Get-Content (Join-Path $workspace 'config\rl8e54.yml') -Raw
    if ($text -notmatch "(?s)\n  ${key}:\s*\n.*?sha256:\s*([0-9a-f]{64})") { throw (T "Falta el hash de $key." "Missing the hash of $key.") }
    $Matches[1]
}

$Install = [IO.Path]::GetFullPath($Install)
$data = Join-Path $Install 'Data'
New-Item -ItemType Directory -Force $Install | Out-Null

Progress 1 (T 'Revisando la imagen del disco...' 'Checking the disc image...')
$info = (Invoke-Tool (T 'La lectura de la imagen del disco' 'Reading the disc image') $nodtool @('--no-color', 'info', $Iso) | Out-String)
if ($info -notmatch 'Game ID:\s*RL8E54') {
    $id = if ($info -match 'Game ID:\s*(\w{6})') { $Matches[1] } else { T 'desconocido' 'unknown' }
    throw (T "Este disco no es MLB Power Pros 2008 para Wii (USA, RL8E54). ID encontrado: $id." `
             "This disc is not MLB Power Pros 2008 for Wii (USA, RL8E54). Found ID: $id.")
}

$dol = Join-Path $data 'sys\main.dol'
$rel = Join-Path $data 'files\ovl_main.rel'
function Test-Inputs {
    (Test-Path $dol) -and (Test-Path $rel) -and
    (Get-FileHash $dol -Algorithm SHA256).Hash.ToLower() -eq (Get-Pin 'dol') -and
    (Get-FileHash $rel -Algorithm SHA256).Hash.ToLower() -eq (Get-Pin 'rel')
}
if (-not (Test-Inputs)) {
    Progress 3 (T 'Extrayendo los archivos del disco...' 'Extracting the disc files...')
    if (Test-Path $data) { Remove-Item -Recurse -Force $data }
    Invoke-Tool (T 'La extracción' 'Extraction') $nodtool @('--no-color', 'extract', $Iso, $data)
    if (-not (Test-Inputs)) {
        throw (T ('El disco no coincide con la versión esperada (main.dol u ovl_main.rel distintos). ' +
                  'Hace falta un volcado limpio y sin modificar de RL8E54.') `
                 ('The disc does not match the expected version (main.dol or ovl_main.rel differ). ' +
                  'A clean, unmodified RL8E54 dump is needed.'))
    }
}

$translating = T 'Traduciendo el código del juego (PowerPC a C++)...' 'Translating the game code (PowerPC to C++)...'
Progress 8 $translating
$src = Join-Path $workspace 'src'
$generated = Join-Path $src 'generated'
$project = Join-Path $workspace 'project.yml'
# The checked-in project file uses repository paths; point it at this workspace and the disc.
$yml = Get-Content (Join-Path $workspace 'config\rl8e54.yml') -Raw
$yml = $yml -replace '(?m)^workspace_root:.*$', 'workspace_root: .'
$yml = $yml -replace '(?m)^(    path: )local/disc/DATA/sys/main\.dol', "`$1'$($dol -replace '\\', '/')'"
$yml = $yml -replace '(?m)^(    path: )local/disc/DATA/files/ovl_main\.rel', "`$1'$($rel -replace '\\', '/')'"
$yml = $yml -replace 'config/wiicompiled/rl8e54-functions\.map', 'config/rl8e54-functions.map'
$yml = $yml -replace 'local/wiicompiled-mlb/src/', 'src/'
$yml = $yml -replace '(?m)^(  root: )local/wiicompiled-mlb/generated', '$1src/generated'
$yml = $yml -replace 'local/wiicompiled-mlb/generated/', 'src/generated/'
[IO.File]::WriteAllText($project, $yml)
$threads = [Math]::Max(1, [Math]::Min($Jobs, 8))
Invoke-Tool (T 'La traducción' 'Translation') $translator @('translate-recursive', '0x8000403C', '--project', $project,
    '--outdir', "$generated\functions", '--output-metadata', "$generated\translation-metadata.json",
    '--threads', "$threads", '--prune-stale') {
    $line = $args[0]
    # The translator reports elapsed time per phase ("(t=31.2s)"); a full run is about 90 s here.
    if ($line -match '\(t=([\d.]+)s\)') {
        "@@ $([int](8 + [Math]::Min(11, [double]$Matches[1] / 8))) $translating"
    }
    $line
}
Progress 20 (T 'Preparando los datos del juego...' 'Preparing the game data...')
Invoke-Tool (T 'La generación de datos' 'Data generation') $translator @('generate-data-init', '--project', $project)
Invoke-Tool (T 'La división en bloques' 'Splitting into shards') $translator @('emit-build-shards', '--project', $project,
    '--base-metadata', "$generated\translation-metadata.json", '--base-functions-dir', "$generated\functions",
    '--native-source-dir', "$src\runtime\src", '--out', "$generated\build_shards")

Progress 25 (T 'Configurando la compilación...' 'Configuring the build...')
$build = Join-Path $workspace 'build'
$dependencies = Join-Path $workspace 'Dependencies'
# Same flags as Wiicompiled's own LocalBuild.ps1 and Prepare-NativePrebuilt.ps1, so the precompiled
# aurora/third-party closure in Dependencies\native_prebuilt matches this build.
$configure = Get-MkwNativeConfigureArguments -SourceDirectory "$src\runtime" -BuildDirectory $build `
    -Ninja (Join-Path $toolkit 'Ninja\ninja.exe') -CCompiler (Join-Path $llvm 'x86_64-w64-mingw32-clang.exe') `
    -CxxCompiler (Join-Path $llvm 'x86_64-w64-mingw32-clang++.exe') `
    -ResourceCompiler (Join-Path $llvm 'x86_64-w64-mingw32-windres.exe') `
    -DependenciesDirectory $dependencies -NativePrebuiltDirectory (Join-Path $dependencies 'native_prebuilt') `
    -AdditionalArguments @('-DWIICOMPILED_GAME=RL8E54', "-DMKW_TRANSLATED_COMPILE_JOBS=$Jobs")
Invoke-Tool (T 'La configuración' 'Build configuration') $cmake $configure

Progress 30 (T 'Compilando el juego (esto es lo que más tarda)...' 'Compiling the game (this takes the longest)...')
Invoke-Tool (T 'La compilación' 'Compilation') $cmake @('--build', $build, '--target', 'WiiCompiled', '--parallel', "$Jobs") {
    $line = $args[0]
    if ($line -match '^\[(\d+)/(\d+)\]') {
        "@@ $([int](30 + 65 * [int]$Matches[1] / [int]$Matches[2])) " +
            (T "Compilando el juego ($($Matches[1]) de $($Matches[2]))..." "Compiling the game ($($Matches[1]) of $($Matches[2]))...")
    }
    $line
}

Progress 96 (T 'Instalando...' 'Installing...')
$game = Join-Path $Install 'Game'
New-Item -ItemType Directory -Force $game | Out-Null
Copy-Item (Join-Path $build 'WiiCompiled.exe') (Join-Path $game 'MLBPowerPros2008.exe') -Force
# Runtime files the build places next to the exe; same list as Wiicompiled's LocalBuild.ps1.
foreach ($name in 'dxcompiler.dll', 'dxil.dll', 'd3dcompiler_47.dll', 'libc++.dll', 'libpng16.dll', 'libunwind.dll', 'libz.dll', 'SDL3.dll',
                  'sqlite3.dll', 'webgpu_dawn.dll', 'z.dll', 'noshaders.zip', 'dsp_coef.bin', 'initial_pipeline_cache.db') {
    $file = Join-Path $build $name
    if (Test-Path $file) { Copy-Item $file $game -Force }
}
$art = Join-Path $workspace 'launcher'  # MoonInning launcher backdrops (optional)
if (Test-Path $art) { Copy-Item $art $game -Recurse -Force }
$editor = Join-Path $workspace 'powerupplus'  # roster editor (launcher > MODS > EDITAR ROSTER)
if (Test-Path $editor) {
    $installedEditor = Join-Path $game 'powerupplus'
    if (Test-Path $installedEditor) { Remove-Item -Recurse -Force $installedEditor }
    Copy-Item $editor $game -Recurse -Force
}
$studio = Join-Path $workspace 'TextureStudio.exe'  # texture mods (launcher > HERRAMIENTAS > EDITOR DE TEXTURAS)
if (Test-Path $studio) { Copy-Item $studio $game -Force }
$seasonStudio = Join-Path $workspace 'SeasonStudio.exe'  # season settings and schedules (launcher > HERRAMIENTAS > EDITOR DE TEMPORADA)
if (Test-Path $seasonStudio) { Copy-Item $seasonStudio $game -Force }
foreach ($name in 'config', 'wii_bootstrap') {
    $dir = Join-Path $build $name
    if (Test-Path $dir) { Copy-Item $dir $game -Recurse -Force }
}
New-Item -ItemType File -Force (Join-Path $game 'portable.txt') | Out-Null
# Player mods live here (the launcher's MODS screen); a reinstall never touches them.
New-Item -ItemType Directory -Force (Join-Path $game 'mods') | Out-Null
$userData = Join-Path $game 'UserData'
New-Item -ItemType Directory -Force $userData | Out-Null
$config = Join-Path $userData 'Config.toml'
$dvdLine = "dvd_root = '$data'"
if (Test-Path $config) {
    $text = [IO.File]::ReadAllText($config)
    $text = if ($text -match '(?m)^dvd_root\s*=') { $text -replace '(?m)^dvd_root\s*=.*$', $dvdLine.Replace('$', '$$') }
            else { "[paths]`r`n$dvdLine`r`n`r`n" + $text }
} else {
    $text = "[paths]`r`n$dvdLine`r`n"
}
[IO.File]::WriteAllText($config, $text)

if (-not $KeepWorkspace) {
    Progress 98 (T 'Limpiando archivos temporales...' 'Cleaning up temporary files...')
    foreach ($dir in $build, $generated) { if (Test-Path $dir) { Remove-Item -Recurse -Force $dir } }
}
if ($Shortcut) {
    $desktop = [Environment]::GetFolderPath('Desktop')
    $link = (New-Object -ComObject WScript.Shell).CreateShortcut((Join-Path $desktop 'MLB Power Pros 2008.lnk'))
    $link.TargetPath = Join-Path $game 'MLBPowerPros2008.exe'
    $link.WorkingDirectory = $game
    $link.Save()
    if (Test-Path (Join-Path $game 'TextureStudio.exe')) {
        $studioLink = (New-Object -ComObject WScript.Shell).CreateShortcut((Join-Path $desktop 'Power Pros Wii 2008 Texture Studio.lnk'))
        $studioLink.TargetPath = Join-Path $game 'TextureStudio.exe'
        $studioLink.WorkingDirectory = $game
        $studioLink.Save()
    }
}
Progress 100 (T 'Listo.' 'Done.')
