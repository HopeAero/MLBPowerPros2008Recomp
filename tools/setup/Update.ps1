# SPDX-License-Identifier: GPL-3.0-or-later
# Runs inside <Install>\Build\workspace (the launcher / Setup start it). Phases:
#   apply     verify the downloaded zips (SHA-256 against <Downloads>\update-manifest.txt), sync every
#             piece root of Build\ by content, then install the new manifest.
#   build     LocalBuild.ps1 -Update -Stage <Install>\Staging (Game is not touched).
#   swap      back up the saves, move the current program to Previous and the Staging one to Game.
#   rollback  swap Game <-> Previous (program only); -RestoreSaves also puts Backup back in UserData.
# Progress goes to stdout as "@@ <percent> <message>", failures as "@@ERROR <message>". A failed swap or
# rollback also writes <Install>\Update\last-error.txt ("<phase>\n<message>"), which the launcher shows
# once, and with -Relaunch starts the program left in Game (the old one when the switch was undone).
param(
    [Parameter(Mandatory)] [string]$Install,
    [Parameter(Mandatory)] [ValidateSet('apply', 'build', 'swap', 'rollback')] [string]$Phase,
    [string]$Downloads,
    [int]$Jobs = [Environment]::ProcessorCount,
    [switch]$RestoreSaves,
    # swap/rollback: the launcher hands off and exits; wait for it (its files are in Game), then relaunch.
    [int]$WaitPid = 0,
    [int]$WaitSeconds = 60,  # how long -WaitPid may take to exit (tests pass less)
    [switch]$Relaunch,
    [ValidateSet('es', 'en')] [string]$Lang = 'es'
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
function T([string]$es, [string]$en) { if ($Lang -eq 'en') { $en } else { $es } }
# swap / rollback run after the launcher exited, so nobody reads this output: the error also goes to
# <Install>\Update\last-error.txt (the next launcher shows it once) and the program is started again.
trap {
    $message = $_.Exception.Message
    Write-Output "@@ERROR $message"
    if ($Phase -eq 'swap' -or $Phase -eq 'rollback') {
        try {
            $updateDir = Join-Path $Install 'Update'
            New-Item -ItemType Directory -Force -Path $updateDir | Out-Null
            [IO.File]::WriteAllText((Join-Path $updateDir 'last-error.txt'), "$Phase`n$message`n", (New-Object Text.UTF8Encoding $false))
        } catch { Write-Output "warning: could not write last-error.txt: $($_.Exception.Message)" }
        if (Get-Command Start-Game -ErrorAction SilentlyContinue) { Start-Game }
    }
    exit 1
}
function Progress([int]$percent, [string]$message) { Write-Output "@@ $percent $message" }

$workspace = $PSScriptRoot
. (Join-Path $workspace 'ProgramItems.ps1')  # Get-ProgramItems
$Install = [IO.Path]::GetFullPath($Install)
$build = Join-Path $Install 'Build'
$game = Join-Path $Install 'Game'
$staging = Join-Path $Install 'Staging'
$previous = Join-Path $Install 'Previous'
$backup = Join-Path $Install 'Backup'
$userData = Join-Path $game 'UserData'
if (-not $Downloads) { $Downloads = Join-Path $Install 'Update\downloads' }
$installedManifest = Join-Path $build 'update-manifest.txt'
$rollbackTmp = Join-Path $Install 'Rollback.tmp'
$lastError = Join-Path $Install 'Update\last-error.txt'
# What the saves backup covers (relative to UserData). The cache never.
$saveItems = @('NAND', 'ModSaves', 'arrange', 'Config.toml')

function Get-ManifestVersion([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    foreach ($line in [IO.File]::ReadAllLines($path)) { if ($line -match '^version\s+(\S+)') { return $Matches[1] } }
    $null
}
function Get-GameVersion {
    # The program carries its own version (LocalBuild writes Game\version.txt).
    $v = Join-Path $game 'version.txt'
    if (Test-Path -LiteralPath $v) { $t = ([IO.File]::ReadAllText($v)).Trim(); if ($t) { return $t } }
    'unknown'
}
function Wait-Launcher {
    if ($WaitPid -gt 0) {
        $proc = Get-Process -Id $WaitPid -ErrorAction SilentlyContinue
        if ($proc -and -not $proc.WaitForExit($WaitSeconds * 1000)) {
            throw (T "El launcher (pid $WaitPid) no se cerró en $WaitSeconds s. No se cambió nada." "The launcher (pid $WaitPid) did not exit within $WaitSeconds s. Nothing was changed.")
        }
    }
    # Programs the launcher started outlive it (Texture, Season, Callname and Uniform Studio, Uniform
    # Studio's Python): files they hold open in Game would make the move fail halfway.
    $prefix = $game.TrimEnd('\') + '\'
    $deadline = (Get-Date).AddSeconds([Math]::Min(10, $WaitSeconds))
    while ($true) {
        $open = @(Get-Process | Where-Object { $_.Id -ne $PID -and $_.Path -and $_.Path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) })
        if ($open.Count -eq 0) { return }
        if ((Get-Date) -ge $deadline) { break }
        Start-Sleep -Milliseconds 500
    }
    $names = (@($open | ForEach-Object { $_.ProcessName }) | Sort-Object -Unique) -join ', '
    throw (T "Cierra $names y vuelve a intentarlo: tiene abiertos archivos del juego. No se cambió nada." `
             "Close $names and try again: it has game files open. Nothing was changed.")
}
$script:gameStarted = $false
function Start-Game {
    # At most once, never while the launcher still runs (two copies), and never throwing: a failed start
    # must not hide the error that led here.
    if (-not $Relaunch -or $script:gameStarted) { return }
    $exe = Join-Path $game 'MLBPowerPros2008.exe'
    if (-not (Test-Path -LiteralPath $exe)) { return }
    if ($WaitPid -gt 0) {
        # A guard may fail before Wait-Launcher ran: the launcher is still closing.
        $proc = Get-Process -Id $WaitPid -ErrorAction SilentlyContinue
        if ($proc -and -not $proc.WaitForExit($WaitSeconds * 1000)) { return }
    }
    $script:gameStarted = $true
    try {
        # MLBPP_UPDATE_TEST_RELAUNCH (tests only): a file that records the start instead; '!fail' makes it fail.
        $hook = $env:MLBPP_UPDATE_TEST_RELAUNCH
        if ($hook -eq '!fail') { throw 'test: the program could not be started' }
        if ($hook) { [IO.File]::AppendAllText($hook, "start $exe`n", (New-Object Text.UTF8Encoding $false)); return }
        Start-Process -FilePath $exe -WorkingDirectory $game
    } catch { Write-Output "warning: could not start ${exe}: $($_.Exception.Message)" }
}
function Write-Text([string]$path, [string]$text) {
    [IO.File]::WriteAllText($path, $text + "`n", (New-Object Text.UTF8Encoding $false))
}
function Reset-Dir([string]$path) {
    if (Test-Path -LiteralPath $path) { Remove-Item -Recurse -Force -LiteralPath $path }
    New-Item -ItemType Directory -Force -Path $path | Out-Null
}
function Move-Items([string]$from, [string[]]$names, [string]$to) {
    # Plain renames (one volume): all or nothing. Move-Item falls back to copying a folder whose files are
    # open and leaves a half-made target behind, into which an undo would then move the old folder.
    foreach ($n in $names) {
        $src = Join-Path $from $n; $dst = Join-Path $to $n
        if (Test-Path -LiteralPath $dst) { throw (T "Ya existe $dst." "$dst already exists.") }
        try {
            if (Test-Path -LiteralPath $src -PathType Container) { [IO.Directory]::Move($src, $dst) } else { [IO.File]::Move($src, $dst) }
        } catch {
            $why = if ($_.Exception.InnerException) { $_.Exception.InnerException.Message } else { $_.Exception.Message }
            throw (T "No se pudo mover ${src}: $why" "Could not move ${src}: $why")
        }
    }
}

function Read-Pieces([string]$path) {
    # piece <name> <zip> <sha256> <bytes> <content hash> <root1>;<root2>...
    foreach ($line in [IO.File]::ReadAllLines($path)) {
        $f = $line.Trim() -split '\s+'
        if ($f.Count -ge 7 -and $f[0] -eq 'piece') {
            [pscustomobject]@{ Name = $f[1]; Zip = $f[2]; Sha = $f[3]; Roots = ($f[6] -split ';') }
        }
    }
}
function Test-Protected([string]$root) {
    # The local build tree and generated sources are never part of an update.
    $r = $root.Replace('\', '/').Trim('/')
    # An empty root (a stray ';') would mirror the whole zip over Build\.
    if (-not $r -or $r -match '(^|/)\.\.?(/|$)' -or [IO.Path]::IsPathRooted($r)) { return $true }
    foreach ($p in 'workspace/build', 'workspace/src/generated', 'workspace/project.yml') {
        if ($r -ieq $p -or $r.StartsWith("$p/", [StringComparison]::OrdinalIgnoreCase) -or $p.StartsWith("$r/", [StringComparison]::OrdinalIgnoreCase)) { return $true }
    }
    $false
}
function Sync-File([string]$src, [string]$dst) {
    if (Test-Path -LiteralPath $dst -PathType Leaf) {
        $a = Get-Item -LiteralPath $src; $b = Get-Item -LiteralPath $dst
        if ($a.Length -eq $b.Length -and (Get-FileHash -LiteralPath $src).Hash -eq (Get-FileHash -LiteralPath $dst).Hash) { return }
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
    # A changed file is written fresh and gets the current time: an older timestamp would make Ninja skip it.
    Copy-Item -LiteralPath $src -Destination $dst -Force
    (Get-Item -LiteralPath $dst).LastWriteTime = Get-Date
}
function Sync-Root([string]$srcRoot, [string]$dstRoot) {
    if (Test-Path -LiteralPath $srcRoot -PathType Leaf) {
        if (Test-Path -LiteralPath $dstRoot -PathType Container) { Remove-Item -Recurse -Force -LiteralPath $dstRoot }
        Sync-File $srcRoot $dstRoot
        return
    }
    if (-not (Test-Path -LiteralPath $srcRoot -PathType Container)) {
        if (Test-Path -LiteralPath $dstRoot) { Remove-Item -Recurse -Force -LiteralPath $dstRoot }
        return
    }
    if (Test-Path -LiteralPath $dstRoot -PathType Leaf) { Remove-Item -Force -LiteralPath $dstRoot }
    $srcBase = (Get-Item -LiteralPath $srcRoot).FullName.TrimEnd('\')
    $seen = @{}
    foreach ($f in Get-ChildItem -LiteralPath $srcRoot -Recurse -File -Force) {
        $rel = $f.FullName.Substring($srcBase.Length + 1)
        $seen[$rel.ToLowerInvariant()] = $true
        Sync-File $f.FullName (Join-Path $dstRoot $rel)
    }
    if (Test-Path -LiteralPath $dstRoot -PathType Container) {
        $dstBase = (Get-Item -LiteralPath $dstRoot).FullName.TrimEnd('\')
        foreach ($f in @(Get-ChildItem -LiteralPath $dstRoot -Recurse -File -Force)) {
            if (-not $seen.ContainsKey($f.FullName.Substring($dstBase.Length + 1).ToLowerInvariant())) {
                Remove-Item -Force -LiteralPath $f.FullName
            }
        }
        # Empty folders left behind, deepest first.
        foreach ($d in @(Get-ChildItem -LiteralPath $dstRoot -Recurse -Directory -Force | Sort-Object { $_.FullName.Length } -Descending)) {
            if (-not (Get-ChildItem -LiteralPath $d.FullName -Force)) { Remove-Item -Force -LiteralPath $d.FullName }
        }
    }
}
function Expand-Zip([string]$zip, [string]$dest) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $z = [IO.Compression.ZipFile]::OpenRead($zip)
    try {
        $destFull = [IO.Path]::GetFullPath($dest).TrimEnd('\') + '\'
        foreach ($e in $z.Entries) {
            if (-not $e.Name) { continue }  # folder entry
            $target = [IO.Path]::GetFullPath((Join-Path $dest ($e.FullName.Replace('/', '\'))))
            if (-not $target.StartsWith($destFull, [StringComparison]::OrdinalIgnoreCase)) { throw (T "Ruta sospechosa en el zip: $($e.FullName)" "Suspicious path in the zip: $($e.FullName)") }
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($e, $target, $true)
        }
    } finally { $z.Dispose() }
}

switch ($Phase) {
    'apply' {
        $newManifest = Join-Path $Downloads 'update-manifest.txt'
        if (-not (Test-Path -LiteralPath $newManifest)) { throw (T "Falta update-manifest.txt en $Downloads" "Missing update-manifest.txt in $Downloads") }
        $newVersion = Get-ManifestVersion $newManifest
        $todo = @()
        foreach ($p in @(Read-Pieces $newManifest)) {
            if ($p.Zip -eq '-') { continue }
            # A bare file name in Downloads, nothing else.
            if ($p.Zip -match '[\\/:]' -or $p.Zip -match '^\.\.?$') { throw (T "Nombre de archivo no permitido en el manifiesto: $($p.Zip)" "File name not allowed in the manifest: $($p.Zip)") }
            $zip = Join-Path $Downloads $p.Zip
            if (Test-Path -LiteralPath $zip) { $todo += $p }  # pieces not downloaded are left alone
        }
        # Verify everything before the first write.
        $i = 0
        foreach ($p in $todo) {
            Progress ([int](5 * $i / [Math]::Max(1, $todo.Count))) (T "Comprobando $($p.Name)..." "Checking $($p.Name)...")
            if ((Get-FileHash -LiteralPath (Join-Path $Downloads $p.Zip) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $p.Sha.ToLowerInvariant()) {
                throw (T "La descarga $($p.Zip) está dañada (SHA-256 distinto). No se cambió nada." "The download $($p.Zip) is corrupt (SHA-256 mismatch). Nothing was changed.")
            }
            foreach ($r in $p.Roots) { if (Test-Protected $r) { throw (T "Raíz no permitida en el manifiesto: $r" "Root not allowed in the manifest: $r") } }
            $i++
        }
        $tmp = Join-Path $Install 'Update\extract'
        $i = 0
        foreach ($p in $todo) {
            Progress ([int](5 + 90 * $i / [Math]::Max(1, $todo.Count))) (T "Aplicando $($p.Name)..." "Applying $($p.Name)...")
            Reset-Dir $tmp
            Expand-Zip (Join-Path $Downloads $p.Zip) $tmp
            foreach ($r in $p.Roots) {
                $rel = $r.Replace('/', '\')
                Sync-Root (Join-Path $tmp $rel) (Join-Path $build $rel)
            }
            $i++
        }
        if (Test-Path -LiteralPath $tmp) { Remove-Item -Recurse -Force -LiteralPath $tmp }
        Copy-Item -LiteralPath $newManifest -Destination $installedManifest -Force
        Progress 100 (T 'Piezas aplicadas.' 'Pieces applied.')
    }
    'build' {
        $lb = Join-Path $workspace 'LocalBuild.ps1'
        if (Test-Path -LiteralPath $staging) { Remove-Item -Recurse -Force -LiteralPath $staging }
        $ErrorActionPreference = 'Continue'
        & powershell -NoProfile -ExecutionPolicy Bypass -File $lb -Update -Stage $staging -Install $Install -Jobs $Jobs -Lang $Lang
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }  # LocalBuild already printed @@ERROR
    }
    'swap' {
        Remove-Item -Force -LiteralPath $lastError -ErrorAction SilentlyContinue  # an old one the launcher never showed
        if (-not (Test-Path -LiteralPath (Join-Path $staging 'MLBPowerPros2008.exe'))) { throw (T 'No hay una versión compilada en Staging.' 'There is no built version in Staging.') }
        # Never delete the only copy: a half-moved Game (or leftover Rollback.tmp) means Previous may hold the program.
        if (Test-Path -LiteralPath $rollbackTmp) { throw (T "Quedó $rollbackTmp de un cambio interrumpido; no se toca nada. Usa REINSTALAR." "$rollbackTmp is left over from an interrupted switch; nothing was touched. Use REINSTALL.") }
        if (-not (Test-Path -LiteralPath (Join-Path $game 'MLBPowerPros2008.exe'))) { throw (T 'Game está incompleto; no se toca nada. Usa REINSTALAR.' 'Game is incomplete; nothing was touched. Use REINSTALL.') }
        Wait-Launcher
        $old = Get-GameVersion
        Progress 5 (T 'Copiando las partidas...' 'Backing up the saves...')
        Reset-Dir $backup
        foreach ($n in $saveItems) {
            $src = Join-Path $userData $n
            if (Test-Path -LiteralPath $src) { Copy-Item -LiteralPath $src -Destination (Join-Path $backup $n) -Recurse -Force }
        }
        Write-Text (Join-Path $backup 'version.txt') $old
        Progress 40 (T 'Cambiando de versión...' 'Switching versions...')
        Reset-Dir $previous
        # ponytail: renames inside one volume, no journal. A power cut between the two moves leaves
        # Game half old / half new; the Setup's REINSTALAR rebuilds it. Not worth a transaction log.
        $oldItems = @(Get-ProgramItems $game)
        $newItems = @(Get-ProgramItems $staging)
        $movedOld = @(); $movedNew = @()
        try {
            foreach ($n in $oldItems) { Move-Items $game @($n) $previous; $movedOld += $n }
            foreach ($n in $newItems) { Move-Items $staging @($n) $game; $movedNew += $n }
        } catch {
            # Undo, then the trap writes last-error.txt and starts the program that is in Game again.
            $err = $_
            $undone = $true
            foreach ($n in $movedNew) { try { Move-Items $game @($n) $staging } catch { $undone = $false } }
            foreach ($n in $movedOld) { try { Move-Items $previous @($n) $game } catch { $undone = $false } }
            if (-not $undone) { throw (T "$($err.Exception.Message) No se pudo deshacer el cambio: usa REINSTALAR." "$($err.Exception.Message) The switch could not be undone: use REINSTALL.") }
            throw $err
        }
        if (Test-Path -LiteralPath $staging) { Remove-Item -Recurse -Force -LiteralPath $staging }
        Progress 100 (T 'Versión cambiada.' 'Version switched.')
        Start-Game
    }
    'rollback' {
        Remove-Item -Force -LiteralPath $lastError -ErrorAction SilentlyContinue
        if (-not (Test-Path -LiteralPath (Join-Path $previous 'MLBPowerPros2008.exe'))) { throw (T 'No hay una versión anterior.' 'There is no previous version.') }
        if (Test-Path -LiteralPath $rollbackTmp) { throw (T "Quedó $rollbackTmp de un cambio interrumpido; no se toca nada. Usa REINSTALAR." "$rollbackTmp is left over from an interrupted switch; nothing was touched. Use REINSTALL.") }
        Wait-Launcher
        New-Item -ItemType Directory -Force -Path $rollbackTmp | Out-Null
        Progress 10 (T 'Volviendo a la versión anterior...' 'Going back to the previous version...')
        # ponytail: same ceiling as swap (power cut mid-move -> REINSTALAR).
        $gameItems = @(Get-ProgramItems $game)
        $prevItems = @(Get-ProgramItems $previous)
        $m1 = @(); $m2 = @(); $m3 = @()
        try {
            foreach ($n in $gameItems) { Move-Items $game @($n) $rollbackTmp; $m1 += $n }
            foreach ($n in $prevItems) { Move-Items $previous @($n) $game; $m2 += $n }
            foreach ($n in $gameItems) { Move-Items $rollbackTmp @($n) $previous; $m3 += $n }
        } catch {
            $err = $_
            $undone = $true
            foreach ($n in $m3) { try { Move-Items $previous @($n) $rollbackTmp } catch { $undone = $false } }
            foreach ($n in $m2) { try { Move-Items $game @($n) $previous } catch { $undone = $false } }
            foreach ($n in $m1) { try { Move-Items $rollbackTmp @($n) $game } catch { $undone = $false } }
            if (-not $undone) { throw (T "$($err.Exception.Message) No se pudo deshacer el cambio: usa REINSTALAR." "$($err.Exception.Message) The switch could not be undone: use REINSTALL.") }
            Remove-Item -Recurse -Force -LiteralPath $rollbackTmp -ErrorAction SilentlyContinue
            throw $err
        }
        Remove-Item -Recurse -Force -LiteralPath $rollbackTmp
        if ($RestoreSaves) {
            Progress 60 (T 'Restaurando las partidas...' 'Restoring the saves...')
            foreach ($n in $saveItems) {
                $src = Join-Path $backup $n
                if (-not (Test-Path -LiteralPath $src)) { continue }
                $dst = Join-Path $userData $n
                if (Test-Path -LiteralPath $dst) { Remove-Item -Recurse -Force -LiteralPath $dst }
                New-Item -ItemType Directory -Force -Path $userData | Out-Null
                Copy-Item -LiteralPath $src -Destination $dst -Recurse -Force
            }
        }
        Progress 100 (T 'Versión anterior restaurada.' 'Previous version restored.')
        Start-Game
    }
}
