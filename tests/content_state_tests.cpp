// What the launcher and the start-up guest selection read from the installed content.

#include <cstring>
#include <string>

#include "check.h"
#include "content_state.h"
#include "sha256.h"
#include "startup_selection.h"
#include "stfs_builder.h"

namespace fs = std::filesystem;
using namespace stfs_test;
using namespace crackdown;
using content::DlcPack;
using content::GuestBuild;

namespace {

Bytes Xexp() {
  Bytes data = Pattern(6000, 1);
  std::memcpy(data.data(), "XEX2", 4);
  PutBe32(data, 0x14, 1);
  PutBe32(data, 0x18, 0x000005FF);
  PutBe32(data, 0x1C, 0x100);
  PutBe32(data, 0x100, 0x2394);
  PutBe32(data, 0x104, 0x601);
  PutBe32(data, 0x108, 0x001);
  return data;
}

TitleUpdateDescriptor Update() {
  const Bytes xexp = Xexp();
  Sha256 hash;
  hash.Update(xexp.data(), xexp.size());
  return {xexp.size(), hash.Finish(), 0x601, 0x001};
}

Bytes UpdatePackage() {
  BuildOptions options;
  options.content_type = content::kTitleUpdateContentType;
  options.media_id = content::kMediaId;
  return BuildPackage({File("Default.xexp", -1, Xexp()), Dir("Local", -1), Dir("en-US", 1),
                       File("GameStrings_DLC.str", 2, Pattern(500, 3))},
                      options);
}

Bytes FourPlayPackage() {
  BuildOptions options;
  options.display_name = u"Four-Play Pack";
  return BuildPackage({File("Agents.txt", -1, Pattern(100, 1)), Dir("Streaming", -1),
                       File("FullCity_DLCAgents.resblock", 1, Pattern(5000, 2))},
                      options);
}

Bytes FreeForAllPackage() {
  BuildOptions options;
  options.display_name = u"Free-For-All Pack";
  options.license3_bits = 1;
  options.license3_flags = 1;
  return BuildPackage({File("Digest2.txt", -1, Pattern(100, 1)), File("Agents.txt", -1, Pattern(100, 2))}, options);
}

struct Env {
  Env() { fs::create_directories(user); }
  fs::path Write(const Bytes& bytes) const {
    const fs::path file = dir.path() / "package.bin";
    WriteBytes(file, bytes);
    return file;
  }
  bool InstallDlcPack(const Bytes& bytes, bool update_usable) const {
    return InstallDlc(Write(bytes), user, update_usable).ok();
  }
  bool InstallUpdate() const { return InstallTitleUpdate(Write(UpdatePackage()), user, {}, Update()).ok(); }
  ContentSnapshot Read(bool build_available = true) const { return ReadContentSnapshot(user, build_available, Update()); }

