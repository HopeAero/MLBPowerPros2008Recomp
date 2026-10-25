#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Gathers what one console test needs into local/switch/<trip>/, laid out as on the SD card, plus a
# LEEME.txt with the user's steps. Copy the folder's contents to the root of the SD card.
#
#   tools/switch/package-trip.sh trip1      the test NROs: ours (hello), nxvk's Vulkan smoke, Dawn's demo
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
trip=${1:?usage: package-trip.sh <trip1>}
out=$root/local/switch/$trip
dest=$out/switch/mlbpowerpros2008
rm -rf "$out" && mkdir -p "$dest/tests"

case $trip in
trip1)
    cp "$root/local/switch/hello/hello.nro" "$dest/tests/1-hello.nro"
    smoke=$root/local/switch/downloads/nxvk-switch-smoke-nros-26.2.2.tar.xz
    [[ -f $smoke ]] || curl -sSL -o "$smoke" \
        https://github.com/nx-mod/nxvk/releases/download/v26.2.2-nx-mod-v1/nxvk-switch-smoke-nros-26.2.2.tar.xz
    echo "3321111253f2df1c2a8214c0987d400925dc00ba29d6c13b562163ade1844e57  $smoke" | sha256sum -c - >/dev/null
    # nxvk's runner chainloads every Vulkan check beside it and writes sdmc:/nxvk_run/summary.log.
    mkdir -p "$dest/tests/vulkan"
    tar -xJf "$smoke" -C "$dest/tests/vulkan" --wildcards './nvk_*.nro'
    demo=$root/local/switch/dawn/dawn-nx-demo.nro
    if [[ -f $demo ]]; then cp "$demo" "$dest/tests/3-dawn.nro"; else echo "warning: no Dawn demo yet (Task 2)" >&2; fi
    cat > "$out/LEEME.txt" <<'EOF'
Prueba 1 en la consola: ¿arrancan nuestros .nro, funciona Vulkan y funciona Dawn?

1. Copia la carpeta "switch" de aquí a la raíz de la tarjeta SD (se junta con la carpeta "switch" que ya tienes).
   Quedan en sdmc:/switch/mlbpowerpros2008/tests/: 1-hello, 3-dawn y la carpeta vulkan/ (con nvk_runner).

2. Abre 1-hello y 3-dawn desde el menú homebrew DOS veces cada uno, y nvk_runner (dentro de vulkan) una vez, "sobre un juego":
   a) desde el álbum (como siempre);
   b) "sobre un juego": mantén R mientras abres cualquier juego instalado; sale el menú homebrew con toda la memoria.

3. Qué hace cada uno:
   - 1-hello: muestra texto (firmware, modo, memoria) y lo guarda. Sal con +.
   - vulkan/nvk_runner: corre solo las pruebas del driver Vulkan (NVK), una tras otra; tarda unos minutos.
     Al final muestra el total y espera +. Si se cierra a mitad, vuelve a abrirlo: sigue donde iba.
   - 3-dawn: la demo de Dawn: hace 10 pruebas y presenta 180 cuadros. Anota cuántas dice que pasan. Sal con + o HOME.

4. Devuélveme:
   - el archivo sdmc:/switch/mlbpowerpros2008/logs/hello.log;
   - el archivo sdmc:/nxvk_run/summary.log (el resultado de Vulkan);
   - lo que viste en 3-dawn (una foto de la pantalla sirve), en cada modo;
   - si alguno se cerró o mostró un error de Atmosphère, la foto del error y el archivo más nuevo de sdmc:/atmosphere/crash_reports/.
EOF
    ;;
*) echo "unknown trip: $trip" >&2; exit 2 ;;
esac
echo "ready: $out"
find "$out" -type f | sed "s|$out/||"
