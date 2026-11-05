#pragma once

#include <filesystem>
#include <string>

namespace crackdown {

// The system call groups the any360_sys_groups setting names (stateless, files, objects, threads, memory, xam, input,
// audio) switched to any360::sys, its file system mounting what the runtime mounts (the title's files, the cache
// partition). Call once the title is loaded, before its first instruction (OnPostSetup).
void StartAny360SystemIfAsked(const std::filesystem::path& game_data_root, const std::filesystem::path& user_data_root);

// A host folder over a guest folder on any360::sys's file system as well (read-only), for the redirections the title
// sets up on the runtime's VFS (the button glyphs' language folder). Nothing while the system binding is off.
void MountOnAny360Files(const std::string& guest_folder, const std::filesystem::path& host);

}  // namespace crackdown