  check::TempDir dir;
  fs::path user = dir.path() / "user_data";
};

void TestNothingInstalled() {
  std::printf("nothing installed: base build, both rows offer their install\n");
  Env env;
  const ContentSnapshot snapshot = env.Read();
  const content::ContentState state = snapshot.State();
  CHECK(snapshot.title_update.state == TitleUpdateState::kNotInstalled);
  CHECK(!state.title_update_usable && !state.title_update_installed && !state.free_for_all_installed);
  CHECK(content::SelectGuestBuild(state) == GuestBuild::kBase);
  CHECK(content::CanInstallDlc(DlcPack::kFourPlay, state));
  CHECK(!content::CanInstallDlc(DlcPack::kFreeForAll, state));
  CHECK(snapshot.Find(DlcPack::kFourPlay) == nullptr);
}

void TestFourPlayWithoutUpdate() {
  std::printf("Four-Play is available and used without Title Update 6\n");
  Env env;
  CHECK(env.InstallDlcPack(FourPlayPackage(), false));
  const ContentSnapshot snapshot = env.Read();
  CHECK(snapshot.Find(DlcPack::kFourPlay) != nullptr);
  CHECK(content::SelectGuestBuild(snapshot.State()) == GuestBuild::kBase);
  CHECK(!content::UnsafeContent(snapshot.State()));
}

void TestFreeForAllFollowsUpdate() {
  std::printf("Free-For-All is enabled once Title Update 6 is usable, and then selects its build\n");
  Env env;
  CHECK(!env.InstallDlcPack(FreeForAllPackage(), false));
  CHECK(env.InstallUpdate());

  ContentSnapshot snapshot = env.Read();
  CHECK(snapshot.title_update.state == TitleUpdateState::kInstalled);
  CHECK(content::CanInstallDlc(DlcPack::kFreeForAll, snapshot.State()));
  CHECK(content::SelectGuestBuild(snapshot.State()) == GuestBuild::kTitleUpdate6);
  CHECK(env.InstallDlcPack(FreeForAllPackage(), snapshot.State().title_update_usable));

  snapshot = env.Read();
  CHECK(snapshot.Find(DlcPack::kFreeForAll) != nullptr);
  CHECK(snapshot.State().free_for_all_installed);
  CHECK(content::SelectGuestBuild(snapshot.State()) == GuestBuild::kTitleUpdate6);
  CHECK(!content::UnsafeContent(snapshot.State()));
  CHECK(!content::CanRemoveTitleUpdate(snapshot.State()));
  CHECK(RemoveTitleUpdate(env.user, env.dir.path() / "game").status == ContentStatus::kTitleUpdateInUse);
}

void TestUpdateWithoutBuild() {
  std::printf("an installed update is not usable when this release has no Title Update 6 build\n");
  Env env;
  CHECK(env.InstallUpdate());
  const ContentSnapshot snapshot = env.Read(false);
  CHECK(snapshot.State().title_update_installed && !snapshot.State().title_update_usable);
  CHECK(content::SelectGuestBuild(snapshot.State()) == GuestBuild::kBase);
  CHECK(!content::CanInstallDlc(DlcPack::kFreeForAll, snapshot.State()));
}

void TestPartialUpdateNotActive() {
  std::printf("a partial or stale update does not select Title Update 6\n");
  Env env;
  CHECK(env.InstallUpdate());
  const fs::path folder = TitleUpdateFolder(env.user);
  fs::remove(folder / "Local" / "en-US" / "GameStrings_DLC.str");
  const ContentSnapshot snapshot = env.Read();
  CHECK(snapshot.title_update.state == TitleUpdateState::kDamaged);
  CHECK(content::SelectGuestBuild(snapshot.State()) == GuestBuild::kBase);
  CHECK(!snapshot.State().title_update_usable);
  CHECK(!content::CanInstallDlc(DlcPack::kFreeForAll, snapshot.State()));
}

void TestFreeForAllWithoutUpdateIsUnsafe() {
  std::printf("Free-For-All left without a usable update is flagged, even when it cannot be verified\n");
  Env env;
  fs::create_directories(DlcFolder(env.user) / "AA10729E62721B4EAF674486684E31CD9B94AC4F");
  const ContentSnapshot snapshot = env.Read();
  CHECK(snapshot.Installed(DlcPack::kFreeForAll));
  CHECK(snapshot.Find(DlcPack::kFreeForAll) == nullptr);
  CHECK(content::UnsafeContent(snapshot.State()));
}

ContentResult Dlc(const Env& env, const Bytes& bytes, DlcPack expected, bool usable) {
  return InstallDlc(env.Write(bytes), env.user, usable, {}, nullptr, {}, expected);
}

ContentResult UpdateFile(const Env& env, const Bytes& bytes) {
  return InstallTitleUpdate(env.Write(bytes), env.user, {}, Update());
}

void ExpectWrong(const ContentResult& result, ContentKind expected, ContentKind actual) {
  CHECK(result.status == ContentStatus::kWrongPackage);
  CHECK(result.expected == expected && result.actual == actual);
}

void TestExpectedPackage() {
  std::printf("a button installs only its own package, before any pack rule applies\n");
  Env env;
  CHECK(Dlc(env, FourPlayPackage(), DlcPack::kFourPlay, false).ok());

  const ContentResult bug = Dlc(env, FreeForAllPackage(), DlcPack::kFourPlay, false);
  CHECK(bug.status != ContentStatus::kTitleUpdateRequired);
  ExpectWrong(bug, ContentKind::kFourPlay, ContentKind::kFreeForAll);
  ExpectWrong(Dlc(env, UpdatePackage(), DlcPack::kFourPlay, false), ContentKind::kFourPlay, ContentKind::kTitleUpdate);

  ExpectWrong(Dlc(env, FourPlayPackage(), DlcPack::kFreeForAll, true), ContentKind::kFreeForAll, ContentKind::kFourPlay);
  ExpectWrong(Dlc(env, UpdatePackage(), DlcPack::kFreeForAll, true), ContentKind::kFreeForAll, ContentKind::kTitleUpdate);
  CHECK(Dlc(env, FreeForAllPackage(), DlcPack::kFreeForAll, true).ok());

  ExpectWrong(UpdateFile(env, FourPlayPackage()), ContentKind::kTitleUpdate, ContentKind::kFourPlay);
  ExpectWrong(UpdateFile(env, FreeForAllPackage()), ContentKind::kTitleUpdate, ContentKind::kFreeForAll);
  CHECK(UpdateFile(env, UpdatePackage()).ok());

  CHECK(DescribeContent(bug).text == "The selected package is Free-For-All Pack, not Four-Play Pack.");
}

void TestGenericRouting() {
  std::printf("drag and drop identifies the package and applies its own rules\n");
  Env env;
  CHECK(Dlc(env, FourPlayPackage(), DlcPack::kUnknown, false).ok());
  CHECK(Dlc(env, FreeForAllPackage(), DlcPack::kUnknown, false).status == ContentStatus::kTitleUpdateRequired);
  CHECK(UpdateFile(env, UpdatePackage()).ok());
}

// The selection a start makes: the port folder is the Env's folder, so the portable user data is Env::user.
StartupSelection Start(const Env& env, bool guest_linked = true, const fs::path& explicit_root = {}) {
  return SelectStartup({env.dir.path(), explicit_root}, guest_linked, {}, Update());
}

void TestStartupSelectsFromTheInstalledContent() {
  std::printf("a start selects the guest from the installed content, before any runtime exists\n");
  Env env;
  StartupSelection start = Start(env);
  CHECK(start.user_data.outcome == UserDataOutcome::kPortable && start.user_data.root == env.user);
  CHECK(start.build == GuestBuild::kBase);

  CHECK(env.InstallUpdate());
  start = Start(env);
  CHECK(start.build == GuestBuild::kTitleUpdate6 && start.content.title_update_usable);
  CHECK(Start(env, false).build == GuestBuild::kBase);

  CHECK(env.InstallDlcPack(FreeForAllPackage(), true));
  start = Start(env);
  CHECK(start.build == GuestBuild::kTitleUpdate6 && start.content.free_for_all_installed);
  CHECK(!content::UnsafeContent(start.content));
}

void TestStartupWithoutAUsableUpdate() {
  std::printf("without a usable update the base guest is selected, and Free-For-All blocks Play\n");
  Env env;
  CHECK(env.InstallUpdate());
  fs::create_directories(DlcFolder(env.user) / "AA10729E62721B4EAF674486684E31CD9B94AC4F");
  StartupSelection start = Start(env);
  CHECK(start.build == GuestBuild::kTitleUpdate6 && !content::UnsafeContent(start.content));

  start = Start(env, false);
  CHECK(start.build == GuestBuild::kBase && content::UnsafeContent(start.content));

  fs::remove(TitleUpdateFolder(env.user) / "Local" / "en-US" / "GameStrings_DLC.str");
  start = Start(env);
  CHECK(start.build == GuestBuild::kBase && content::UnsafeContent(start.content));

  fs::remove_all(TitleUpdateFolder(env.user));
  start = Start(env);
  CHECK(start.build == GuestBuild::kBase && content::UnsafeContent(start.content));
}

void TestStartupStaleUpdateFolder() {
  std::printf("an empty or half-written update folder does not select Title Update 6\n");
  Env env;
  fs::create_directories(TitleUpdateFolder(env.user));
  CHECK(Start(env).build == GuestBuild::kBase);
  WriteBytes(TitleUpdateFolder(env.user) / "Default.xexp", Pattern(64, 9));
  CHECK(Start(env).build == GuestBuild::kBase);
  CHECK(!Start(env).content.title_update_usable);
}

void TestStartupReadsTheRootTheRunUses() {
  std::printf("an explicit user data root is both where the guest is selected and where the run reads\n");
  Env env;
  const fs::path other = env.dir.path() / "elsewhere" / "user_data";
  fs::create_directories(env.user);
  CHECK(InstallTitleUpdate(env.Write(UpdatePackage()), other, {}, Update()).ok());

  StartupSelection start = Start(env, true, other);
  CHECK(start.user_data.outcome == UserDataOutcome::kExplicit && start.user_data.root == other);
  CHECK(start.build == GuestBuild::kTitleUpdate6);

  start = Start(env);
  CHECK(start.user_data.root == env.user && start.build == GuestBuild::kBase);

  CHECK(env.InstallUpdate());
  CHECK(fs::remove_all(TitleUpdateFolder(other)) > 0);
  CHECK(Start(env, true, other).build == GuestBuild::kBase);
  CHECK(Start(env).build == GuestBuild::kTitleUpdate6);
}

Bytes SaveFile(uint32_t version) {
  Bytes out;
  const auto put = [&out](uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
      out.push_back(uint8_t(value >> shift));
    }
  };
  for (const uint32_t type : {4u, 1u}) {
    put(0x4352414B);
    put(version);
    put(type);
    put(0);
    put(0);
  }
  return out;
}

