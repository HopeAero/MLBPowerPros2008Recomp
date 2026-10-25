# SPDX-License-Identifier: GPL-3.0-or-later
"""Horizon (libnx/newlib) fixes for the Abseil our Dawn fetches (third_party/abseil-cpp), the four
nx-mod's port notes describe; dawn-nx's newer Abseil no longer needs them in its own tree. Each one
joins an existing platform branch, the way the neighbouring platforms are handled. Idempotent.

Usage (in the build image, from build-dawn.sh): python3 resolve-abseil-horizon.py <abseil-cpp dir>
"""
import pathlib
import sys

absl = pathlib.Path(sys.argv[1]) / "absl"


def sub(rel, old, new):
    path = absl / rel
    text = path.read_text(encoding="utf-8")
    if new in text:
        return
    assert text.count(old) == 1, (rel, old[:60], text.count(old))
    path.write_text(text.replace(old, new), encoding="utf-8")


# libnx's pthread_t is an opaque pointer: the thread's kernel handle (32-bit, unique in the process)
# is the TID, as Fuchsia's branch uses zx_thread_self().
sub("base/internal/sysinfo.cc",
    '''#if defined(__Fuchsia__)
#include <zircon/process.h>''',
    '''#if defined(__Fuchsia__)
#include <zircon/process.h>
#elif defined(__SWITCH__)
#include <switch.h>''')
sub("base/internal/sysinfo.cc",
    '''  return static_cast<pid_t>(zx_thread_self());
}

#else''',
    '''  return static_cast<pid_t>(zx_thread_self());
}

#elif defined(__SWITCH__)

pid_t GetTID() { return static_cast<pid_t>(threadGetCurHandle()); }

#else''')

# libnx has no POSIX signal delivery, like the pthreads already listed.
sub("base/internal/thread_identity.cc",
    '''#if defined(__wasi__) || defined(__EMSCRIPTEN__) || defined(__MINGW32__) || \\
    defined(__hexagon__)''',
    '''#if defined(__wasi__) || defined(__EMSCRIPTEN__) || defined(__MINGW32__) || \\
    defined(__hexagon__) || defined(__SWITCH__)''')

# newlib exposes _timezone and tzname, which is what this branch uses.
sub("time/internal/cctz/src/time_zone_libc.cc",
    '''#elif defined(__native_client__) || defined(__myriad2__) || \\
    defined(__EMSCRIPTEN__)''',
    '''#elif defined(__native_client__) || defined(__myriad2__) || \\
    defined(__EMSCRIPTEN__) || defined(__SWITCH__)''')

# ELF output, but no dynamic linker or VDSO on Horizon (no <link.h>).
sub("debugging/internal/elf_mem_image.h",
    '''    !defined(__sun) && !defined(__VXWORKS__) && !defined(__hexagon__) && \\
    !defined(__XTENSA__)''',
    '''    !defined(__sun) && !defined(__VXWORKS__) && !defined(__hexagon__) && \\
    !defined(__XTENSA__) && !defined(__SWITCH__)''')
print("abseil: horizon fixes applied")
