# crackdown-bundler: a stand-alone tool, independent of the ReXGlue SDK and of the game build. It reuses the launcher's
# disc check and installer (cmake/game_files_library.cmake). See docs/bundler.md.
#
#   cmake -S tools/bundler -B out/bundler -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build out/bundler
#   ctest --test-dir out/bundler --output-on-failure

cmake_minimum_required(VERSION 3.25)
project(crackdown_bundler VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()
# No Visual C++ runtime to install next to the tool.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

get_filename_component(CRACKDOWN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include("${CRACKDOWN_ROOT}/cmake/game_files_library.cmake")

# Third-party sources, pinned by SHA-256: miniz (ZIP, gzip) and the zstd decompressor (AppImages are zstd SquashFS).
include(FetchContent)
FetchContent_Declare(crackdown_miniz
    URL https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
    URL_HASH SHA256=ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR no-cmake)
FetchContent_Declare(crackdown_zstd
    URL https://github.com/facebook/zstd/releases/download/v1.5.6/zstd-1.5.6.tar.gz
    URL_HASH SHA256=8c29e06cf42aacc1eafc4077ae2ec6c6fcb96a626157e0593d5e82a34fd403c1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR no-cmake)
FetchContent_MakeAvailable(crackdown_miniz crackdown_zstd)

add_library(crackdown_bundler_miniz STATIC "${crackdown_miniz_SOURCE_DIR}/miniz.c")
target_include_directories(crackdown_bundler_miniz SYSTEM PUBLIC "${crackdown_miniz_SOURCE_DIR}")

file(GLOB _zstd_sources "${crackdown_zstd_SOURCE_DIR}/lib/common/*.c" "${crackdown_zstd_SOURCE_DIR}/lib/decompress/*.c")
add_library(crackdown_bundler_zstd STATIC ${_zstd_sources})
target_include_directories(crackdown_bundler_zstd SYSTEM PUBLIC "${crackdown_zstd_SOURCE_DIR}/lib")
target_compile_definitions(crackdown_bundler_zstd PRIVATE ZSTD_DISABLE_ASM ZSTD_MULTITHREAD=0)

set(CRACKDOWN_BUNDLER_WARNINGS "$<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wall;-Wextra>")

add_library(crackdown_bundler_core STATIC
    src/archive.cpp
    src/bundle.cpp
    src/squashfs.cpp
)
target_include_directories(crackdown_bundler_core PUBLIC src)
target_link_libraries(crackdown_bundler_core PUBLIC crackdown_game_files crackdown_bundler_miniz crackdown_bundler_zstd)
target_compile_options(crackdown_bundler_core PRIVATE ${CRACKDOWN_BUNDLER_WARNINGS})

add_executable(crackdown-bundler src/main.cpp)
target_link_libraries(crackdown-bundler PRIVATE crackdown_bundler_core)
target_compile_options(crackdown-bundler PRIVATE ${CRACKDOWN_BUNDLER_WARNINGS})
target_compile_definitions(crackdown-bundler PRIVATE CRACKDOWN_BUNDLER_VERSION="${PROJECT_VERSION}")
if(WIN32)
    target_link_libraries(crackdown-bundler PRIVATE shell32)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Runs on systems with an older C++ runtime than the build machine's (the Steam Deck among them).
    target_link_options(crackdown-bundler PRIVATE -static-libstdc++ -static-libgcc)
endif()

enable_testing()
add_executable(crackdown_bundler_tests tests/bundler_tests.cpp)
target_include_directories(crackdown_bundler_tests PRIVATE "${CRACKDOWN_ROOT}/tests")
target_link_libraries(crackdown_bundler_tests PRIVATE crackdown_bundler_core)
target_compile_options(crackdown_bundler_tests PRIVATE ${CRACKDOWN_BUNDLER_WARNINGS})
add_test(NAME crackdown_bundler_tests COMMAND crackdown_bundler_tests)
