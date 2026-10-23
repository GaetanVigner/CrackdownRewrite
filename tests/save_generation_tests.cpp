// Switching the live profile and campaign save between the base game's and Title Update 6's snapshots.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "save_compat.h"
#include "save_generation.h"

namespace fs = std::filesystem;
using namespace crackdown;
using content::SaveCompatibility;

namespace {

using Bytes = std::vector<unsigned char>;
using Tree = std::map<std::string, std::string>;

void PutBe32(Bytes& out, uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8) {
    out.push_back(static_cast<unsigned char>(value >> shift));
  }
}

Bytes Chunk(uint32_t version, uint32_t type, const std::string& payload) {
  Bytes out;
  PutBe32(out, 0x4352414B);
  PutBe32(out, version);
  PutBe32(out, type);
  uint32_t checksum = 0;
  for (const char byte : payload) {
    checksum ^= static_cast<unsigned char>(byte);
  }
  PutBe32(out, checksum);
  PutBe32(out, uint32_t(payload.size()));
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

std::string File(uint32_t version, uint32_t type, const std::string& tag) {
  Bytes bytes = Chunk(version, type, tag);
  const Bytes terminator = Chunk(version, 1, "");
  bytes.insert(bytes.end(), terminator.begin(), terminator.end());
  return std::string(bytes.begin(), bytes.end());
}

const uint32_t kBaseVersion = 4;
const uint32_t kUpdateVersion = 5;

void Put(const fs::path& file, const std::string& text) {
  check::WriteText(file, text);
}

// A live state of one generation: the two checked files, an extra file in each folder, and the container header.
void WriteLive(const fs::path& root, uint32_t version, const std::string& tag) {
  fs::remove_all(ProfileDirPath(root));
  fs::remove_all(CampaignContainerPath(root));
  Put(ProfileBlobPath(root), File(version, 21, tag));
  Put(ProfileDirPath(root) / "63E83FFE", "profile-extra-" + tag);
  Put(CampaignSavePath(root), File(version == kBaseVersion ? 4 : 6, 4, tag));
  Put(CampaignContainerPath(root) / "extra.bin", "campaign-extra-" + tag);
  Put(CampaignHeaderPath(root), "header-" + tag);
}

Tree Snapshot(const fs::path& root) {
  Tree tree;
  std::error_code ec;
  if (!fs::exists(root, ec)) {
    return tree;
  }
  for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
    tree[fs::relative(it->path(), root).generic_string()] = it->is_regular_file() ? check::ReadText(it->path()) : "<dir>";
  }
  return tree;
}

Tree LiveState(const fs::path& root) {
  Tree all;
  for (const auto& [name, path] : {std::pair<const char*, fs::path>{"profile", ProfileDirPath(root)},
                                    {"container", CampaignContainerPath(root)}}) {
    for (const auto& [file, content] : Snapshot(path)) {
      all[std::string(name) + "/" + file] = content;
    }
  }
  if (fs::exists(CampaignHeaderPath(root))) {
    all["header"] = check::ReadText(CampaignHeaderPath(root));
  }
  return all;
}

Tree SnapshotState(const fs::path& root, const char* generation) {
  Tree tree = Snapshot(SaveVersionsPath(root) / generation);
  Tree files;
  for (const auto& [name, content] : tree) {
    if (content != "<dir>") {
      files[name] = content;
    }
  }
  return files;
}

SaveGeneration Base() { return SaveGeneration::kBase; }
SaveGeneration Update() { return SaveGeneration::kTitleUpdate6; }

bool NoSwitchFolders(const fs::path& root) {
  std::error_code ec;
  if (!fs::exists(SaveVersionsPath(root), ec)) {
    return true;
  }
  for (const auto& entry : fs::directory_iterator(SaveVersionsPath(root))) {
    if (entry.path().filename().string().rfind(".switch-", 0) == 0) {
      return false;
    }
  }
  return true;
}

