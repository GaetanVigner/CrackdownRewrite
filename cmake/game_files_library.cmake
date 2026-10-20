# The game-data check and the disc installer (src/game_files.cpp, src/xdvdfs.cpp) as one object library. The game, the
# pure-logic tests (tests/) and the bundler (tools/bundler/) all link this target, so the launcher's "Install from disc
# image..." and the bundler validate and extract a disc with the same code. It needs no ReXGlue SDK.
include_guard(GLOBAL)

find_package(Threads REQUIRED)

get_filename_component(CRACKDOWN_GAME_FILES_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../src" ABSOLUTE)
add_library(crackdown_game_files OBJECT
    "${CRACKDOWN_GAME_FILES_SRC_DIR}/game_files.cpp"
    "${CRACKDOWN_GAME_FILES_SRC_DIR}/xdvdfs.cpp"
)
target_include_directories(crackdown_game_files PUBLIC "${CRACKDOWN_GAME_FILES_SRC_DIR}")
target_compile_features(crackdown_game_files PUBLIC cxx_std_20)
# The installer copies on a worker thread.
target_link_libraries(crackdown_game_files PUBLIC Threads::Threads)
