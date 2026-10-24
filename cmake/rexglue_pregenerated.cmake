# Compile wiring for the sources codegen wrote to generated/default and generated/tu6.
# generated/<guest>/rexglue.cmake adds a codegen rule that reruns during cmake --build,
# so it is not included. tools/codegen.ps1 is the only codegen entry point.
# The guests themselves are wired in cmake/crackdown_guests.cmake.

find_package(rexglue 0.10.0 REQUIRED CONFIG)

# Guest SEH scopes need asynchronous EH to trap hardware faults.
set(REXGLUE_RECOMP_OPTIONS "")
if(WIN32)
    if(MSVC)
        list(APPEND REXGLUE_RECOMP_OPTIONS /EHa)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        list(APPEND REXGLUE_RECOMP_OPTIONS -fasync-exceptions)
    endif()
endif()
list(APPEND REXGLUE_RECOMP_OPTIONS
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-gline-tables-only>)

# clang rejects a PCH whose EH mode differs from the including TU, so the options go on the target too.
function(rexglue_apply_recomp_settings target_name pch_header)
    target_precompile_headers(${target_name} PRIVATE "${pch_header}")
    target_compile_options(${target_name} PRIVATE ${REXGLUE_RECOMP_OPTIONS})
endfunction()

macro(rexglue_setup_host_target target_name)
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(${target_name} PRIVATE rex::runtime)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/metadata/icons")
        rexglue_embed_metadata(${target_name}
            DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/metadata/icons"
            PREFIX "icons")
    endif()
    rexglue_configure_target(${target_name} ${ARGN})
endmacro()
