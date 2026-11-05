#include "any360_system.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#include <sstream>
#include <string>

#include "any360/host/system.h"

REXCVAR_DEFINE_STRING(any360_sys_groups, "", "Kernel",
                      "Any360: system call groups run on any360::sys, comma-separated (stateless, files, objects, threads, "
                      "memory, xam, input, audio); empty: all on the runtime");
REXCVAR_DEFINE_BOOL(any360_sys_trace, false, "Kernel",
                    "Any360: log the first call of each system call that goes to any360::sys");
REXCVAR_DEFINE_BOOL(any360_sys_trace_all, false, "Kernel",
                    "Any360: log every system call that goes to any360::sys, with its arguments and result (slow)");
REXCVAR_DEFINE_STRING(any360_sys_runtime_calls, "", "Kernel",
                      "Any360: system calls left to the runtime though their group is on, comma-separated (debugging)");
REXCVAR_DEFINE_STRING(any360_sys_trace_groups, "", "Kernel",
                      "Any360: the groups any360_sys_trace_all reports, comma-separated; empty: all");

// Every system call any360::sys provides, overridden in the executable: the runtime's unless its group is switched.
// VdSwap and the other video calls are the graphics binding's (any360_graphics.cpp).
#include "any360/host/system_exports.inc"
#include "any360/sys/files.h"

namespace crackdown {

void StartAny360SystemIfAsked(const std::filesystem::path& game_data_root, const std::filesystem::path& user_data_root) {
  const std::string groups = REXCVAR_GET(any360_sys_groups);
  if (groups.empty()) return;
  std::string error;
  any360::host::rexglue::SystemOptions options;
  options.trace_calls = REXCVAR_GET(any360_sys_trace);
  options.trace_every_call = REXCVAR_GET(any360_sys_trace_all);
  options.log = [](const std::string& line) { REXLOG_INFO("Any360 system: {}", line); };
  {
    std::stringstream traced(REXCVAR_GET(any360_sys_trace_groups));
    std::string name;
    while (std::getline(traced, name, ',')) {
      any360::host::rexglue::SysGroup group;
      if (any360::host::rexglue::ParseSysGroup(name, group)) options.trace_groups.push_back(group);
    }
    std::stringstream kept(REXCVAR_GET(any360_sys_runtime_calls));
    while (std::getline(kept, name, ',')) {
      if (!name.empty()) options.runtime_calls.push_back(name);
    }
  }
  // As the runtime mounts them (Runtime::SetupVfs, MountCacheDevice).
  const bool game_writes = rex::cvar::GetFlagByName("allow_game_relative_writes") == "true";
  options.mounts.push_back({"\\Device\\Harddisk0\\Partition1", std::filesystem::absolute(game_data_root).string(),
                            !game_writes, {"game:", "d:"}});
  options.mounts.push_back({"\\Device\\cache0", (user_data_root / "4D5307DC" / "cache").string(), false, {}});
  // XAM: the saves and the profile settings where the runtime keeps them.
  options.user_data_root = std::filesystem::absolute(user_data_root).string();
  if (!any360::host::rexglue::StartSystem(options, error)) {
    REXLOG_ERROR("Any360 system: {} (every system call stays on the runtime)", error);
    return;
  }
  std::stringstream list(groups);
  std::string name, switched;
  while (std::getline(list, name, ',')) {
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    while (!name.empty() && name.back() == ' ') name.pop_back();
    any360::host::rexglue::SysGroup group;
    if (!any360::host::rexglue::ParseSysGroup(name, group)) {
      REXLOG_WARN("Any360 system: no group named '{}'", name);
      continue;
    }
    any360::host::rexglue::EnableSystemGroup(group, true);
    switched += (switched.empty() ? "" : ", ") + name;
  }
  const uint32_t variables = any360::host::rexglue::BindVariables();
  REXLOG_INFO("Any360 system: {} on any360::sys ({} variable imports)", switched.empty() ? "no group" : switched, variables);
}

void MountOnAny360Files(const std::string& guest_folder, const std::filesystem::path& host) {
  if (REXCVAR_GET(any360_sys_groups).empty()) return;
  // The longest mounted prefix wins, so this folder shadows the title's own under the partition's mount.
  any360::sys::Files().Mount(guest_folder, host, /*read_only=*/true);
  REXLOG_INFO("Any360 system: {} mounted at {}", guest_folder, host.string());
}

}  // namespace crackdown