void TestFirstBaseToUpdate() {
  std::printf("the first switch to Title Update 6 preserves the base saves and leaves them live\n");
  check::TempDir dir;
  const fs::path root = dir.path();
  WriteLive(root, kBaseVersion, "B1");
  const Tree before = LiveState(root);
  const SaveSwitchResult result = PrepareSaveGeneration(root, Update());
  CHECK(result.status == SaveSwitchStatus::kSwitched && result.preserved_snapshot && !result.restored_snapshot);
  CHECK(result.from == SaveGeneration::kBase && result.to == SaveGeneration::kTitleUpdate6);
  CHECK(LiveState(root) == before);
  CHECK(BaseSaveRestorable(root));
  CHECK(SnapshotState(root, "base").at("profile/63E83FFF") == check::ReadText(ProfileBlobPath(root)));
  CHECK(SnapshotState(root, "base").size() == before.size());
  CHECK(NoSwitchFolders(root));
  CHECK(!fs::exists(SaveVersionsPath(root) / "tu6"));
}

void TestRoundTrip() {
  std::printf("switching back and forth keeps each generation's progress byte for byte\n");
  check::TempDir dir;
  const fs::path root = dir.path();
  WriteLive(root, kBaseVersion, "B1");
  CHECK(PrepareSaveGeneration(root, Update()).ok());

  WriteLive(root, kUpdateVersion, "T1");
  const Tree tu6_one = LiveState(root);
  SaveSwitchResult result = PrepareSaveGeneration(root, Base());
  CHECK(result.status == SaveSwitchStatus::kSwitched && result.preserved_snapshot && result.restored_snapshot);
  CHECK(CheckSaves(root).combined == SaveCompatibility::kBaseCompatible);
  CHECK(SnapshotState(root, "tu6").size() == tu6_one.size());
  CHECK(SnapshotState(root, "tu6").at("profile/63E83FFE") == "profile-extra-T1");

  WriteLive(root, kBaseVersion, "B2");
  const Tree base_two = LiveState(root);
  result = PrepareSaveGeneration(root, Update());
  CHECK(result.status == SaveSwitchStatus::kSwitched);
  CHECK(LiveState(root) == tu6_one);
  CHECK(SnapshotState(root, "base").at("container/extra.bin") == "campaign-extra-B2");

  WriteLive(root, kUpdateVersion, "T2");
  const Tree tu6_two = LiveState(root);
  CHECK(PrepareSaveGeneration(root, Base()).ok());
  CHECK(LiveState(root) == base_two);
  CHECK(PrepareSaveGeneration(root, Update()).ok());
  CHECK(LiveState(root) == tu6_two);
  CHECK(PrepareSaveGeneration(root, Base()).ok());
  CHECK(LiveState(root) == base_two);
  CHECK(NoSwitchFolders(root));
}

void TestSameGenerationWritesNothing() {
  std::printf("starting the generation that is already live copies and writes nothing\n");
  check::TempDir dir;
  const fs::path root = dir.path();
  WriteLive(root, kBaseVersion, "B1");
  const SaveSwitchResult result = PrepareSaveGeneration(root, Base());
  CHECK(result.status == SaveSwitchStatus::kUnchanged);
  CHECK(!fs::exists(SaveVersionsPath(root)));
  WriteLive(root, kUpdateVersion, "T1");
  CHECK(PrepareSaveGeneration(root, Update()).status == SaveSwitchStatus::kUnchanged);
  CHECK(!fs::exists(SaveVersionsPath(root)));
}

void TestNoSaveFirstLaunch() {
  std::printf("a first launch without saves works and the base timeline can come back\n");
  check::TempDir dir;
  const fs::path root = dir.path();
  CHECK(PrepareSaveGeneration(root, Base()).status == SaveSwitchStatus::kUnchanged);
  CHECK(!fs::exists(SaveVersionsPath(root)));
  const SaveSwitchResult first = PrepareSaveGeneration(root, Update());
  CHECK(first.status == SaveSwitchStatus::kSwitched && !first.restored_snapshot && BaseSaveRestorable(root));
  WriteLive(root, kUpdateVersion, "T1");
  const Tree tu6 = LiveState(root);
  CHECK(PrepareSaveGeneration(root, Base()).ok());
  CHECK(CheckSaves(root).combined == SaveCompatibility::kNoSave && LiveState(root).empty());
  CHECK(PrepareSaveGeneration(root, Update()).ok());
  CHECK(LiveState(root) == tu6);
}