void WriteSave(const Env& env, uint32_t version) {
  fs::create_directories(CampaignSavePath(env.user).parent_path());
  WriteBytes(CampaignSavePath(env.user), SaveFile(version));
}

void TestSaveNeedingTheUpdateStartsTheBaseGameEmpty() {
  std::printf("Title Update 6 saves without Title Update 6 are preserved and the base game starts with no saves\n");
  Env env;
  WriteSave(env, 6);
  const Bytes tu6_bytes = ReadBytes(CampaignSavePath(env.user));
  StartupSelection start = Start(env);
  CHECK(start.build == GuestBuild::kBase && start.save_switch.status == SaveSwitchStatus::kSwitched);
  CHECK(start.save_switch.created_empty_snapshot && start.save_switch.preserved_snapshot);
  CHECK(start.content.save == content::SaveCompatibility::kNoSave && !content::UnsafeContent(start.content));
  CHECK(!fs::exists(CampaignSavePath(env.user)));
  CHECK(ReadBytes(SaveVersionsPath(env.user) / "tu6" / "container" / kCampaignSaveName) == tu6_bytes);

  fs::create_directories(ProfileBlobPath(env.user).parent_path());
  WriteBytes(ProfileBlobPath(env.user), SaveFile(5));
  fs::remove_all(SaveVersionsPath(env.user) / "base");
  fs::create_directories(SaveVersionsPath(env.user) / "base" / "container");
  WriteBytes(SaveVersionsPath(env.user) / "base" / "container" / kCampaignSaveName, Pattern(40, 7));
  start = Start(env);
  CHECK(start.save_switch.status == SaveSwitchStatus::kSnapshotInvalid && start.content.save_switch_failed);
  CHECK(content::UnsafeContent(start.content));

  fs::remove_all(SaveVersionsPath(env.user));
  WriteSave(env, 6);
  CHECK(env.InstallUpdate());
  start = Start(env);
  CHECK(start.build == GuestBuild::kTitleUpdate6 && !content::UnsafeContent(start.content));
}

