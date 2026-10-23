#include "save_generation.h"

#include <array>
#include <chrono>
#include <fstream>

#include "save_compat.h"

namespace crackdown {

namespace {

namespace fs = std::filesystem;
using content::SaveCompatibility;

struct Slot {
  const char* name;
  fs::path live;
  bool directory;
};

std::array<Slot, 3> SlotsOf(const fs::path& user_data_root) {
  return {{{"profile", ProfileDirPath(user_data_root), true},
           {"container", CampaignContainerPath(user_data_root), true},
           {"header", CampaignHeaderPath(user_data_root), false}}};
}

const char* FolderName(SaveGeneration generation) {
  return generation == SaveGeneration::kBase ? "base" : "tu6";
}

SaveFileOps Complete(SaveFileOps ops) {
  if (!ops.copy_file) {
    ops.copy_file = [](const fs::path& from, const fs::path& to) {
      std::error_code ec;
      return fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec) && !ec;
    };
  }
  if (!ops.rename) {
    ops.rename = [](const fs::path& from, const fs::path& to) {
      std::error_code ec;
      fs::rename(from, to, ec);
      return !ec;
    };
  }
  if (!ops.remove_all) {
    ops.remove_all = [](const fs::path& path) {
      std::error_code ec;
      fs::remove_all(path, ec);
    };
  }
  return ops;
}

bool Exists(const fs::path& path) {
  std::error_code ec;
  return fs::symlink_status(path, ec).type() != fs::file_type::not_found;
}

// Only regular files and folders: a link, junction or special file anywhere in the tree rejects it.
bool IsPlainTree(const fs::path& path) {
  std::error_code ec;
  const fs::file_status status = fs::symlink_status(path, ec);
  if (status.type() == fs::file_type::not_found) {
    return true;
  }
  if (ec || (status.type() != fs::file_type::regular && status.type() != fs::file_type::directory)) {
    return false;
  }
  if (status.type() == fs::file_type::regular) {
    return true;
  }
  for (fs::directory_iterator it(path, ec), end; !ec && it != end; it.increment(ec)) {
    if (!IsPlainTree(it->path())) {
      return false;
    }
  }
  return !ec;
}

bool IsEmptyDirectory(const fs::path& path) {
  std::error_code ec;
  return fs::is_directory(path, ec) && fs::directory_iterator(path, ec) == fs::directory_iterator();
}

bool CopyTree(const fs::path& from, const fs::path& to, const SaveFileOps& ops) {
  std::error_code ec;
  const fs::file_status status = fs::symlink_status(from, ec);
  if (status.type() == fs::file_type::regular) {
    return ops.copy_file(from, to);
  }
  if (status.type() != fs::file_type::directory) {
    return false;
  }
  fs::create_directories(to, ec);
  if (ec) {
    return false;
  }
  for (fs::directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec)) {
    if (!CopyTree(it->path(), to / it->path().filename(), ops)) {
      return false;
    }
  }
  return !ec;
}

// Copies the three save slots of `source` into a snapshot folder; an absent folder slot becomes an empty one.
bool CopySlots(const fs::path& source_root, const fs::path& snapshot, const SaveFileOps& ops, bool source_is_live,
               const fs::path& user_data_root) {
  std::error_code ec;
  fs::create_directories(snapshot, ec);
  if (ec) {
    return false;
  }
  for (const Slot& slot : SlotsOf(user_data_root)) {
    const fs::path from = source_is_live ? slot.live : source_root / slot.name;
    const fs::path to = snapshot / slot.name;
    if (Exists(from)) {
      if (!CopyTree(from, to, ops)) {
        return false;
      }
    } else if (slot.directory) {
      fs::create_directories(to, ec);
      if (ec) {
        return false;
      }
    }
  }
  return true;
}

enum class SnapshotState { kMissing, kInvalid, kValid };

SnapshotState Validate(const fs::path& snapshot, SaveGeneration generation) {
  if (!Exists(snapshot)) {
    return SnapshotState::kMissing;
  }
  std::error_code ec;
  if (!IsPlainTree(snapshot) || !fs::is_directory(snapshot / "profile", ec) ||
      !fs::is_directory(snapshot / "container", ec)) {
    return SnapshotState::kInvalid;
  }
  const fs::path header = snapshot / "header";
  if (Exists(header) && !fs::is_regular_file(header, ec)) {
    return SnapshotState::kInvalid;
  }
  const SaveCompatibility profile = ClassifyProfileBlob(snapshot / "profile" / kProfileBlobName);
  const SaveCompatibility campaign = ClassifyCampaignSave(snapshot / "container" / kCampaignSaveName);
  if (profile == SaveCompatibility::kInvalidOrUnknown || campaign == SaveCompatibility::kInvalidOrUnknown ||
      (generation == SaveGeneration::kBase && CombineSaveCompatibility(profile, campaign) ==
                                                  SaveCompatibility::kRequiresTitleUpdate6)) {
    return SnapshotState::kInvalid;
  }
  return SnapshotState::kValid;
}

