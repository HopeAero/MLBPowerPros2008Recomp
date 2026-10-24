#!/bin/bash
# MLB Life manager role talk, on the test save (mod ZZTwoWayTest, saved at the free night of 3/1/2025):
# loads it, goes FREE > CHAT > STAFF > MLB Manager > "About my two-way role." and picks answer $1
# (0 two-way, 1 batter only, 2 pitcher only). Restores the save from local/tmp/life-test-night-0301 first.
# Waits on the game's log where it can; never presses past a step it cannot see.
cd "$(dirname "$0")"
P=${PYTHON:-/c/Users/david/.pyenv/pyenv-win/versions/3.12.0/python.exe}
L="$P live.py"
B=/d/MLBPowerPros2008Recomp/local/wiicompiled-mlb/build/UserData/ModSaves/ZZTwoWayTest
$L stop >/dev/null 2>&1; taskkill //F //IM WiiCompiled.exe >/dev/null 2>&1; sleep 1
cp -p /d/MLBPowerPros2008Recomp/local/tmp/life-test-night-0301/* "$B"/
(MODSLIST='["ZZTwoWayTest"]' $L run "${@:2}" >/dev/null 2>&1 &)
sleep 3
$L do until:THP:120 wait:3 START wait:2 START wait:2 START wait:2 START wait:2 START wait:8 \
    hold:down:250 wait:1.5 hold:A:250 wait:16 hold:START:250 wait:4 hold:A:250 wait:12 hold:A:250 \
    until:"MLB Life menu":60 wait:3 >/dev/null || exit 1
# FREE > CHAT > STAFF tab > MLB Manager (first row)
$L do hold:A:200 wait:1.5 hold:right:200 wait:0.8 hold:right:200 wait:0.8 hold:right:200 wait:1 \
    hold:A:200 wait:3 hold:R:200 wait:1.5 mark spam:A:"manager talk":40 >/dev/null || exit 1
# Greeting, then the manager's list: the role row is the second one.
$L do wait:2 hold:A:200 wait:2 hold:down:200 wait:1 shot:role_list mark hold:A:200 wait:2 shot:role_question hold:A:200 wait:2 >/dev/null
moves=""; for i in $(seq 1 "$1"); do moves="$moves hold:down:200 wait:1"; done
$L do shot:role_default $moves shot:role_choice mark hold:A:200 until:"MLB Life role":10 wait:2 shot:role_answer