void TestCompatibleSaveDoesNotBlockPlay() {
  std::printf("base-compatible, absent and unknown saves do not block Play\n");
  Env env;
  CHECK(!content::UnsafeContent(Start(env).content));
  WriteSave(env, 4);
  StartupSelection start = Start(env);
  CHECK(start.content.save == content::SaveCompatibility::kBaseCompatible && !content::UnsafeContent(start.content));
  WriteBytes(CampaignSavePath(env.user), Pattern(40, 7));
  start = Start(env);
  CHECK(start.content.save == content::SaveCompatibility::kInvalidOrUnknown && !content::UnsafeContent(start.content));
}

void TestRemovingTheUpdateUnderTitleUpdate6Saves() {
  std::printf("removing the update with no base saves preserves the update's saves and reinstalling restores them\n");
  Env env;
  CHECK(env.InstallUpdate());
  WriteSave(env, 6);
  const Bytes before = ReadBytes(CampaignSavePath(env.user));

  StartupSelection start = Start(env);
  CHECK(content::RemovalSwitchesSaves(start.content) && content::CanRemoveTitleUpdate(start.content));
  CHECK(start.build == GuestBuild::kTitleUpdate6 && !content::UnsafeContent(start.content));
  CHECK(env.Read().title_update.state == TitleUpdateState::kInstalled);

  CHECK(RemoveTitleUpdate(env.user, env.dir.path() / "game").ok());
  start = Start(env);
  CHECK(start.build == GuestBuild::kBase && !content::UnsafeContent(start.content));
  CHECK(start.save_switch.created_empty_snapshot && !fs::exists(CampaignSavePath(env.user)));
  CHECK(ReadBytes(SaveVersionsPath(env.user) / "tu6" / "container" / kCampaignSaveName) == before);

  CHECK(env.InstallUpdate());
  start = Start(env);
  CHECK(start.build == GuestBuild::kTitleUpdate6 && !content::UnsafeContent(start.content));
  CHECK(ReadBytes(CampaignSavePath(env.user)) == before);
}

