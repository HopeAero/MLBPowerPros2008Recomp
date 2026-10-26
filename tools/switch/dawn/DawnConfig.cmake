# SPDX-License-Identifier: GPL-3.0-or-later
# Dawn for Horizon as the Switch game build's aurora finds it (AURORA_DAWN_PROVIDER=system, Dawn_DIR set
# to this directory by tools/switch/build-nro.sh): the monolithic libwebgpu_dawn.a that
# tools/switch/dawn/build-dawn.sh builds in MKW_SWITCH_DAWN_DIR. Executables add NVK's link recipe and
# newlib gap-fill (dawn-nx's NvkLink.cmake), see MKW_SWITCH_DAWN_DIR in the runtime's CMake.
set(_mkw_dawn_dir "${MKW_SWITCH_DAWN_DIR}")
if(NOT EXISTS "${_mkw_dawn_dir}/build/src/dawn/native/libwebgpu_dawn.a")
    message(FATAL_ERROR "Dawn for Horizon is not built in '${_mkw_dawn_dir}': run tools/switch/dawn/build-dawn.sh")
endif()
if(NOT TARGET dawn::webgpu_dawn)
    add_library(dawn::webgpu_dawn STATIC IMPORTED)
    set_target_properties(dawn::webgpu_dawn PROPERTIES
        IMPORTED_LOCATION "${_mkw_dawn_dir}/build/src/dawn/native/libwebgpu_dawn.a"
        INTERFACE_INCLUDE_DIRECTORIES "${_mkw_dawn_dir}/src/include;${_mkw_dawn_dir}/build/gen/include")
endif()
set(Dawn_FOUND TRUE)
