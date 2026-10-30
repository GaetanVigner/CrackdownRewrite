# Native translations (src/decomp): the recompiled originals listed in tests/decomp/functions.txt are extracted from
# the local generated/default tree at configure time and run against the translations by decomp_io_tests. Without
# generated code the test is not added. No guest code is kept in the repository.

set(_crackdown_generated "${CMAKE_CURRENT_SOURCE_DIR}/../generated/default")
set(_decomp_list "${CMAKE_CURRENT_SOURCE_DIR}/decomp/functions.txt")
if(EXISTS "${_crackdown_generated}/codegen.partition.json" AND EXISTS "${_decomp_list}")
    set(_out "${CMAKE_CURRENT_BINARY_DIR}/decomp_originals.cpp")
    set(_partition "${_crackdown_generated}/codegen.partition.json")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_partition}" "${_decomp_list}")
    file(READ "${_partition}" _json)
    file(STRINGS "${_decomp_list}" _lines)

    set(_bodies "")
    set(_table "")
    set(_count 0)
    foreach(_line IN LISTS _lines)
        string(STRIP "${_line}" _address)
        if(_address STREQUAL "" OR _address MATCHES "^#")
            continue()
        endif()
        string(REGEX MATCH "\"${_address}\": [0-9]+" _pair "${_json}")
        if(NOT _pair)
            message(WARNING "decomp originals: ${_address} is not in ${_partition}")
            continue()
        endif()
        string(REGEX REPLACE "^.*: " "" _index "${_pair}")
        file(READ "${_crackdown_generated}/crackdown_recomp.${_index}.cpp" _text)
        string(FIND "${_text}" "DEFINE_REX_FUNC(sub_${_address})" _start)
        if(_start EQUAL -1)
            message(WARNING "decomp originals: sub_${_address} not found in crackdown_recomp.${_index}.cpp")
            continue()
        endif()
        string(SUBSTRING "${_text}" ${_start} -1 _text)
        string(FIND "${_text}" "\n}\n" _end)
        math(EXPR _length "${_end} + 3")
        string(SUBSTRING "${_text}" 0 ${_length} _body)
        string(APPEND _bodies "${_body}\n")
        string(APPEND _table "    {0x${_address}u, &sub_${_address}},\n")
        math(EXPR _count "${_count} + 1")
    endforeach()

    set(_generated "// Extracted from generated/default at configure time; do not commit.\n#include \"guest_sandbox.h\"\n\n")
    string(APPEND _generated "${_bodies}")
    string(APPEND _generated "extern const guest_sandbox::FunctionEntry kDecompOriginals[] = {\n${_table}};\n")
    string(APPEND _generated "extern const unsigned kDecompOriginalCount = ${_count};\n")
    file(WRITE "${_out}.new" "${_generated}")
    configure_file("${_out}.new" "${_out}" COPYONLY)
    message(STATUS "decomp originals: ${_count} recompiled functions extracted for decomp_io_tests")

    crackdown_add_test(decomp_io_tests SOURCES decomp_io_tests.cpp "${_out}" "${CRACKDOWN_SRC_DIR}/decomp/leaf_batch1.cpp"
                       "${CRACKDOWN_SRC_DIR}/decomp/leaf_batch2.cpp"
                       "${CRACKDOWN_SRC_DIR}/decomp/leaf_batch3.cpp"
                       "${CRACKDOWN_SRC_DIR}/decomp/leaf_batch4.cpp"
                       "${CRACKDOWN_SRC_DIR}/decomp/leaf_batch5.cpp"
                       "${CRACKDOWN_SRC_DIR}/decomp/leaf_batch6.cpp"
                       "${CRACKDOWN_SRC_DIR}/decomp/leaf_batch7.cpp"
                       "${CRACKDOWN_SRC_DIR}/decomp/leaf_batch8.cpp"
                       "${CRACKDOWN_SRC_DIR}/decomp/leaf_batch9.cpp"
                       "${CRACKDOWN_SRC_DIR}/decomp/leaf_batch10.cpp"
                       "${CRACKDOWN_SRC_DIR}/decomp/leaf_batch11.cpp"
                       SDK_HEADERS)
    target_include_directories(decomp_io_tests PRIVATE "${_crackdown_generated}")
    target_compile_definitions(decomp_io_tests PRIVATE SPDLOG_COMPILED_LIB SPDLOG_FMT_EXTERNAL)
    if(WIN32)
        target_compile_definitions(decomp_io_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
        target_compile_options(decomp_io_tests PRIVATE -msse4.1)
    endif()
    if(WIN32)
        target_link_libraries(decomp_io_tests PRIVATE "${CRACKDOWN_SDK_DIR}/lib/rexruntimerd.lib"
                              "${CRACKDOWN_SDK_DIR}/lib/spdlogrd.lib" "${CRACKDOWN_SDK_DIR}/lib/fmtrd.lib")
        foreach(_dll rexruntimerd.dll TracyClientrd.dll)
            add_custom_command(TARGET decomp_io_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
                               "${CRACKDOWN_SDK_DIR}/bin/${_dll}" "$<TARGET_FILE_DIR:decomp_io_tests>")
        endforeach()
    else()
        target_link_libraries(decomp_io_tests PRIVATE "${CRACKDOWN_SDK_DIR}/lib/librexruntimerd.so")
    endif()
    target_compile_options(decomp_io_tests PRIVATE -Wno-unused-label -Wno-unused-variable -Wno-unused-parameter
                           -Wno-unused-but-set-variable -Wno-sign-compare)
endif()
