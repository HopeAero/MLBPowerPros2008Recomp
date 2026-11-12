# Dependencies of this proof of concept

*Versión en español: [THIRD_PARTY.md](THIRD_PARTY.md)*

This integration's own code is published under GPLv3; see `LICENSE`. The binaries and assets from the game copy are not included in the repository.

| Dependency | Version or revision used | Working location | Use |
|---|---|---|---|
| [DolRecomp](https://github.com/AH64-dll/DolRecomp) | `71c3122a92bf25a9fbe91fe7c662ad70125fba59`, GPLv3 | `local/vendor/DolRecomp` | Only `-RegenerateMap`: reuses its analysis already generated in `local/` |
| [decomp-toolkit](https://github.com/encounter/decomp-toolkit) | 1.8.4 | `local/tools` | Analysis and extraction |
| [NKit](https://github.com/Nanook/NKit) | 3.1.0 | `local/tools/nkit` | Image expansion |
| [Dolphin](https://github.com/dolphin-emu/dolphin) | Executable already installed, identified by SHA256 | `D:\Dolphin-x64` | Initial extraction |
| [ModernGekko](https://github.com/ExpansionPak/ModernGekko) | `a2e4e2b026a68d4848f20ebf16f0e0ddeb573957`, GPL-3.0-or-later | `local/vendor/ModernGekko` | Only `-RegenerateMap`: SDK signatures (`totaldb.dsy`) from its Dolphin |
| [RecompCore](https://github.com/ExpansionPak/RecompCore) | `cf8ceb81b47d6172164cd0572f57aa7624719f33`, per-file licenses | `vendor/dolphin` submodule of ModernGekko | Contains that `totaldb.dsy` |
| [WiiCompiled](https://github.com/patchzyy/Wiicompiled) | `6fa24737d3362b6cba8162671745262751f457b8`, GPLv3 | `local/vendor/Wiicompiled` | Current translator and runtime (with the patches in `tools/patches/wiicompiled-*`); its `Prepare-*` scripts assemble the Setup's tools and dependencies |
| [nod / nodtool](https://github.com/encounter/nod) | v2.0.0-alpha.10, SHA256 `1e150e33…948d`, MIT/Apache-2.0 | `local/tools/nodtool` | Reading and extracting the player's image in the Setup |
| [llvm-mingw](https://github.com/mstorsjo/llvm-mingw), [CMake](https://cmake.org), [Ninja](https://ninja-build.org) | 20260616, 4.3.3 and 1.13.2 (pinned by Wiicompiled) | Setup: `toolkit/` | Local compilation on the player's PC |
| [PowerUp+](https://github.com/ogura-connection/PowerUpPlus) / [PowerUp](https://github.com/CSho27/PowerUp) | `eb1a2c2`, GPLv3 | `local/vendor/PowerUpPlus`; working copy with `tools/patches/powerupplus-2008.patch` in `local/powerupplus`; Setup: `workspace/powerupplus` → `Game/powerupplus` | 2007 roster format (field definitions in `GSPlayer.cs` and character table), basis of the conversion to 2008 (`tools/roster`, `runtime/src/roster_fields.inc`); the patch adds importing and exporting 2008 rosters (`tools/powerupplus.ps1`). The Setup redistributes its self-contained build (web interface and server, with its CSV/JSON libraries) together with its license, the commit and the patch in `licenses/`. It does not include its 2007 base save or its `.dat` rosters, which are game content |
| [.NET SDK](https://dotnet.microsoft.com) | 10.0.401 (`dotnet-install.ps1 -Channel 10.0`), MIT | `local/tools/dotnet10`; Setup: .NET and ASP.NET Core runtime (MIT) inside `Game/powerupplus` | Building PowerUp+, which targets .NET 10; the self-contained build carries its runtime |
| MoonInning UI pack | Original art contributed by the project's author (not game content) | `assets/launcher` (processed by `tools/launcher/prepare_mooninning_art.py`) | Launcher backgrounds |
| [Blender](https://www.blender.org) | 4.2.23 LTS, GPL-2.0-or-later | `local/tools/blender-4.2.23-windows-x64` (not distributed) | Automated tests of the stadium add-on (`tools/blender/test_import.py`); the add-on uses Blender's `bpy` API |
| [OpenSKP](https://github.com/iamahsanmehmood/openskp) | 2026, MIT | Not included: `docs/en/stadiums.md` recommends it and the player installs it with `pip` | Converting SketchUp models (`.skp`) to `.glb` to import them into Blender |
| [AMD FidelityFX Super Resolution 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR) | Published algorithm (EASU + RCAS), MIT, Copyright (c) 2021 Advanced Micro Devices | Ported to WGSL in `aurora-main/lib/webgpu/fsr.cpp` (patch `wiicompiled-graphics-options`) | Launcher option SCALING (ESCALADO) = AMD FSR 1 |
| [CPython](https://www.python.org), [NumPy](https://numpy.org), [Pillow](https://python-pillow.org) | 3.12.8 (Windows embeddable package, PSF), 2.1.3 (BSD-3-Clause, with OpenBLAS and the other libraries in its wheel) and 11.0.0 (MIT-CMU); SHA256 pinned in `tools/uniform-studio/Get-EmbeddedPython.ps1` | `local/tools/uniform-python`; Setup: `workspace/uniform-studio/python` → `Game/uniform-studio/python` | Uniform Studio's bridge (`studio_bridge.py`: bake `.ppu`, import `.fsh`/`.iff`, read the figure from the player's own game). Their licenses go in the Setup's `licenses/` |
| [SDL3](https://libsdl.org), [Dear ImGui](https://github.com/ocornut/imgui), Dawn and aurora's other dependencies | Versions pinned by aurora/Wiicompiled | Setup: `workspace/Dependencies` | Game runtime and Setup window |

The license files from each download are kept. The repository does not include copies of these dependencies. The Setup release redistributes only the tools and sources marked as "Setup", with their licenses in `licenses/` and `toolkit/README-LICENSES.txt`; never game content. The previous runtime based on ModernGekko/DolRecomp, its patches and its reports are in the git tag `moderngekko-era`.