void TestRemovingTheUpdateSwitchesTheSavesBack() {
  std::printf("removing the update keeps both timelines: the base saves return and the update's are kept\n");
  Env env;
  WriteSave(env, 4);
  const Bytes base_bytes = ReadBytes(CampaignSavePath(env.user));
  CHECK(env.InstallUpdate());
  StartupSelection start = Start(env);
  CHECK(start.build == GuestBuild::kTitleUpdate6 && start.save_switch.status == SaveSwitchStatus::kSwitched);
  CHECK(ReadBytes(CampaignSavePath(env.user)) == base_bytes);

  WriteSave(env, 6);
  const Bytes tu6_bytes = ReadBytes(CampaignSavePath(env.user));
  CHECK(content::RemovalSwitchesSaves(env.Read().State()));

  CHECK(RemoveTitleUpdate(env.user, env.dir.path() / "game").ok());
  const content::ContentState removed = env.Read().State();
  CHECK(removed.save == content::SaveCompatibility::kRequiresTitleUpdate6 && !content::UnsafeContent(removed));
  start = Start(env);
  CHECK(start.build == GuestBuild::kBase && start.save_switch.status == SaveSwitchStatus::kSwitched);
  CHECK(start.content.save == content::SaveCompatibility::kBaseCompatible && !content::UnsafeContent(start.content));
  CHECK(ReadBytes(CampaignSavePath(env.user)) == base_bytes);
  CHECK(fs::exists(SaveVersionsPath(env.user) / "tu6" / "container" / kCampaignSaveName));

  CHECK(env.InstallUpdate());
  CHECK(fs::exists(SaveVersionsPath(env.user) / "base"));
  start = Start(env);
  CHECK(start.build == GuestBuild::kTitleUpdate6 && start.save_switch.restored_snapshot);
  CHECK(ReadBytes(CampaignSavePath(env.user)) == tu6_bytes);
  CHECK(!content::UnsafeContent(start.content));
}

