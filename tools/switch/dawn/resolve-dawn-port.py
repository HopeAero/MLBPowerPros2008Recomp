# SPDX-License-Identifier: GPL-3.0-or-later
"""Finishes carrying dawn-nx's Horizon port onto our older Dawn (13abc3bc, June 2026) after
`git apply --reject`: the three places where the port's base (Dawn of September 2026) differs.

- dawn.json: our s type enum ends at 81, so the Horizon surface's chained struct takes 82.
- VulkanFunctions.h: the LoadGlobalProcs(PFN_vkGetInstanceProcAddr) overload NVK's static ICD uses.
- SystemHandle lives in src/dawn/utils/ here (src/dawn/common/ upstream): the HORIZON branch.

Usage (in the build image, from make-dawn-horizon-patch.sh): python3 resolve-dawn-port.py <dawn src>
"""
import pathlib
import sys

src = pathlib.Path(sys.argv[1])


def sub(rel, old, new):
    path = src / rel
    text = path.read_text(encoding="utf-8")
    if new in text:
        return
    assert text.count(old) == 1, (rel, old[:60], text.count(old))
    path.write_text(text.replace(old, new), encoding="utf-8")


sub("src/dawn/dawn.json",
    '''            {"value": 81, "name": "adapter properties drm", "tags": ["dawn", "native"]}
        ]''',
    '''            {"value": 81, "name": "adapter properties drm", "tags": ["dawn", "native"]},
            {"value": 82, "name": "surface source vi NN", "tags": ["dawn", "native"]}
        ]''')

sub("src/dawn/native/vulkan/VulkanFunctions.h",
    '''    MaybeError LoadGlobalProcs(const DynamicLib& vulkanLib);
''',
    '''    MaybeError LoadGlobalProcs(const DynamicLib& vulkanLib);
    // Horizon has no dlopen (see DynamicLib.cpp); NVK's static ICD entry point
    // (vk_icdGetInstanceProcAddr) is loaded directly, skipping DynamicLib entirely.
    MaybeError LoadGlobalProcs(PFN_vkGetInstanceProcAddr getInstanceProcAddr);
''')

sub("src/dawn/utils/SystemHandle.h",
    '''#elif DAWN_PLATFORM_IS(FUCHSIA)
    using Handle = uint32_t;''',
    '''#elif DAWN_PLATFORM_IS(FUCHSIA) || DAWN_PLATFORM_IS(HORIZON)
    // libnx kernel object Handle is a u32, same shape as Fuchsia's zx_handle_t.
    using Handle = uint32_t;''')

sub("src/dawn/utils/SystemHandle.cpp",
    '''#elif DAWN_PLATFORM_IS(FUCHSIA)
#include <zircon/syscalls.h>
#elif DAWN_PLATFORM_IS(POSIX)''',
    '''#elif DAWN_PLATFORM_IS(FUCHSIA)
#include <zircon/syscalls.h>
#elif DAWN_PLATFORM_IS(HORIZON)
#include <switch.h>
#elif DAWN_PLATFORM_IS(POSIX)''')

sub("src/dawn/utils/SystemHandle.cpp",
    '''inline void CloseHandle(zx_handle_t handle) {
    auto status = zx_handle_close(handle);
    DAWN_CHECK(status == ZX_OK);
}

#elif DAWN_PLATFORM_IS(POSIX)''',
    '''inline void CloseHandle(zx_handle_t handle) {
    auto status = zx_handle_close(handle);
    DAWN_CHECK(status == ZX_OK);
}

#elif DAWN_PLATFORM_IS(HORIZON)

// Horizon has no blanket handle duplication; Duplicate() is only reached by cross-API or
// cross-process sharing, which a single-backend in-process game never does, so it fails fast.
constexpr inline ::Handle kInvalidHandle = INVALID_HANDLE;

inline bool IsHandleValid(::Handle handle) {
    return handle != INVALID_HANDLE;
}

inline ::Handle DuplicateHandle(::Handle handle) {
    (void)handle;
    DAWN_UNREACHABLE();
    return kInvalidHandle;
}

inline void CloseHandle(::Handle handle) {
    Result rc = svcCloseHandle(handle);
    DAWN_CHECK(R_SUCCEEDED(rc));
}

#elif DAWN_PLATFORM_IS(POSIX)''')

for rej in list(src.rglob("*.rej")) + list(src.rglob("*.orig")):
    rej.unlink()
print("resolved")
