crackdown_add_test(vulkan_per_draw_tests SOURCES vulkan_per_draw_tests.cpp)
target_compile_definitions(vulkan_per_draw_tests PRIVATE CRACKDOWN_PATCH_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../patches/rexglue-sdk")