fs::path SnapshotPath(const fs::path& user_data_root, SaveGeneration generation) {
  return SaveVersionsPath(user_data_root) / FolderName(generation);
}

// Puts every live slot an interrupted switch moved aside back, discarding what replaced it.
void RollBack(const fs::path& user_data_root, const fs::path& temp, const SaveFileOps& ops) {
  for (const Slot& slot : SlotsOf(user_data_root)) {
    const fs::path old = temp / (std::string("old-") + slot.name);
    if (Exists(old) || Exists(temp / (std::string("absent-") + slot.name))) {
      ops.remove_all(slot.live);
      if (Exists(old)) {
        ops.rename(old, slot.live);
      }
    }
  }
  for (const SaveGeneration generation : {SaveGeneration::kBase, SaveGeneration::kTitleUpdate6}) {
    const fs::path old = temp / (std::string("old-snapshot-") + FolderName(generation));
    if (Exists(old) && !Exists(SnapshotPath(user_data_root, generation))) {
      ops.rename(old, SnapshotPath(user_data_root, generation));
    }
  }
}

bool Recover(const fs::path& user_data_root, const SaveFileOps& ops) {
  std::error_code ec;
  const fs::path versions = SaveVersionsPath(user_data_root);
  if (!fs::is_directory(versions, ec)) {
    return true;
  }
  for (fs::directory_iterator it(versions, ec), end; !ec && it != end; it.increment(ec)) {
    const fs::path temp = it->path();
    if (temp.filename().string().rfind(".switch-", 0) != 0) {
      continue;
    }
    if (!Exists(temp / "done")) {
      RollBack(user_data_root, temp, ops);
    }
    ops.remove_all(temp);
    if (Exists(temp)) {
      return false;
    }
  }
  return !ec;
}

// Moves the live slots aside and installs the staged ones; on failure the live saves are put back.
bool CommitLive(const fs::path& user_data_root, const fs::path& temp, const SaveFileOps& ops) {
  for (const Slot& slot : SlotsOf(user_data_root)) {
    const fs::path staged = temp / "stage" / slot.name;
    const fs::path old = temp / (std::string("old-") + slot.name);
    bool moved = true;
    if (Exists(slot.live)) {
      moved = ops.rename(slot.live, old);
    } else {
      std::ofstream(temp / (std::string("absent-") + slot.name)).put('\n');
    }
    std::error_code ec;
    const bool install = Exists(staged) && !IsEmptyDirectory(staged);
    if (moved && install) {
      fs::create_directories(slot.live.parent_path(), ec);
      moved = !ec && ops.rename(staged, slot.live);
    }
    if (!moved) {
      RollBack(user_data_root, temp, ops);
      return false;
    }
  }
  return true;
}

// Anything an interrupted or failed switch moved aside is kept for the next start to put back.
bool HoldsOriginals(const fs::path& temp) {
  std::error_code ec;
  for (fs::directory_iterator it(temp, ec), end; !ec && it != end; it.increment(ec)) {
    if (it->path().filename().string().rfind("old-", 0) == 0) {
      return true;
    }
  }
  return false;
}

SaveSwitchResult Fail(SaveSwitchResult result, SaveSwitchStatus status) {
  result.status = status;
  return result;
}

}  // namespace

bool SaveSwitchResult::ok() const {
  return status == SaveSwitchStatus::kUnchanged || status == SaveSwitchStatus::kSwitched ||
         status == SaveSwitchStatus::kLiveUnknown;
}

bool SaveSwitchResult::blocks_play() const {
  return status == SaveSwitchStatus::kSnapshotInvalid || status == SaveSwitchStatus::kFailed;
}

SaveGeneration GenerationFor(content::GuestBuild build) {
  return build == content::GuestBuild::kTitleUpdate6 ? SaveGeneration::kTitleUpdate6 : SaveGeneration::kBase;
}

const char* SaveGenerationName(SaveGeneration generation) {
  return generation == SaveGeneration::kBase ? "base" : "TU6";
}

