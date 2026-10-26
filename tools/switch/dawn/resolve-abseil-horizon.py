# SPDX-License-Identifier: GPL-3.0-or-later
"""Horizon (libnx/newlib) fixes for an Abseil tree: the one our Dawn fetches (third_party/abseil-cpp)
and the one aurora fetches (20240722.0, _deps/abseil-cpp-src). These are the four nx-mod's port notes
describe; each joins an existing platform branch, the way neighbouring platforms are handled. The
anchors exist in both Abseil versions; a file that already mentions __SWITCH__ is left as it is.

Usage (in the build image): python3 resolve-abseil-horizon.py <abseil-cpp dir>
"""
import pathlib
import sys

absl = pathlib.Path(sys.argv[1]) / "absl"


def sub(rel, old, new):
    path = absl / rel
    text = path.read_text(encoding="utf-8")
    if "__SWITCH__" in text:
        return
    assert text.count(old) == 1, (rel, old[:60], text.count(old))
    path.write_text(text.replace(old, new), encoding="utf-8")


# libnx's pthread_t is an opaque pointer: the thread's kernel handle (32-bit, unique in the process) is
# the TID, as Fuchsia's branch uses zx_thread_self(). Declared here: <switch.h> would clash with Abseil.
sub("base/internal/sysinfo.cc",
    '''#else

// Fallback implementation of `GetTID` using `pthread_self`.''',
    '''#elif defined(__SWITCH__)

extern "C" uint32_t threadGetCurHandle(void);  // libnx
pid_t GetTID() { return static_cast<pid_t>(threadGetCurHandle()); }

#else

// Fallback implementation of `GetTID` using `pthread_self`.''')

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
    '''#define ABSL_HAVE_ELF_MEM_IMAGE 1
''',
    '''#if !defined(__SWITCH__)  // ELF, but no dynamic linker or VDSO on Horizon
#define ABSL_HAVE_ELF_MEM_IMAGE 1
#endif
''')
print("abseil: horizon fixes applied")
