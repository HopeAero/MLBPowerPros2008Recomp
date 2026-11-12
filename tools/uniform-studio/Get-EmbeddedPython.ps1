# SPDX-License-Identifier: GPL-3.0-or-later
# Puts the Python that Uniform Studio's bridge needs (studio_bridge.py: numpy + Pillow) into <Output>:
# the official Windows embeddable CPython 3.12 with the numpy and Pillow wheels unpacked next to it
# (the embeddable python312._pth already lists its own folder, so no pip and no site-packages).
# Downloads are cached in local\tools\uniform-python and checked against pinned SHA-256 sums
# (the wheels' sums are PyPI's; python.org signs the zip with sigstore only, so its sum is pinned here).
param([Parameter(Mandatory)][string]$Output)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$cache = Join-Path $root 'local\tools\uniform-python'
$files = @(
    @{ Name = 'python-3.12.8-embed-amd64.zip'; Sha = '8d3f33be9eb810f23c102f08475af2854e50484b8e4e06275e937be61ce3d2fb'
       Url = 'https://www.python.org/ftp/python/3.12.8/python-3.12.8-embed-amd64.zip' },
    @{ Name = 'numpy-2.1.3-cp312-cp312-win_amd64.whl'; Sha = '0d30c543f02e84e92c4b1f415b7c6b5326cbe45ee7882b6b77db7195fb971e3a'
       Url = 'https://files.pythonhosted.org/packages/a6/84/fa11dad3404b7634aaab50733581ce11e5350383311ea7a7010f464c0170/numpy-2.1.3-cp312-cp312-win_amd64.whl' },
    @{ Name = 'pillow-11.0.0-cp312-cp312-win_amd64.whl'; Sha = '8ec4a89295cd6cd4d1058a5e6aec6bf51e0eaaf9714774e1bfac7cfc9051db47'
       Url = 'https://files.pythonhosted.org/packages/57/97/73f756c338c1d86bb802ee88c3cab015ad7ce4b838f8a24f16b676b1ac7c/pillow-11.0.0-cp312-cp312-win_amd64.whl' }
)
New-Item -ItemType Directory -Force $cache | Out-Null
if (Test-Path $Output) { Remove-Item -Recurse -Force $Output }
New-Item -ItemType Directory -Force $Output | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach ($f in $files) {
    $path = Join-Path $cache $f.Name
    if (-not (Test-Path $path) -or (Get-FileHash $path).Hash.ToLower() -ne $f.Sha) {
        Invoke-WebRequest -UseBasicParsing -OutFile $path $f.Url
        if ((Get-FileHash $path).Hash.ToLower() -ne $f.Sha) { throw "$($f.Name): SHA-256 mismatch" }
    }
    [IO.Compression.ZipFile]::ExtractToDirectory($path, $Output)  # a wheel is a zip too
}
# What the bridge does not need: tests, headers, type stubs, the f2py and pyinstaller hooks.
foreach ($unused in 'numpy\_core\tests', 'numpy\tests', 'numpy\f2py', 'numpy\_pyinstaller', 'numpy\_core\include',
                    'numpy\typing\tests', 'numpy\random\tests', 'numpy\linalg\tests', 'numpy\fft\tests',
                    'numpy\lib\tests', 'numpy\ma\tests', 'numpy\polynomial\tests', 'numpy\testing\tests') {
    $p = Join-Path $Output $unused
    if (Test-Path $p) { Remove-Item -Recurse -Force $p }
}
# (no quotes inside -c: Windows PowerShell 5.1 strips them when calling a native program)
& (Join-Path $Output 'python.exe') -I -c 'import numpy, PIL.Image; print(numpy.__version__, PIL.__version__)'
if ($LASTEXITCODE -ne 0) { throw 'embedded Python check failed' }
