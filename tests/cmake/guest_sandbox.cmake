# Guest function sandbox (tests/guest_sandbox.h): the leaf functions of the title's D3D runtime (0x826A0000-
# 0x826D0000, no calls, no MMIO) are extracted from the local generated/default tree at configure time into the
# build folder and run by guest_d3d_leaf_tests. Without generated code (tools/codegen.ps1 not run) the test is not
# added. No guest code is kept in the repository.

set(_crackdown_generated "${CMAKE_CURRENT_SOURCE_DIR}/../generated/default")
if(EXISTS "${_crackdown_generated}/codegen.partition.json")
    set(_out "${CMAKE_CURRENT_BINARY_DIR}/guest_d3d_leaf_functions.cpp")
    set(_partition "${_crackdown_generated}/codegen.partition.json")
    # Re-extract when codegen runs again.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_partition}")
    file(READ "${_partition}" _json)
    string(REGEX MATCHALL "\"826[ABC][0-9A-F][0-9A-F][0-9A-F][0-9A-F]\": [0-9]+" _pairs "${_json}")
    set(_files)
    foreach(_pair IN LISTS _pairs)
        string(REGEX REPLACE "^.*: " "" _index "${_pair}")
        list(APPEND _files "${_index}")
    endforeach()
    list(REMOVE_DUPLICATES _files)

    set(_bodies "")
    set(_table "")
    set(_count 0)
    foreach(_index IN LISTS _files)
        file(READ "${_crackdown_generated}/crackdown_recomp.${_index}.cpp" _text)
        while(TRUE)
            string(FIND "${_text}" "DEFINE_REX_FUNC(sub_826" _start)
            if(_start EQUAL -1)
                break()
            endif()
            string(SUBSTRING "${_text}" ${_start} -1 _text)
            string(FIND "${_text}" "\n}\n" _end)
            if(_end EQUAL -1)
                break()
            endif()
            math(EXPR _length "${_end} + 3")
            string(SUBSTRING "${_text}" 0 ${_length} _body)
            string(SUBSTRING "${_text}" ${_length} -1 _text)
            string(SUBSTRING "${_body}" 16 12 _name)  # sub_XXXXXXXX
            string(SUBSTRING "${_name}" 4 8 _address)
            # sub_826A0000-sub_826CFFFF only, and only functions that call nothing (directly or indirectly), touch no
            # MMIO and do not read the time base.
            if(NOT _address MATCHES "^826[ABC]")
                continue()
            endif()
            string(FIND "${_body}" "(ctx, base);" _call)
            string(FIND "${_body}" "REX_MM_" _mmio)
            string(FIND "${_body}" "CALL_INDIRECT" _indirect)
            string(FIND "${_body}" "REX_QUERY_TIMEBASE" _time)
            if(NOT _call EQUAL -1 OR NOT _mmio EQUAL -1 OR NOT _indirect EQUAL -1 OR NOT _time EQUAL -1)
                continue()
            endif()
            string(APPEND _bodies "${_body}\n")
            string(APPEND _table "    {0x${_address}u, &${_name}},\n")
            math(EXPR _count "${_count} + 1")
        endwhile()
    endforeach()

    set(_generated "// Extracted from generated/default at configure time; do not commit.\n#include \"guest_sandbox.h\"\n\n")
    string(APPEND _generated "${_bodies}")
    string(APPEND _generated "extern const guest_sandbox::FunctionEntry kGuestD3dLeafFunctions[] = {\n${_table}};\n")
    string(APPEND _generated "extern const unsigned kGuestD3dLeafFunctionCount = ${_count};\n")
    # Written through a copy so an unchanged extraction does not rebuild the test.
    file(WRITE "${_out}.new" "${_generated}")
    configure_file("${_out}.new" "${_out}" COPYONLY)
    message(STATUS "guest sandbox: ${_count} D3D runtime leaf functions extracted")

    crackdown_add_test(guest_d3d_leaf_tests SOURCES guest_d3d_leaf_tests.cpp "${_out}" SDK_HEADERS)
    target_include_directories(guest_d3d_leaf_tests PRIVATE "${_crackdown_generated}")
    # As the game build compiles the generated code.
    target_compile_definitions(guest_d3d_leaf_tests PRIVATE SPDLOG_COMPILED_LIB SPDLOG_FMT_EXTERNAL)
    if(WIN32)
        target_compile_definitions(guest_d3d_leaf_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
        target_compile_options(guest_d3d_leaf_tests PRIVATE -msse4.1)
    endif()
    # The prelude's logging (reached only on unimplemented instructions) links the runtime, spdlog and fmt; the
    # tests build RelWithDebInfo, the SDK's "rd" libraries.
    if(WIN32)
        target_link_libraries(guest_d3d_leaf_tests PRIVATE "${CRACKDOWN_SDK_DIR}/lib/rexruntimerd.lib"
                              "${CRACKDOWN_SDK_DIR}/lib/spdlogrd.lib" "${CRACKDOWN_SDK_DIR}/lib/fmtrd.lib")
        foreach(_dll rexruntimerd.dll TracyClientrd.dll)
            add_custom_command(TARGET guest_d3d_leaf_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
                               "${CRACKDOWN_SDK_DIR}/bin/${_dll}" "$<TARGET_FILE_DIR:guest_d3d_leaf_tests>")
        endforeach()
    else()
        target_link_libraries(guest_d3d_leaf_tests PRIVATE "${CRACKDOWN_SDK_DIR}/lib/librexruntimerd.so")
    endif()
    target_compile_options(guest_d3d_leaf_tests PRIVATE -Wno-unused-label -Wno-unused-variable -Wno-unused-parameter
                           -Wno-unused-but-set-variable -Wno-sign-compare)
    target_compile_definitions(guest_d3d_leaf_tests PRIVATE
        CRACKDOWN_GOLDEN="${CMAKE_CURRENT_SOURCE_DIR}/golden/guest_d3d_leaf.txt")
endif()
