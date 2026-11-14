# SPDX-License-Identifier: GPL-3.0-or-later
# The first-level names inside <Install>\Game that belong to the program (everything else there is the
# player's: UserData, mods, portable.txt). LocalBuild.ps1 installs them and Update.ps1 swaps them;
# both dot-source this file. Make-Payload.ps1 copies it to workspace\ProgramItems.ps1.
function Get-ProgramItems([string]$Directory) {
    $items = @(
        'MLBPowerPros2008.exe',
        'dxcompiler.dll', 'dxil.dll', 'd3dcompiler_47.dll', 'libc++.dll', 'libpng16.dll', 'libunwind.dll', 'libz.dll',
        'SDL3.dll', 'sqlite3.dll', 'webgpu_dawn.dll', 'z.dll', 'noshaders.zip', 'dsp_coef.bin', 'initial_pipeline_cache.db',
        'launcher', 'powerupplus', 'uniform-studio', 'config', 'wii_bootstrap',
        'TextureStudio.exe', 'SeasonStudio.exe', 'CallnameStudio.exe', 'UniformStudio.exe',
        'version.txt'  # the version of this program, so swap/rollback carry it along
    )
    # With a directory: only the items that exist there.
    if ($Directory) { $items | Where-Object { Test-Path -LiteralPath (Join-Path $Directory $_) } } else { $items }
}

# After a full install (Setup, REINSTALAR) Game holds a complete program, so what an interrupted update
# or rollback left behind is obsolete; a leftover Rollback.tmp would block every later swap and rollback.
# LocalBuild.ps1 calls it. Previous (the program GO BACK TO returns to) stays, unless a rollback was
# interrupted: then it may be half of a program and goes too (only program files, never saves).
function Clear-UpdateLeftovers([string]$Install) {
    $names = @('Staging')
    if (Test-Path -LiteralPath (Join-Path $Install 'Rollback.tmp')) { $names += 'Rollback.tmp', 'Previous' }
    foreach ($name in $names) {
        $path = Join-Path $Install $name
        if (Test-Path -LiteralPath $path) { Remove-Item -Recurse -Force -LiteralPath $path }
    }
}