void TestMalformedSnapshots() {
  std::printf("a malformed snapshot is not restored and the live saves stay\n");
  {
    check::TempDir dir;
    const fs::path root = dir.path();
    WriteLive(root, kBaseVersion, "B1");
    CHECK(PrepareSaveGeneration(root, Update()).ok());
    WriteLive(root, kUpdateVersion, "T1");
    Put(SaveVersionsPath(root) / "base" / "container" / kCampaignSaveName, "garbage");
    const Tree before = LiveState(root);
    const SaveSwitchResult result = PrepareSaveGeneration(root, Base());
    CHECK(result.status == SaveSwitchStatus::kSnapshotInvalid && result.blocks_play());
    CHECK(!BaseSaveRestorable(root));
    CHECK(LiveState(root) == before);
    CHECK(!fs::exists(SaveVersionsPath(root) / "tu6"));
  }
  {
    check::TempDir dir;
    const fs::path root = dir.path();
    WriteLive(root, kBaseVersion, "B1");
    CHECK(PrepareSaveGeneration(root, Update()).ok());
    WriteLive(root, kUpdateVersion, "T1");
    CHECK(PrepareSaveGeneration(root, Base()).ok());
    Put(SaveVersionsPath(root) / "tu6" / "profile" / kProfileBlobName, "garbage");
    const Tree base_snapshot = SnapshotState(root, "base");
    const Tree before = LiveState(root);
    const SaveSwitchResult result = PrepareSaveGeneration(root, Update());
    CHECK(result.status == SaveSwitchStatus::kSnapshotInvalid && result.blocks_play());
    CHECK(LiveState(root) == before);
    CHECK(SnapshotState(root, "base") == base_snapshot);
  }
  {
    check::TempDir dir;
    const fs::path root = dir.path();
    WriteLive(root, kBaseVersion, "B1");
    CHECK(PrepareSaveGeneration(root, Update()).ok());
    WriteLive(root, kUpdateVersion, "T1");
    fs::remove_all(SaveVersionsPath(root) / "base" / "profile");
    CHECK(!BaseSaveRestorable(root));
    CHECK(PrepareSaveGeneration(root, Base()).status == SaveSwitchStatus::kSnapshotInvalid);
  }
  {
    check::TempDir dir;
    const fs::path root = dir.path();
    WriteLive(root, kBaseVersion, "B1");
    Put(CampaignSavePath(root), "garbage");
    const Tree before = LiveState(root);
    CHECK(PrepareSaveGeneration(root, Update()).status == SaveSwitchStatus::kLiveUnknown);
    CHECK(PrepareSaveGeneration(root, Base()).status == SaveSwitchStatus::kLiveUnknown);
    CHECK(LiveState(root) == before && !fs::exists(SaveVersionsPath(root)));
  }
}

// Fails the copy or rename numbered `allowed`; as a crash, that one and every later one, leaving what was moved.
struct Failing {
  int allowed;
  bool crash;
  int seen = 0;
  bool Pass() {
    const int index = seen++;
    return crash ? index < allowed : index != allowed;
  }
  SaveFileOps Ops() {
    SaveFileOps ops;
    ops.copy_file = [this](const fs::path& from, const fs::path& to) {
      std::error_code ec;
      return Pass() && fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec) && !ec;
    };
    ops.rename = [this](const fs::path& from, const fs::path& to) {
      std::error_code ec;
      if (!Pass()) {
        return false;
      }
      fs::rename(from, to, ec);
      return !ec;
    };
    ops.remove_all = [this](const fs::path& path) {
      if (!crash || seen <= allowed) {
        std::error_code ec;
        fs::remove_all(path, ec);
      }
    };
    return ops;
  }
};

