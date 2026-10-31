# CMAKE_PROJECT_INCLUDE for the PGO builds (MLB_SWITCH_PGO=gen|use:<dir> in tools/switch/build-nro.sh).
# Replaces tools/switch/windows/msys-rules.cmake as the project include (it includes that file first), so
# the PGO hook needs no edit of the runtime's own CMake files:
#   -DMLB_PGO_MSYS_RULES=<msys-rules.cmake>    the rules this file stands in for (optional)
#   -DMLB_PGO_DIR=<dir with mlb_pgo_gen.cpp, pgo_core.cpp, pgo_core.h>
#   -DMLB_PGO_TARGETS=WiiCompiled              executables that get the hook
#   -DMLB_PGO_MODE=gen|use
# Only the gen build gets the hook sources. main.cpp's call site is the same in every build (a weak
# reference, null outside the gen build), so the profiled code is identical in gen and use.
if(MLB_PGO_MSYS_RULES)
    include("${MLB_PGO_MSYS_RULES}")
endif()

# The instrumentation/use options for every C/C++ target of the project, set here (directory scope, top
# level, right after its project()) rather than through CMAKE_<LANG>_FLAGS: those cache variables would
# replace the Switch toolchain's own init flags (-march, -mtp=soft, -ffunction-sections, -D__SWITCH__).
# try_compile checks do not see them.
if(MLB_PGO_COMPILE_OPTIONS AND CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    get_property(_mlb_pgo_opts GLOBAL PROPERTY MLB_PGO_OPTIONS_SET)
    if(NOT _mlb_pgo_opts)
        set_property(GLOBAL PROPERTY MLB_PGO_OPTIONS_SET TRUE)
        foreach(_o IN LISTS MLB_PGO_COMPILE_OPTIONS)
            add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:${_o}>")
        endforeach()
        if(MLB_PGO_LINK_OPTIONS)
            add_link_options(${MLB_PGO_LINK_OPTIONS})
        endif()
    endif()
endif()

if(MLB_PGO_DIR AND MLB_PGO_MODE STREQUAL "gen")
    get_property(_mlb_pgo_deferred GLOBAL PROPERTY MLB_PGO_DEFERRED)
    if(NOT _mlb_pgo_deferred)
        set_property(GLOBAL PROPERTY MLB_PGO_DEFERRED TRUE)

        function(_mlb_pgo_attach)
            foreach(_t IN LISTS MLB_PGO_TARGETS)
                if(NOT TARGET ${_t})
                    message(FATAL_ERROR "MLB_PGO_TARGETS: no target ${_t}")
                endif()
                set(_src "${MLB_PGO_DIR}/mlb_pgo_gen.cpp" "${MLB_PGO_DIR}/pgo_core.cpp")
                target_sources(${_t} PRIVATE ${_src})
                target_include_directories(${_t} PRIVATE "${MLB_PGO_DIR}")
                # <switch.h> and <sys/socket.h> names must not meet the rest of a unity group
                set_source_files_properties(${_src} PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
                                            SKIP_PRECOMPILE_HEADERS ON)
                if(MLB_PGO_MODE STREQUAL "gen")
                    # zlib for the profile stream (the game's own targets do not see portlibs' headers)
                    if(DEFINED ENV{DEVKITPRO})
                        set(_dkp "$ENV{DEVKITPRO}")
                    else()
                        set(_dkp "/opt/devkitpro")
                    endif()
                    set_source_files_properties("${MLB_PGO_DIR}/pgo_core.cpp" PROPERTIES
                                                COMPILE_OPTIONS "-isystem;${_dkp}/portlibs/switch/include")
                    # only this file's own text changes between gen and use (everything else is identical)
                    set_source_files_properties("${MLB_PGO_DIR}/mlb_pgo_gen.cpp" PROPERTIES
                                                COMPILE_DEFINITIONS MLB_PGO_GEN)
                endif()
            endforeach()
        endfunction()

        cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _mlb_pgo_attach)
    endif()
endif()
