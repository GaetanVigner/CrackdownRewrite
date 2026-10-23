#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

#include "content_fs.h"

namespace crackdown {

// What a supported install is. The real one comes from src/game_manifest.h; tests inject small ones.
struct GameDescriptor {
  std::string xex_name;  // matched case-insensitively, at the root
  uint64_t xex_size = 0;
  std::array<uint8_t, 32> xex_sha256{};
  struct File {
    std::string path;  // forward-slash separated, relative to the game folder
    uint64_t size = 0;
  };
  std::vector<File> required_files;            // includes the executable
  std::vector<std::string> optional_prefixes;  // may exist in a source, never extracted
};

const GameDescriptor& CrackdownGame();

struct GameFiles {
  enum State {
    kReady,
    kMissing,      // no folder, or no executable in it
    kUnsupported,  // the executable is a different build
    kIncomplete,   // a required file is missing or has the wrong size
  };
  State state = kMissing;
  std::string detail;  // for the log
};

// The one readiness check: the launcher, the install verification and the clean-start decision all use it.
// File names are matched case-insensitively. Cheap enough to run at every start.
GameFiles CheckGameFiles(const std::filesystem::path& game_dir, const GameDescriptor& game = CrackdownGame());

enum class InstallStatus {
  kOk,
  kCancelled,
  kNoSource,            // nothing chosen, or the chosen path does not exist
  kNotAnInstallSource,  // not a disc image and not a folder holding the executable
  kWrongVersion,        // the executable is a different build
  kIncomplete,          // a required file is missing, short or beyond the end of a truncated image
  kNoSpace,
  kNoGameFolder,        // no usable game folder path, or its parent folder does not exist
  kForeignFolder,       // the game folder holds something other than a game install
  kUnsafeTarget,        // links in the work folders, or the source and the game folder overlap
  kIoError,
};

struct InstallResult {
  InstallStatus status = InstallStatus::kOk;
  std::string detail;  // technical text for the log only
  bool ok() const { return status == InstallStatus::kOk; }
};

// `source` is a disc image file (XDVDFS) or a folder with the extracted disc contents. The required files are
// copied into a staging folder next to `game_dir`, verified with CheckGameFiles and only then swapped in, keeping
// the previous install until the swap succeeds. On any failure or cancellation the previous install is as it was.
InstallResult InstallGame(const std::filesystem::path& source, const std::filesystem::path& game_dir,
                          const InstallHooks& hooks = {}, const GameDescriptor& game = CrackdownGame(),
                          const FileOps& ops = {});

struct InstallMessage {
  std::string title;
  std::string text;  // plain, player-facing
};
InstallMessage DescribeInstall(InstallStatus status);

// A crash between the two renames of the swap leaves `<name>-install-backup` and no game folder: put it back.
// Call once at start-up. Returns true if it restored something.
bool RecoverInterruptedInstall(const std::filesystem::path& game_dir, const GameDescriptor& game = CrackdownGame(),
                               const FileOps& ops = {});

}  // namespace crackdown