void RunInterrupted(SaveGeneration target, bool simulate_crash) {
  for (int allowed = 0;; ++allowed) {
    check::TempDir dir;
    const fs::path root = dir.path();
    WriteLive(root, kBaseVersion, "B1");
    CHECK(PrepareSaveGeneration(root, Update()).ok());
    WriteLive(root, kUpdateVersion, "T1");
    CHECK(PrepareSaveGeneration(root, Base()).ok());
    if (target == Update()) {
      WriteLive(root, kBaseVersion, "B2");
    } else {
      WriteLive(root, kUpdateVersion, "T2");
    }
    const Tree before = LiveState(root);
    const Tree base_snapshot = SnapshotState(root, "base");
    const Tree tu6_snapshot = SnapshotState(root, "tu6");

    Failing failing{allowed, simulate_crash};
    const SaveSwitchResult result = PrepareSaveGeneration(root, target, failing.Ops());
    if (result.ok()) {
      CHECK(failing.seen <= allowed);
      break;
    }
    CHECK(result.status == SaveSwitchStatus::kFailed);
    if (!simulate_crash) {
      CHECK(LiveState(root) == before);
    }
    // The next start finishes or rolls back whatever was interrupted, then makes the switch.
    const SaveSwitchResult again = PrepareSaveGeneration(root, target);
    CHECK(again.ok());
    const Tree expected = target == Update() ? tu6_snapshot : base_snapshot;
    Tree restored;
    for (const auto& [name, content] : expected) {
      restored[name] = content;
    }
    CHECK(LiveState(root) == restored);
    CHECK(NoSwitchFolders(root));
    CHECK(BaseSaveRestorable(root));
    if (allowed > 200) {
      CHECK(false);
      break;
    }
  }
}

void TestEmptyBaseFallback() {
  std::printf("Title Update 6 saves without a base snapshot are preserved and the base game starts empty\n");
  check::TempDir dir;
  const fs::path root = dir.path();
  WriteLive(root, kUpdateVersion, "T1");
  const Tree tu6 = LiveState(root);
  CHECK(!BaseSaveRestorable(root));

  SaveSwitchResult result = PrepareSaveGeneration(root, Base());
  CHECK(result.status == SaveSwitchStatus::kSwitched && result.ok() && !result.blocks_play());
  CHECK(result.preserved_snapshot && result.created_empty_snapshot);
  CHECK(SnapshotState(root, "tu6") == tu6);
  CHECK(LiveState(root).empty() && !fs::exists(ProfileBlobPath(root)) && !fs::exists(CampaignSavePath(root)));
  CHECK(CheckSaves(root).combined == SaveCompatibility::kNoSave);
  CHECK(BaseSaveRestorable(root) && SnapshotState(root, "base").empty());
  CHECK(fs::is_directory(SaveVersionsPath(root) / "base" / "profile"));
  CHECK(fs::is_directory(SaveVersionsPath(root) / "base" / "container"));
  CHECK(!fs::exists(CampaignHeaderPath(root)) && NoSwitchFolders(root));
  CHECK(PrepareSaveGeneration(root, Base()).status == SaveSwitchStatus::kUnchanged);

  Tree base_state;
  Tree update_state = tu6;
  for (int cycle = 1; cycle <= 2; ++cycle) {
    WriteLive(root, kBaseVersion, "B" + std::to_string(cycle));
    base_state = LiveState(root);
    result = PrepareSaveGeneration(root, Update());
    CHECK(result.status == SaveSwitchStatus::kSwitched && result.restored_snapshot && !result.created_empty_snapshot);
    CHECK(SnapshotState(root, "base") == base_state && LiveState(root) == update_state);

    WriteLive(root, kUpdateVersion, "T" + std::to_string(cycle + 1));
    update_state = LiveState(root);
    result = PrepareSaveGeneration(root, Base());
    CHECK(result.status == SaveSwitchStatus::kSwitched && !result.created_empty_snapshot);
    CHECK(SnapshotState(root, "tu6") == update_state && LiveState(root) == base_state);
  }
}

