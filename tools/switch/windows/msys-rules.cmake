# SPDX-License-Identifier: GPL-3.0-or-later
# CMAKE_PROJECT_INCLUDE for Switch builds under MSYS2 (tools/switch/build-nro.sh, dawn/build-dawn.sh):
# archives and links go through rsp.sh, which hands devkitA64 its arguments in a response file. MSYS2's
# CMake assumes no command line limit, but native Windows programs have 32K.
foreach(_rule CMAKE_C_ARCHIVE_CREATE CMAKE_CXX_ARCHIVE_CREATE CMAKE_C_ARCHIVE_APPEND
              CMAKE_CXX_ARCHIVE_APPEND CMAKE_C_LINK_EXECUTABLE CMAKE_CXX_LINK_EXECUTABLE)
    if(DEFINED ${_rule} AND NOT ${_rule} MATCHES "rsp\\.sh")
        set(${_rule} "sh ${CMAKE_CURRENT_LIST_DIR}/rsp.sh ${${_rule}}")
    endif()
endforeach()
