# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the Setup payload in one step from committed sources only, so a release never picks up
# hand edits in local\wiicompiled-mlb\src or a stale aurora prebuilt:
#   1. git clone of this repository at -Ref into local\rc (uncommitted changes are not in it);
#   2. a fresh Wiicompiled checkout at the pinned commit with every patch applied and rebound
#      (tools\wiicompiled-translate.ps1 -Fresh -PrepareOnly; no game files involved);
#   3. Wiicompiled's Prepare-PortableTools / Prepare-Dependencies / Prepare-NativePrebuilt in it;
#   4. the clone's tools\setup\Make-Payload.ps1 writing to this repository's local\setup-payload.
# Pinned inputs that are not in git (the Wiicompiled and PowerUp+ clones, the .NET 10 SDK, nodtool,
# package caches) come from this repository's local\vendor, local\tools and local\cache through
# junctions; every one is checked out or verified by commit or hash. Downloaded archives are seeded
# from the working tree's Launcher\artifacts\downloads when present (the Prepare scripts check their
# SHA-256).
#   5. with -Zip, tools\update_manifest.py cuts the payload into per-piece zips and writes update-manifest.txt
#      (plus the Linux/Steam Deck kit lines when local\release exists: it must then hold both kits of this
#      version, from tools/linux/make-kits.sh, or the step fails) into local\setup-payload\release.
param([string]$Ref = 'HEAD', [switch]$Zip, [string]$Discussion, [string]$DiscussionEn)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$clone = Join-Path $root 'local\rc'
if ($Zip -and -not $Discussion) { throw '-Discussion <url> is required with -Zip (release notes link).' }
function Invoke-Checked([string]$label) { if ($LASTEXITCODE -ne 0) { throw "$label failed ($LASTEXITCODE)" } }

if (git -C $root status --porcelain) { Write-Warning 'Uncommitted changes are not part of the release.' }
# rmdir /s removes junctions without following them (Remove-Item in PowerShell 5.1 may not).
if (Test-Path $clone) { cmd /c rmdir /s /q "$clone"; if (Test-Path $clone) { throw "Could not remove $clone" } }
git clone -q --no-hardlinks $root $clone; Invoke-Checked 'git clone'
git -C $clone checkout -q $Ref; Invoke-Checked "checkout $Ref"
$commit = git -C $clone rev-parse --short HEAD
New-Item -ItemType Directory -Force "$clone\local" | Out-Null
foreach ($shared in 'vendor', 'tools', 'cache') {
    cmd /c mklink /J "$clone\local\$shared" "$root\local\$shared" | Out-Null; Invoke-Checked "junction $shared"
}

Write-Host "Wiicompiled tree ($commit)..."
& "$clone\tools\wiicompiled-translate.ps1" -Fresh -PrepareOnly
$src = "$clone\local\wiicompiled-mlb\src"
$launcher = "$src\Launcher"
$seed = "$root\local\wiicompiled-mlb\src\Launcher\artifacts\downloads"
if (Test-Path $seed) {
    robocopy $seed "$launcher\artifacts\downloads" /E /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw 'seeding downloads failed' }
}
Write-Host 'Portable tools, dependencies, aurora prebuilt...'
& "$launcher\Prepare-PortableTools.ps1"
& "$launcher\Prepare-Dependencies.ps1"
& "$launcher\Prepare-NativePrebuilt.ps1"

& "$clone\tools\setup\Make-Payload.ps1" -Source $src -Output "$root\local\setup-payload\MLB Power Pros 2008 Setup" -Zip:$Zip
if ($Zip) {
    # Python 3: $env:PYTHON (a full path) when set, else `python` on PATH.
    $python = if ($env:PYTHON) { $env:PYTHON } else { (Get-Command python -ErrorAction Stop).Source }
    $version = (Get-Content "$clone\VERSION" -TotalCount 1).Trim()
    $release = "$root\local\setup-payload\release"
    if (Test-Path $release) { Remove-Item -Recurse -Force $release }
    $um = @('-I', "$clone\tools\update_manifest.py", 'release', "$root\local\setup-payload\MLB Power Pros 2008 Setup",
            $release, $version, $Discussion)
    if ($DiscussionEn) { $um += $DiscussionEn }
    if (Test-Path "$root\local\release") {
        $um += '--kits', "$root\local\release", '--containerfile', "$clone\tools\linux\Containerfile"
    }
    & $python @um
    Invoke-Checked 'update_manifest release'
    Write-Host "Pieces and update-manifest.txt in $release"
}
Write-Host "Release payload built from $commit."
