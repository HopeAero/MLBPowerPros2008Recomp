# SPDX-License-Identifier: GPL-3.0-or-later
# The deko3d renderer's libraries for the Switch game build (include() this file, or find_package(Deko)
# with Deko_DIR set to tools/switch/deko):
#   mlb::deko3d  deko3d 0.5.0 with SwitchWaker's fixes (tools/switch/deko/build-deko3d.sh); the release
#                library, or the checking one (upstream's libdeko3dd) with MLB_DEKO3D_DEBUG_LIB=ON
#   mlb::uamlib  uam 1.1.0 + SwitchWaker's patches, GLSL -> DKSH at run time (build-uam.sh); uam_api.h
#   dawn::tint   Tint's WGSL reader / inspector / GLSL writer with the deko3d options
#                (tools/switch/dawn/DawnConfig.cmake; objects inside libwebgpu_dawn.a; C++20)
#   mlb::deko    all three
# Variables: MLB_DEKO_DIR (the builds; default <repo>/local/switch/work/deko, or $MLB_SWITCH_WORK/deko),
# MLB_DEKO3D_LIBRARY, MLB_DEKO3D_INCLUDE_DIR, MLB_UAM_LIBRARY, MLB_UAM_INCLUDE_DIR, Deko_FOUND.
# deko3d is linked by full path: nothing else on the link line may name -ldeko3d (devkitPro's unpatched
# copy in libnx/lib), or the first archive wins.
get_filename_component(_mlb_repo "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
if(NOT MLB_DEKO_DIR)
    if(DEFINED ENV{MLB_SWITCH_WORK})
        set(MLB_DEKO_DIR "$ENV{MLB_SWITCH_WORK}/deko")
    else()
        set(MLB_DEKO_DIR "${_mlb_repo}/local/switch/work/deko")
    endif()
endif()
option(MLB_DEKO3D_DEBUG_LIB "deko3d: link the checking library (libdeko3dd's -DDEBUG build)" OFF)
if(MLB_DEKO3D_DEBUG_LIB)
    set(MLB_DEKO3D_LIBRARY "${MLB_DEKO_DIR}/deko3d/build/deko3d/libdeko3d_debug.a")
else()
    set(MLB_DEKO3D_LIBRARY "${MLB_DEKO_DIR}/deko3d/build/deko3d/libdeko3d_release.a")
endif()
set(MLB_DEKO3D_INCLUDE_DIR "${MLB_DEKO_DIR}/deko3d/src/include")
set(MLB_UAM_LIBRARY "${MLB_DEKO_DIR}/uam/build/libuamlib_private.a")  # only uam:: global (build-uam.sh)
set(MLB_UAM_INCLUDE_DIR "${MLB_DEKO_DIR}/uam/src")
foreach(_f "${MLB_DEKO3D_LIBRARY}" "${MLB_UAM_LIBRARY}")
    if(NOT EXISTS "${_f}")
        message(FATAL_ERROR "'${_f}' is not built: run tools/switch/deko/build-deko3d.sh and build-uam.sh")
    endif()
endforeach()

if(NOT TARGET dawn::tint)
    include("${_mlb_repo}/tools/switch/dawn/DawnConfig.cmake")  # needs MKW_SWITCH_DAWN_DIR, as aurora's
endif()
if(NOT TARGET mlb::deko3d)
    add_library(mlb::deko3d STATIC IMPORTED GLOBAL)
    set_target_properties(mlb::deko3d PROPERTIES
        IMPORTED_LOCATION "${MLB_DEKO3D_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${MLB_DEKO3D_INCLUDE_DIR}")
endif()
if(NOT TARGET mlb::uamlib)
    add_library(mlb::uamlib STATIC IMPORTED GLOBAL)
    set_target_properties(mlb::uamlib PROPERTIES
        IMPORTED_LOCATION "${MLB_UAM_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${MLB_UAM_INCLUDE_DIR}"
        IMPORTED_LINK_INTERFACE_LANGUAGES CXX)
endif()
if(NOT TARGET mlb::deko)
    add_library(mlb::deko INTERFACE IMPORTED GLOBAL)
    set_target_properties(mlb::deko PROPERTIES
        INTERFACE_LINK_LIBRARIES "mlb::deko3d;mlb::uamlib;dawn::tint")
endif()
set(Deko_FOUND TRUE)