void TestInvalidBaseSnapshotIsNotReplaced() {
  std::printf("an existing malformed base snapshot blocks the switch and is not replaced by an empty one\n");
  check::TempDir dir;
  const fs::path root = dir.path();
  Put(SaveVersionsPath(root) / "base" / "profile" / kProfileBlobName, "garbage");
  fs::create_directories(SaveVersionsPath(root) / "base" / "container");
  WriteLive(root, kUpdateVersion, "T1");
  const Tree live = LiveState(root);
  const Tree snapshot = SnapshotState(root, "base");
  const SaveSwitchResult result = PrepareSaveGeneration(root, Base());
  CHECK(result.status == SaveSwitchStatus::kSnapshotInvalid && result.blocks_play());
  CHECK(LiveState(root) == live && SnapshotState(root, "base") == snapshot && !fs::exists(SaveVersionsPath(root) / "tu6"));
}

void RunEmptyBaseInterrupted(bool simulate_crash) {
  for (int allowed = 0;; ++allowed) {
    check::TempDir dir;
    const fs::path root = dir.path();
    WriteLive(root, kUpdateVersion, "T1");
    const Tree before = LiveState(root);

    Failing failing{allowed, simulate_crash};
    const SaveSwitchResult result = PrepareSaveGeneration(root, Base(), failing.Ops());
    if (result.ok()) {
      CHECK(failing.seen <= allowed);
      break;
    }
    CHECK(result.status == SaveSwitchStatus::kFailed);
    if (!simulate_crash) {
      CHECK(LiveState(root) == before);
    }
    const bool partial = LiveState(root) != before && !LiveState(root).empty();
    CHECK(simulate_crash || !partial);
    CHECK(PrepareSaveGeneration(root, Base()).ok());
    CHECK(LiveState(root).empty() && SnapshotState(root, "tu6") == before);
    CHECK(BaseSaveRestorable(root) && NoSwitchFolders(root));
    if (allowed > 200) {
      CHECK(false);
      break;
    }
  }
}

void TestFailuresKeepTheLiveSaves() {
  std::printf("a failed copy or rename leaves the live saves intact, and a crash is recovered on the next start\n");
  RunInterrupted(Update(), false);
  RunInterrupted(Base(), false);
  RunInterrupted(Update(), true);
  RunInterrupted(Base(), true);
  RunEmptyBaseInterrupted(false);
  RunEmptyBaseInterrupted(true);
}

void TestLinksAreRejected() {
  std::printf("links in the live saves or a snapshot are rejected\n");
  check::TempDir dir;
  const fs::path root = dir.path();
  WriteLive(root, kBaseVersion, "B1");
  const fs::path target = dir.path() / "outside.bin";
  Put(target, "outside");
  std::error_code ec;
  fs::create_symlink(target, ProfileDirPath(root) / "link", ec);
  if (ec) {
    std::printf("  (symbolic links are not available here: skipped)\n");
    return;
  }
  const Tree before = LiveState(root);
  CHECK(PrepareSaveGeneration(root, Update()).status == SaveSwitchStatus::kFailed);
  CHECK(LiveState(root) == before && !fs::exists(SaveVersionsPath(root) / "base"));

  fs::remove(ProfileDirPath(root) / "link");
  CHECK(PrepareSaveGeneration(root, Update()).ok());
  WriteLive(root, kUpdateVersion, "T1");
  fs::create_symlink(target, SaveVersionsPath(root) / "base" / "profile" / "link", ec);
  CHECK(!ec && !BaseSaveRestorable(root));
  CHECK(PrepareSaveGeneration(root, Base()).status == SaveSwitchStatus::kSnapshotInvalid);
}

}  // namespace

int main() {
  TestFirstBaseToUpdate();
  TestRoundTrip();
  TestSameGenerationWritesNothing();
  TestNoSaveFirstLaunch();
  TestEmptyBaseFallback();
  TestInvalidBaseSnapshotIsNotReplaced();
  TestMalformedSnapshots();
  TestFailuresKeepTheLiveSaves();
  TestLinksAreRejected();
  return check::Finish("save_generation_tests");
}
