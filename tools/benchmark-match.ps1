param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\local\wiicompiled-mlb\build'),
    [ValidateSet(0, 1, 2, 3, 4)][int[]]$Scale = @(4),  # 0: window size ("Según la ventana")
    [ValidateSet('on', 'off')][string[]]$Force169 = @('on'),
    [ValidateSet('auto', 'd3d12', 'vulkan', 'd3d11')][string]$GraphicsApi = 'auto',
    [ValidateRange(5, 600)][int]$Seconds = 30,
    [ValidateSet(0, 120, 180)][int]$Interpolation = 0,  # frame_interpolation_fps
    [switch]$Interactive
)
$ErrorActionPreference = 'Stop'
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$exe = Join-Path $build 'WiiCompiled.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw "No se encuentra $exe" }
$folder = Join-Path $build ('UserData\Benchmarks\monitor-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $folder -Force | Out-Null
foreach ($resolution in $Scale) {
    foreach ($aspect in $Force169) {
        $label = "x$resolution-169-$aspect-$GraphicsApi" + $(if ($Interpolation) { "-interp$Interpolation" } else { "" })
        $info = New-Object System.Diagnostics.ProcessStartInfo
        $info.FileName = $exe
        $info.WorkingDirectory = $build
        $info.UseShellExecute = $false
        $info.RedirectStandardOutput = $true
        $info.RedirectStandardError = $true
        foreach ($key in @('WIICOMPILED_PAD_SCRIPT','WIICOMPILED_KPAD_SCRIPT','WIICOMPILED_BENCH','WIICOMPILED_DEBUG_MEM1')) {
            $info.EnvironmentVariables.Remove($key)
        }
        $info.EnvironmentVariables['WIICOMPILED_MATCH_BENCH'] = '1'
        $info.EnvironmentVariables['WIICOMPILED_MATCH_SCALE'] = "$resolution"
        $info.EnvironmentVariables['WIICOMPILED_MATCH_16_9'] = $(if ($aspect -eq 'on') { '1' } else { '0' })
        $info.EnvironmentVariables['WIICOMPILED_MATCH_API'] = $GraphicsApi
        $info.EnvironmentVariables['WIICOMPILED_MATCH_INTERP'] = "$Interpolation"
        $info.EnvironmentVariables['WIICOMPILED_MATCH_SECONDS'] = $(if ($Interactive) { '0' } else { "$Seconds" })
        $toolchain = Join-Path $PSScriptRoot '..\local\tools\llvm-mingw-20260616-ucrt-x86_64\bin'
        if (Test-Path -LiteralPath $toolchain) { $info.EnvironmentVariables['PATH'] = (Resolve-Path -LiteralPath $toolchain).Path + ';' + $info.EnvironmentVariables['PATH'] }
        $game = New-Object System.Diagnostics.Process
        $game.StartInfo = $info
        Write-Host "Yankees - Colorado: $label"
        [void]$game.Start()
        $stdout = $game.StandardOutput.ReadToEndAsync()
        $stderr = $game.StandardError.ReadToEndAsync()
        $started = Get-Date
        $rows = New-Object System.Collections.Generic.List[object]
        $monitorAvailable = $true
        try {
            while (-not $game.WaitForExit(1000)) {
                if (-not $Interactive -and ((Get-Date) - $started).TotalSeconds -gt (150 + $Seconds)) {
                    $game.Kill(); $game.WaitForExit()
                    throw 'La prueba no llego al campo dentro del tiempo limite.'
                }
                $engines = @()
                if ($monitorAvailable) {
                    try {
                        $engines = @(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine -Filter "Name LIKE 'pid_$($game.Id)_%'" -ErrorAction Stop)
                    } catch {
                        $monitorAvailable = $false
                        Write-Warning 'Windows no ofrece contadores GPU; se conservaran las mediciones internas del juego.'
                    }
                }
                $maxFor = { param($kind)
                    $items = @($engines | Where-Object { $_.Name -like "*engtype_$kind*" })
                    if ($items.Count) { ($items | Measure-Object UtilizationPercentage -Maximum).Maximum } else { $null }
                }
                $rows.Add([pscustomobject]@{
                    utc_unix_ms = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
                    process_id = $game.Id
                    scale = $resolution
                    force_16_9 = $aspect
                    graphics_api_requested = $GraphicsApi
                    gpu_3d_percent = (& $maxFor '3D')
                    gpu_copy_percent = (& $maxFor 'Copy')
                    gpu_compute_percent = (& $maxFor 'Compute')
                    private_memory_mb = [math]::Round($game.PrivateMemorySize64 / 1MB, 1)
                })
            }
            $game.WaitForExit()
            if ($game.ExitCode -ne 0) { throw "El juego termino con codigo $($game.ExitCode)." }
        } finally {
            if (-not $game.HasExited) { $game.Kill(); $game.WaitForExit() }
            [IO.File]::WriteAllText((Join-Path $folder "$label.stdout.log"), $stdout.GetAwaiter().GetResult())
            [IO.File]::WriteAllText((Join-Path $folder "$label.stderr.log"), $stderr.GetAwaiter().GetResult())
            if ($rows.Count) { $rows | Export-Csv -LiteralPath (Join-Path $folder "$label.gpu.csv") -NoTypeInformation -Encoding UTF8 }
            $game.Dispose()
        }
    }
}
Write-Host "Informes del juego: $(Join-Path $build 'UserData\Benchmarks')"
Write-Host "Monitoreo de Windows: $folder"
