# Title update and DLC installers: STFS reader, Marketplace layout, dependency rules. No game files needed.
find_package(Threads REQUIRED)

crackdown_add_test(sha1_tests SOURCES sha1_tests.cpp)
crackdown_add_test(content_policy_tests SOURCES content_policy_tests.cpp ../src/content_state.cpp ../src/title_update.cpp
                   ../src/dlc.cpp ../src/stfs.cpp ../src/content_fs.cpp ../src/content_install.cpp ../src/save_compat.cpp ../src/save_generation.cpp)
target_link_libraries(content_policy_tests PRIVATE Threads::Threads)

crackdown_add_test(stfs_tests SOURCES stfs_tests.cpp ../src/stfs.cpp)

crackdown_add_test(dlc_tests SOURCES dlc_tests.cpp ../src/dlc.cpp ../src/stfs.cpp ../src/content_fs.cpp
                   ../src/content_install.cpp)
target_link_libraries(dlc_tests PRIVATE Threads::Threads)

crackdown_add_test(title_update_tests SOURCES title_update_tests.cpp ../src/title_update.cpp ../src/dlc.cpp
                   ../src/stfs.cpp ../src/content_fs.cpp ../src/content_install.cpp)
target_link_libraries(title_update_tests PRIVATE Threads::Threads)

crackdown_add_test(content_state_tests SOURCES content_state_tests.cpp ../src/content_state.cpp ../src/title_update.cpp
                   ../src/dlc.cpp ../src/stfs.cpp ../src/content_fs.cpp ../src/content_install.cpp
                   ../src/startup_selection.cpp ../src/user_data.cpp ../src/save_compat.cpp ../src/save_generation.cpp)
crackdown_add_test(save_compat_tests SOURCES save_compat_tests.cpp ../src/save_compat.cpp)
crackdown_add_test(save_generation_tests SOURCES save_generation_tests.cpp ../src/save_compat.cpp ../src/save_generation.cpp)
target_link_libraries(content_state_tests PRIVATE Threads::Threads)

# Developer tool for tools\codegen.ps1: installs an update package with the port installer (not a test).
add_executable(crackdown_install_update EXCLUDE_FROM_ALL ../tools/install_update/main.cpp ../src/title_update.cpp ../src/dlc.cpp
               ../src/stfs.cpp ../src/content_fs.cpp ../src/content_install.cpp)
target_include_directories(crackdown_install_update PRIVATE "${CRACKDOWN_SRC_DIR}")
target_compile_options(crackdown_install_update PRIVATE -Wall -Wextra)
target_link_libraries(crackdown_install_update PRIVATE Threads::Threads)

# The packages and the Title Update 6 target, checked from their scripts and sources.
crackdown_add_test(packaging_tests SOURCES packaging_tests.cpp)
