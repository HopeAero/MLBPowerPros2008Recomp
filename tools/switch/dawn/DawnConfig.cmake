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
# Tint's C++ API (WGSL reader, inspector, GLSL writer with the tint-glsl-*.patch options) for the deko3d
# renderer's shader translation: its objects are inside libwebgpu_dawn.a (libtint is bundled into the
# monolithic library); this target adds Tint's source-tree headers and the TINT_BUILD_* switches the
# library was compiled with (its headers depend on them). Users compile as C++20, Dawn's standard.
if(NOT TARGET dawn::tint)
    load_cache("${_mkw_dawn_dir}/build" READ_WITH_PREFIX _mkw_dawn_
        TINT_BUILD_GLSL_VALIDATOR TINT_BUILD_GLSL_WRITER TINT_BUILD_HLSL_WRITER TINT_BUILD_IR_BINARY
        TINT_BUILD_MSL_WRITER TINT_BUILD_NULL_WRITER TINT_BUILD_SPV_READER TINT_BUILD_SPV_WRITER
        TINT_BUILD_TINTD TINT_BUILD_WGSL_READER TINT_BUILD_WGSL_WRITER TINT_ENABLE_IR_DUMPING
        TINT_ENABLE_IR_VALIDATION_ASSERTS)
    if(NOT _mkw_dawn_TINT_BUILD_GLSL_WRITER)
        message(FATAL_ERROR "Dawn in '${_mkw_dawn_dir}' was built without Tint's GLSL writer: rerun tools/switch/dawn/build-dawn.sh")
    endif()
    set(_mkw_tint_defs TINT_BUILD_IS_LINUX=0 TINT_BUILD_IS_MAC=0 TINT_BUILD_IS_WIN=0)
    foreach(_opt GLSL_VALIDATOR GLSL_WRITER HLSL_WRITER IR_BINARY MSL_WRITER NULL_WRITER SPV_READER
                 SPV_WRITER TINTD WGSL_READER WGSL_WRITER)
        if(_mkw_dawn_TINT_BUILD_${_opt})
            list(APPEND _mkw_tint_defs TINT_BUILD_${_opt}=1)
        else()
            list(APPEND _mkw_tint_defs TINT_BUILD_${_opt}=0)
        endif()
    endforeach()
    foreach(_opt IR_DUMPING IR_VALIDATION_ASSERTS)
        if(_mkw_dawn_TINT_ENABLE_${_opt})
            list(APPEND _mkw_tint_defs TINT_ENABLE_${_opt}=1)
        else()
            list(APPEND _mkw_tint_defs TINT_ENABLE_${_opt}=0)
        endif()
    endforeach()
    add_library(dawn::tint INTERFACE IMPORTED)
    set_target_properties(dawn::tint PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_mkw_dawn_dir}/src;${_mkw_dawn_dir}/src/include;${_mkw_dawn_dir}/build/gen/include"
        INTERFACE_COMPILE_DEFINITIONS "${_mkw_tint_defs}"
        INTERFACE_COMPILE_FEATURES cxx_std_20
        # GCC's note on the C++17 ABI change for Tint's Number<> by-value arguments, once per use
        INTERFACE_COMPILE_OPTIONS "$<$<COMPILE_LANGUAGE:CXX>:-Wno-psabi>"
        INTERFACE_LINK_LIBRARIES dawn::webgpu_dawn)
endif()
set(Dawn_FOUND TRUE)
