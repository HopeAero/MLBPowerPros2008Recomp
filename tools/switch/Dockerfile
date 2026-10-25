# SPDX-License-Identifier: GPL-3.0-or-later
# Build image for the MLB Power Pros 2008 Switch NRO (tools/switch/docker-build.sh builds and uses it):
# devkitPro's devkitA64 (GCC) + libnx, the NVK Vulkan driver for Horizon (nxvk, prebuilt), devkitPro's
# SDL3 for Switch (built from source: there is no package), and the host tools the translation needs.
FROM devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl git xz-utils python3 python3-pil wget ninja-build \
    && wget -q https://packages.microsoft.com/config/debian/12/packages-microsoft-prod.deb -O /tmp/ms.deb \
    && dpkg -i /tmp/ms.deb && rm /tmp/ms.deb \
    && apt-get update && apt-get install -y --no-install-recommends dotnet-sdk-8.0 \
    && rm -rf /var/lib/apt/lists/*

# nxvk 26.2.2 (nx-mod release v26.2.2-nx-mod-v1): libnvk.a, libnvk_support.a and nxvk.pc into the
# Switch portlibs, where Dawn's Vulkan backend links them statically.
ARG NXVK_URL=https://github.com/nx-mod/nxvk/releases/download/v26.2.2-nx-mod-v1/nxvk-switch-portlib-26.2.2.tar.xz
ARG NXVK_SHA256=9bbf12732d81891a2f03633b752d8f4f106b268861a8982fef956c1945a8e4e2
RUN curl -sSL -o /tmp/nxvk.tar.xz "$NXVK_URL" \
    && echo "$NXVK_SHA256  /tmp/nxvk.tar.xz" | sha256sum -c - \
    && tar -xJf /tmp/nxvk.tar.xz -C /opt/devkitpro/portlibs/switch && rm /tmp/nxvk.tar.xz

# SDL3 for Switch: devkitPro/SDL branch switch-sdl-3.4, pinned.
ARG SDL3_COMMIT=c329016c0e338ab4397ad930f83330c9fe058348
RUN curl -sSL -o /tmp/sdl.tar.gz "https://github.com/devkitPro/SDL/archive/${SDL3_COMMIT}.tar.gz" \
    && mkdir /tmp/sdl && tar -xzf /tmp/sdl.tar.gz -C /tmp/sdl --strip-components=1 \
    && cmake -S /tmp/sdl -B /tmp/sdl/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake \
        -DCMAKE_INSTALL_PREFIX=/opt/devkitpro/portlibs/switch \
        -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
    && cmake --build /tmp/sdl/build && cmake --install /tmp/sdl/build \
    && rm -rf /tmp/sdl /tmp/sdl.tar.gz

# Horizon has no dynamic loader; Dawn's CMake still links -ldl for its (unused here) dlopen path, as
# nxvk's own build environment does, so an empty libdl.a satisfies it.
RUN /opt/devkitpro/devkitA64/bin/aarch64-none-elf-ar rcs /opt/devkitpro/portlibs/switch/lib/libdl.a

WORKDIR /work
