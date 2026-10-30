crackdown_add_test(hle_draw_match_tests SOURCES hle_draw_match_tests.cpp)
target_compile_definitions(hle_draw_match_tests PRIVATE CRACKDOWN_PATCH_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../patches/rexglue-sdk")