void TestSaveSwitchFailureBlocksPlay() {
  std::printf("a failed save switch blocks Play and leaves the saves as they were\n");
  Env env;
  WriteSave(env, 4);
  CHECK(env.InstallUpdate());
  SaveFileOps failing;
  failing.rename = [](const fs::path&, const fs::path&) { return false; };
  const StartupSelection start = SelectStartup({env.dir.path(), {}}, true, {}, Update(), failing);
  CHECK(start.save_switch.status == SaveSwitchStatus::kFailed && start.save_switch.blocks_play());
  CHECK(start.content.save_switch_failed && content::UnsafeContent(start.content));
  CHECK(ReadBytes(CampaignSavePath(env.user)) == SaveFile(4));
}

void TestDirectStartWithSkipLauncherStillShowsTheLauncher() {
  std::printf("a failed save switch forces the launcher even when it was to be skipped\n");
  Env env;
  WriteSave(env, 6);
  SaveFileOps failing;
  failing.rename = [](const fs::path&, const fs::path&) { return false; };
  const StartupSelection start = SelectStartup({env.dir.path(), {}}, true, {}, Update(), failing);
  const bool content_ready = !content::UnsafeContent(start.content);
  CHECK(!content_ready);
  const std::string launcher = check::ReadText(fs::path(CRACKDOWN_SRC_DIR) / "launcher.h");
  CHECK(launcher.find("if (!game_ready) {") != std::string::npos);
  CHECK(check::ReadText(fs::path(CRACKDOWN_SRC_DIR) / "launcher.cpp").find("CheckAndLogGameFiles(game_dir).state == GameFiles::kReady && content_ready") !=
        std::string::npos);
  CHECK(check::ReadText(fs::path(CRACKDOWN_SRC_DIR) / "crackdown_app.h")
            .find("ShouldShowLauncher(game_data_root(), !crackdown::content::UnsafeContent(startup_.content))") !=
        std::string::npos);
}

void TestStartupWithUnwritableUserData() {
  std::printf("an unwritable portable user data folder reads nothing and selects the base guest\n");
  Env env;
  CHECK(env.InstallUpdate());
  UserDataFileOps ops;
  ops.probe_writable = [](const fs::path&, std::error_code& ec) {
    ec = std::make_error_code(std::errc::permission_denied);
    return false;
  };
  const StartupSelection start = SelectStartup({env.dir.path(), {}}, true, ops, Update());
  CHECK(start.user_data.outcome == UserDataOutcome::kNotWritable);
  CHECK(start.build == GuestBuild::kBase && !start.content.title_update_usable);
}

}  // namespace

int main() {
  TestExpectedPackage();
  TestGenericRouting();
  TestNothingInstalled();
  TestFourPlayWithoutUpdate();
  TestFreeForAllFollowsUpdate();
  TestUpdateWithoutBuild();
  TestPartialUpdateNotActive();
  TestFreeForAllWithoutUpdateIsUnsafe();
  TestStartupSelectsFromTheInstalledContent();
  TestStartupWithoutAUsableUpdate();
  TestStartupStaleUpdateFolder();
  TestStartupReadsTheRootTheRunUses();
  TestStartupWithUnwritableUserData();
  TestSaveNeedingTheUpdateStartsTheBaseGameEmpty();
  TestCompatibleSaveDoesNotBlockPlay();
  TestRemovingTheUpdateUnderTitleUpdate6Saves();
  TestRemovingTheUpdateSwitchesTheSavesBack();
  TestSaveSwitchFailureBlocksPlay();
  TestDirectStartWithSkipLauncherStillShowsTheLauncher();
  return check::Finish("content_state_tests");
}