bool BaseSaveRestorable(const fs::path& user_data_root) {
  return Validate(SnapshotPath(user_data_root, SaveGeneration::kBase), SaveGeneration::kBase) == SnapshotState::kValid;
}

SaveSwitchResult PrepareSaveGeneration(const fs::path& user_data_root, SaveGeneration target, const SaveFileOps& custom) {
  const SaveFileOps ops = Complete(custom);
  SaveSwitchResult result;
  result.to = target;
  if (!Recover(user_data_root, ops)) {
    return Fail(result, SaveSwitchStatus::kFailed);
  }

  const SaveCompatibility live = CheckSaves(user_data_root).combined;
  if (live == SaveCompatibility::kInvalidOrUnknown) {
    return Fail(result, SaveSwitchStatus::kLiveUnknown);
  }
  result.from = live == SaveCompatibility::kRequiresTitleUpdate6 ? SaveGeneration::kTitleUpdate6 : SaveGeneration::kBase;
  if (result.from == target) {
    return result;
  }

  const fs::path target_snapshot = SnapshotPath(user_data_root, target);
  const SnapshotState target_state = Validate(target_snapshot, target);
  if (target_state == SnapshotState::kInvalid) {
    return Fail(result, SaveSwitchStatus::kSnapshotInvalid);
  }
  const bool empty_base = target_state == SnapshotState::kMissing && target == SaveGeneration::kBase;
  for (const Slot& slot : SlotsOf(user_data_root)) {
    if (!IsPlainTree(slot.live)) {
      return Fail(result, SaveSwitchStatus::kFailed);
    }
  }

  const fs::path versions = SaveVersionsPath(user_data_root);
  const fs::path temp =
      versions / (".switch-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code ec;
  fs::create_directories(temp, ec);
  if (ec) {
    return Fail(result, SaveSwitchStatus::kFailed);
  }
  const auto abort = [&](SaveSwitchStatus status) {
    if (!HoldsOriginals(temp)) {
      ops.remove_all(temp);
    }
    return Fail(result, status);
  };

  const bool restore = target_state == SnapshotState::kValid || empty_base;
  if (target_state == SnapshotState::kValid &&
      (!CopySlots(target_snapshot, temp / "stage", ops, false, user_data_root) ||
       Validate(temp / "stage", target) != SnapshotState::kValid)) {
    return abort(SaveSwitchStatus::kFailed);
  }
  if (empty_base && !CopySlots(temp / "none", temp / "empty", ops, false, user_data_root)) {
    return abort(SaveSwitchStatus::kFailed);
  }
  if (!CopySlots({}, temp / "preserve", ops, true, user_data_root) ||
      Validate(temp / "preserve", result.from) != SnapshotState::kValid) {
    return abort(SaveSwitchStatus::kFailed);
  }

  const fs::path from_snapshot = SnapshotPath(user_data_root, result.from);
  const fs::path old_snapshot = temp / (std::string("old-snapshot-") + FolderName(result.from));
  if (Exists(from_snapshot) && !ops.rename(from_snapshot, old_snapshot)) {
    return abort(SaveSwitchStatus::kFailed);
  }
  if (!ops.rename(temp / "preserve", from_snapshot)) {
    if (Exists(old_snapshot)) {
      ops.rename(old_snapshot, from_snapshot);
    }
    return abort(SaveSwitchStatus::kFailed);
  }
  result.preserved_snapshot = true;

  if (empty_base) {
    result.created_empty_snapshot = true;
    if (!ops.rename(temp / "empty", target_snapshot)) {
      return abort(SaveSwitchStatus::kFailed);
    }
  }
  if (restore) {
    if (!CommitLive(user_data_root, temp, ops)) {
      if (empty_base) {
        ops.remove_all(target_snapshot);
      }
      return abort(SaveSwitchStatus::kFailed);
    }
    result.restored_snapshot = true;
  }
  std::ofstream(temp / "done").put('\n');
  ops.remove_all(temp);
  result.status = SaveSwitchStatus::kSwitched;
  return result;
}

std::string DescribeSaveSwitch(const SaveSwitchResult& result) {
  switch (result.status) {
    case SaveSwitchStatus::kSnapshotInvalid:
      return std::string("The saved ") + SaveGenerationName(result.to) +
             " progress cannot be restored, so your saves were left unchanged.";
    case SaveSwitchStatus::kFailed:
      return "Your save data could not be switched to this game version. Your saves were left unchanged.";
    case SaveSwitchStatus::kLiveUnknown:
    case SaveSwitchStatus::kUnchanged:
    case SaveSwitchStatus::kSwitched:
      break;
  }
  return {};
}

}  // namespace crackdown
