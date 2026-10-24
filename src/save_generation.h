#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "content_catalog.h"

namespace crackdown {

// The base game and Title Update 6 write incompatible save files. The port keeps one snapshot of the profile and
// campaign save state per generation and swaps the live files to the generation about to run.
enum class SaveGeneration { kBase, kTitleUpdate6 };

SaveGeneration GenerationFor(content::GuestBuild build);
const char* SaveGenerationName(SaveGeneration generation);

// The file operations a switch is made of, replaceable to test failures.
struct SaveFileOps {
  std::function<bool(const std::filesystem::path& from, const std::filesystem::path& to)> copy_file;
  std::function<bool(const std::filesystem::path& from, const std::filesystem::path& to)> rename;
  std::function<void(const std::filesystem::path&)> remove_all;
};

enum class SaveSwitchStatus {
  kUnchanged,            // the live saves already belong to the target generation
  kSwitched,
  kLiveUnknown,          // the live saves are malformed: nothing is switched
  kSnapshotInvalid,      // the snapshot of the target generation is malformed and was not restored
  kFailed,               // a file operation failed; the live saves are as they were
};

struct SaveSwitchResult {
  SaveSwitchStatus status = SaveSwitchStatus::kUnchanged;
  SaveGeneration from = SaveGeneration::kBase;
  SaveGeneration to = SaveGeneration::kBase;
  bool preserved_snapshot = false;
  bool restored_snapshot = false;
  bool created_empty_snapshot = false;  // no base-game snapshot existed: the base game starts with no saves

  bool ok() const;
  // The switch could not be made and the game must not start.
  bool blocks_play() const;
};

// Makes the live profile and campaign save the target generation's. Nothing is written when they already are.
SaveSwitchResult PrepareSaveGeneration(const std::filesystem::path& user_data_root, SaveGeneration target,
                                       const SaveFileOps& ops = {});

// A valid base-game snapshot exists, so the live saves may need Title Update 6 and still be switched back.
bool BaseSaveRestorable(const std::filesystem::path& user_data_root);

std::string DescribeSaveSwitch(const SaveSwitchResult& result);

}  // namespace crackdown
