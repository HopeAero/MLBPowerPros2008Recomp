#!/bin/bash
# MLB Life from the title screen to the career's menu, with the PowerUp2025 mod's save 1 (back it up first:
# a probe that saves overwrites it). Extra args go to live.py run. Waits on the game, not on fixed times.
cd "$(dirname "$0")"
P=${PYTHON:-/c/Users/david/.pyenv/pyenv-win/versions/3.12.0/python.exe}
L="$P live.py"
$L stop >/dev/null 2>&1; taskkill //F //IM WiiCompiled.exe >/dev/null 2>&1
(MODSLIST=${MODSLIST:-'["PowerUp2025"]'} $L run "$@" >/dev/null 2>&1 &)
sleep 3
$L do until:THP:120 wait:3 START wait:2 START wait:2 START wait:2 START wait:2 START wait:8 \
    hold:down:250 wait:1.5 hold:A:250 untilpeek:8104B608:2:2329:60 wait:6 hold:START:250 wait:4 hold:A:250 wait:12 \
    hold:A:250 wait:6 shot:life
