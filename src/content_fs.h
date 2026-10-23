#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>

namespace crackdown {

struct InstallProgress {
  std::string phase;
  uint64_t done_bytes = 0;  // across the whole install, never decreasing
  uint64_t total_bytes = 0;
};

struct InstallHooks {
  // Return false to cancel. Cancelling is possible until the new files are activated.
  std::function<bool(const InstallProgress&)> progress;
};

// The file system calls an install makes that tests replace. An empty function means the real call.
struct FileOps {
  std::function<void(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code&)> rename;
  std::function<void(const std::filesystem::path&, std::error_code&)> remove_all;
  std::function<uintmax_t(const std::filesystem::path&, std::error_code&)> available_space;
  std::chrono::milliseconds retry_delay{300};  // between attempts of a rename (antivirus can hold a new folder)
  int rename_attempts = 10;
};

// Path checks shared by the game, title update and DLC installers. None follows a link.
namespace content_fs {

namespace fs = std::filesystem;

std::string PathToUtf8(const fs::path& path);
fs::path::string_type LowerNative(fs::path::string_type text);

// A symbolic link or a junction. Cloud placeholders are reparse points too but redirect nothing.
bool IsLink(const fs::path& path);
bool IsAbsent(const fs::path& path);
bool IsPlainFile(const fs::path& path);
// Recursive deletion must not follow a link, so a link anywhere under `dir` makes the folder unsafe to delete.
bool ContainsLink(const fs::path& dir);
bool IsPlainFolder(const fs::path& path);
// Nothing at `path`, or a folder with no link at or under it. Only these may be deleted recursively.
bool IsAbsentOrPlainFolder(const fs::path& path);
bool IsAbsentOrPlainFile(const fs::path& path);

fs::path Resolved(const fs::path& path);
bool IsSameOrInside(const fs::path& outer, const fs::path& inner);
// True when `path` is below `root` (lexically); `root` itself does not count.
bool IsInside(const fs::path& root, const fs::path& path);

// True when `child` is a real folder directly inside `parent`, reached without a link on the way.
bool IsPlainChildDirectory(const fs::path& parent, const fs::path& child);
// True when removing `folder` would delete `game_dir` (it is the folder or inside it), or `folder` cannot be resolved.
bool GameFolderWouldBeRemoved(const fs::path& folder, const fs::path& game_dir);

bool RenameWithRetry(const fs::path& from, const fs::path& to, const FileOps& ops, std::error_code& last);

enum class Removal { kGone, kUnsafe, kFailed };

// Deletes a folder only when it is absent or has no link at or under it.
Removal RemoveWorkFolder(const fs::path& dir, const FileOps& ops);

enum class SwapResult { kActivated, kUnchanged, kStranded };

// Moves `staged` into place at `live` and keeps the previous `live` at `backup`, so the caller can still revert.
// kStranded: the previous copy could not be put back and is still at `backup`.
SwapResult SwapIn(const fs::path& staged, const fs::path& live, const fs::path& backup, const FileOps& ops,
                  bool& had_previous);

// Removes the activated folder, then restores the previous one. False when the previous copy is not back in place.
bool RevertSwap(const fs::path& live, const fs::path& backup, bool had_previous, const FileOps& ops);

// Clears the folder when the operation ends, unless released.
class WorkFolderGuard {
 public:
  WorkFolderGuard(fs::path dir, const FileOps& ops) : dir_(std::move(dir)), ops_(ops) {}
  WorkFolderGuard(const WorkFolderGuard&) = delete;
  WorkFolderGuard& operator=(const WorkFolderGuard&) = delete;
  ~WorkFolderGuard() {
    if (armed_) {
      RemoveWorkFolder(dir_, ops_);
    }
  }
  void Release() { armed_ = false; }

 private:
  fs::path dir_;
  const FileOps& ops_;
  bool armed_ = true;
};

}  // namespace content_fs
}  // namespace crackdown
