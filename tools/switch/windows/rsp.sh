#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# rsp.sh <tool> <args...>: runs a native Windows tool (devkitA64's ar or g++) with its arguments in a
# response file. MSYS2 starts native programs through a 32K command line, which a big archive or link
# overflows; absolute MSYS paths become Windows ones, since the tool reads the file itself.
tool=$1; shift
rsp=.rsp-$$.txt   # relative: the build directory, and MSYS leaves "@file" untouched
for arg; do
    case $arg in
        /*) arg=$(cygpath -m "$arg") ;;
        -L/*|-I/*) arg=${arg%"${arg#??}"}$(cygpath -m "${arg#??}") ;;
        -*=/*) arg=${arg%%=*}=$(cygpath -m "${arg#*=}") ;;   # -specs=/opt/...
    esac
    printf '"%s"\n' "$arg"
done > "$rsp"
"$tool" @"$rsp"
rc=$?
rm -f "$rsp"
exit $rc
