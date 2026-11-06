#include "any360_system.h"

#include "any360/host/settings.h"

// Every system call any360::sys provides, overridden in the executable: the runtime's unless its group is switched.
// VdSwap and the other video calls are the graphics binding's (any360_graphics.cpp).
#include "any360/host/system_exports.inc"

namespace crackdown {

void StartAny360SystemIfAsked(const std::filesystem::path& game_data_root, const std::filesystem::path& user_data_root) {
  // The settings, the title's files and the console's devices are Any360's; Crackdown adds the cache device it
  // registers on the runtime's file system (MountCacheDevice).
  any360::host::rexglue::StartSystemFromSettings(
      game_data_root, user_data_root, {{"\\Device\\cache0", (user_data_root / "4D5307DC" / "cache").string(), false, {}}});
}

void MountOnAny360Files(const std::string& guest_folder, const std::filesystem::path& host) {
  any360::host::rexglue::MountFolderIfStarted(guest_folder, host);
}

}  // namespace crackdown
