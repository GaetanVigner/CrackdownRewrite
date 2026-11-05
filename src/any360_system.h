#pragma once

#include <filesystem>

namespace crackdown {

// The system call groups the any360_sys_groups setting names (stateless, files, objects, threads, memory, xam, input,
// audio) switched to any360::sys, its file system mounting what the runtime mounts (the title's files, the cache
// partition). Call once the title is loaded, before its first instruction (OnPostSetup).
void StartAny360SystemIfAsked(const std::filesystem::path& game_data_root, const std::filesystem::path& user_data_root);

}  // namespace crackdown
