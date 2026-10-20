# Game-data check and installer: SHA-256, XDVDFS reader, staging and swap. No game files needed.
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/game_files_library.cmake")

crackdown_add_test(installer_sha256_tests SOURCES sha256_tests.cpp)

crackdown_add_test(installer_tests SOURCES installer_tests.cpp)
target_link_libraries(installer_tests PRIVATE crackdown_game_files)
