// DLC installer: classification, the Marketplace layout, the .header file, reinstall, removal and what it leaves alone.
// Run: dlc_tests [--package FILE --expect four-play|free-for-all]  (with --package, the real package is installed too)

#include <cstring>
#include <set>
#include <string>
#include <system_error>

#include "check.h"
#include "dlc.h"
#include "stfs.h"
#include "stfs_builder.h"

namespace fs = std::filesystem;
using namespace stfs_test;
using namespace crackdown;
using content::DlcPack;

namespace {

constexpr const char* kZero = "0000000000000000";
constexpr const char* kTitle = "4D5307DC";

std::vector<Item> FourPlayItems() {
  return {
      File("Agents.txt", -1, Pattern(228, 1)),                          // 0
      File("Capabilities.txt", -1, Pattern(3, 2)),                      // 1
      File("Digest.txt", -1, Pattern(582, 3)),                          // 2
      File("FullCity_DLCAgents.dff", -1, Pattern(10396, 4)),            // 3
      Dir("Streaming", -1),                                             // 4
      Dir("XUIFrontEnd", -1),                                           // 5
      File("FullCity_DLCAgents.resblock", 4, Pattern(300000, 5)),       // 6
      File("Scene.xzp", 5, Pattern(9000, 6)),                           // 7
  };
}

std::vector<Item> FreeForAllItems() {
  return {
      File("Agents.txt", -1, Pattern(228, 11)),                         // 0
      Dir("Save", -1),                                                  // 1
      File("Digest2.txt", -1, Pattern(2778, 12)),                       // 2
      File("Street Racing.dff", -1, Pattern(126058, 13)),               // 3
      File("Cheat Mode.sav", 1, Pattern(10453, 14)),                    // 4
  };
}

BuildOptions FourPlayOptions() {
  BuildOptions options;
  options.display_name = u"Four-Play Pack";
  options.license_bits = 1;
  return options;
}

BuildOptions FreeForAllOptions() {
  BuildOptions options;
  options.display_name = u"Free-For-All Pack";
  options.license_bits = 1;
  options.license3_bits = 1;
  options.license3_flags = 1;
  return options;
}

Bytes FourPlay() { return BuildPackage(FourPlayItems(), FourPlayOptions()); }
Bytes FreeForAll() { return BuildPackage(FreeForAllItems(), FreeForAllOptions()); }

class Env {
 public:
  Env() {
    user = dir.path() / "user_data";
    game = dir.path() / "game";
    fs::create_directories(user);
    WriteBytes(game / "default.xex", Bytes{1, 2, 3});
  }
  fs::path Package(const Bytes& bytes, const char* name = "package.bin") const {
    const fs::path file = dir.path() / name;
    WriteBytes(file, bytes);
    return file;
  }
  ContentResult Install(const Bytes& bytes, bool title_update = false, DlcInfo* info = nullptr,
                        const FileOps& ops = {}) const {
    return InstallDlc(Package(bytes), user, title_update, {}, info, ops);
  }

  check::TempDir dir;
  fs::path user;
  fs::path game;
};

std::set<std::string> Listing(const fs::path& root) {
  std::set<std::string> out;
  for (const auto& e : fs::recursive_directory_iterator(root)) {
    out.insert(fs::relative(e.path(), root).generic_string());
  }
  return out;
}

std::string IdOf(const Bytes& bytes) {
  check::TempDir dir;
  WriteBytes(dir.path() / "p", bytes);
  crackdown::stfs::Package pkg;
  CHECK(pkg.Open(dir.path() / "p").ok());
  return pkg.content_id_hex();
}

void TestClassify() {
  std::printf("classification by content ID or display name\n");
  CHECK(ClassifyDlc("E4DDBC100F97B6954B84E82ED807E79B5A4314C0", "") == DlcPack::kFourPlay);
  CHECK(ClassifyDlc("e4ddbc100f97b6954b84e82ed807e79b5a4314c0", "anything") == DlcPack::kFourPlay);
  CHECK(ClassifyDlc("AA10729E62721B4EAF674486684E31CD9B94AC4F", "") == DlcPack::kFreeForAll);
  CHECK(ClassifyDlc("00", "Four-Play Pack") == DlcPack::kFourPlay);
  CHECK(ClassifyDlc("00", "  free-for-all   PACK ") == DlcPack::kFreeForAll);
  CHECK(ClassifyDlc("00", "Four-Play") == DlcPack::kUnknown);
  CHECK(ClassifyDlc("00", "") == DlcPack::kUnknown);
  CHECK(ClassifyDlc("0123456789ABCDEF0123456789ABCDEF01234567", "Some Other Pack") == DlcPack::kUnknown);
}

void TestFourPlayInstall() {
  std::printf("Four-Play installs in the Marketplace layout under the zero XUID\n");
  Env env;
  DlcInfo info;
  const Bytes bytes = FourPlay();
  const ContentResult result = env.Install(bytes, false, &info);
  CHECK(result.ok());
  const std::string id = IdOf(bytes);
  CHECK(info.content_id == id && info.pack == DlcPack::kFourPlay && info.verified);
  CHECK(info.display_name == "Four-Play Pack");
  CHECK(info.license_mask == 0);

  const fs::path folder = env.user / kZero / kTitle / "00000002" / id;
  const fs::path header = env.user / kZero / kTitle / "Headers" / "00000002" / (id + ".header");
  CHECK(folder == DlcFolder(env.user) / id);
  CHECK(header == DlcHeaderPath(env.user, id));
  CHECK(fs::is_directory(folder));
  CHECK(ReadBytes(folder / "Agents.txt") == FourPlayItems()[0].data);
  CHECK(ReadBytes(folder / "Streaming" / "FullCity_DLCAgents.resblock") == FourPlayItems()[6].data);
  CHECK(ReadBytes(folder / "XUIFrontEnd" / "Scene.xzp") == FourPlayItems()[7].data);
  CHECK(!fs::exists(env.user / "dlc-install-temp"));

  const Bytes h = ReadBytes(header);
  CHECK(h.size() == 0x148);
  CHECK(h[3] == 1 && h[7] == 2);
  CHECK(h[8] == 0 && h[9] == 'F' && h[10] == 0 && h[11] == 'o');
  CHECK(std::string(reinterpret_cast<const char*>(&h[264]), id.size()) == id);
  CHECK(h[0x140] == 0x4D && h[0x141] == 0x53 && h[0x142] == 0x07 && h[0x143] == 0xDC);

  CHECK(Listing(env.game) == std::set<std::string>{"default.xex"});
  CHECK(ReadBytes(env.game / "default.xex") == (Bytes{1, 2, 3}));
}

void TestFreeForAllNeedsUpdate() {
  std::printf("Free-For-All is identified, and refused without Title Update 6\n");
  Env env;
  const Bytes bytes = FreeForAll();
  const ContentResult refused = env.Install(bytes, false);
  CHECK(refused.status == ContentStatus::kTitleUpdateRequired);
  CHECK(Listing(env.user).empty());

  DlcInfo info;
  const ContentResult accepted = env.Install(bytes, true, &info);
  CHECK(accepted.ok());
  CHECK(info.pack == DlcPack::kFreeForAll && info.display_name == "Free-For-All Pack");
  CHECK(info.license_mask == 1);

  const std::string id = IdOf(bytes);
  const Bytes h = ReadBytes(DlcHeaderPath(env.user, id));
  CHECK(h.size() == 0x148 + 4);
  CHECK(h[0x148] == 1 && h[0x149] == 0 && h[0x14A] == 0 && h[0x14B] == 0);
  CHECK(fs::exists(DlcFolder(env.user) / id / "Save" / "Cheat Mode.sav"));
  CHECK(fs::exists(DlcFolder(env.user) / id / "Street Racing.dff"));
}

void TestRejections() {
  std::printf("packages that are not a supported DLC\n");
  Env env;
  {
    BuildOptions options = FourPlayOptions();
    options.title = 0x58410A18;
    CHECK(env.Install(BuildPackage(FourPlayItems(), options)).status == ContentStatus::kWrongTitle);
  }
  for (const uint32_t type : {uint32_t(0x000D0000), uint32_t(0x000B0000), uint32_t(0x00000001)}) {
    BuildOptions options = FourPlayOptions();
    options.content_type = type;
    CHECK(env.Install(BuildPackage(FourPlayItems(), options)).status == ContentStatus::kNotDlcPackage);
  }
  CHECK(env.Install(Bytes(1000, 1)).status == ContentStatus::kNotPackage);
  {
    BuildOptions options;
    options.display_name = u"Some Other Pack";
    CHECK(env.Install(BuildPackage(FourPlayItems(), options)).status == ContentStatus::kUnsupportedDlc);
  }
  {
    BuildOptions options = FourPlayOptions();
    CHECK(env.Install(BuildPackage({File("Agents.txt", -1, Pattern(10, 1))}, options)).status ==
          ContentStatus::kUnsupportedDlc);
  }
  CHECK(Listing(env.user).empty());
}

void TestDamagedAndUnsafe() {
  std::printf("damaged and unsafe packages change nothing\n");
  Env env;
  {
    BuildOptions options = FourPlayOptions();
    options.corrupt_data_byte_of_item = 6;
    const ContentResult result = env.Install(BuildPackage(FourPlayItems(), options));
    CHECK(result.status == ContentStatus::kDamagedPackage);
  }
  {
    BuildOptions options = FourPlayOptions();
    options.corrupt_header = true;
    CHECK(env.Install(BuildPackage(FourPlayItems(), options)).status == ContentStatus::kDamagedPackage);
  }
  {
    BuildOptions options = FourPlayOptions();
    options.truncate = 8 * 4096;
    CHECK(env.Install(BuildPackage(FourPlayItems(), options)).status == ContentStatus::kDamagedPackage);
  }
  {
    std::vector<Item> items = FourPlayItems();
    items.push_back(File("..", -1, Pattern(10, 9)));
    CHECK(env.Install(BuildPackage(items, FourPlayOptions())).status == ContentStatus::kDamagedPackage);
  }
  CHECK(Listing(env.user).empty());
}

void TestReinstallAndSideBySide() {
  std::printf("reinstall replaces the pack, and two packs live side by side\n");
  Env env;
  const Bytes four_play = FourPlay();
  CHECK(env.Install(four_play).ok());
  const std::string id = IdOf(four_play);
  WriteBytes(DlcFolder(env.user) / id / "stale.bin", Bytes{9});
  CHECK(env.Install(four_play).ok());
  CHECK(!fs::exists(DlcFolder(env.user) / id / "stale.bin"));
  CHECK(fs::exists(DlcFolder(env.user) / id / "Agents.txt"));

  const Bytes free_for_all = FreeForAll();
  CHECK(env.Install(free_for_all, true).ok());
  const std::vector<DlcInfo> listed = ListInstalledDlc(env.user);
  CHECK(listed.size() == 2);
  int four_play_count = 0;
  int ffa_count = 0;
  for (const DlcInfo& info : listed) {
    CHECK(info.verified);
    four_play_count += info.pack == DlcPack::kFourPlay;
    ffa_count += info.pack == DlcPack::kFreeForAll;
  }
  CHECK(four_play_count == 1 && ffa_count == 1);
  CHECK(fs::exists(DlcFolder(env.user) / id / "Agents.txt"));
  CHECK(!fs::exists(env.user / "dlc-install-temp"));
}

// Fails the move of a staged item into `target`, not the move that puts the previous copy back.
FileOps FailingRename(const fs::path& target, const char* staged_name, int* failures) {
  FileOps ops;
  ops.rename_attempts = 2;
  ops.retry_delay = std::chrono::milliseconds(0);
  ops.rename = [target, staged_name, failures](const fs::path& from, const fs::path& to, std::error_code& ec) {
    if (to == target && from.filename() == staged_name) {
      ++*failures;
      ec = std::make_error_code(std::errc::permission_denied);
      return;
    }
    fs::rename(from, to, ec);
  };
  return ops;
}

void TestFailedReinstallKeepsPrevious() {
  std::printf("a failed reinstall leaves the installed pack as it was\n");
  Env env;
  const Bytes four_play = FourPlay();
  CHECK(env.Install(four_play).ok());
  const std::string id = IdOf(four_play);
  const fs::path folder = DlcFolder(env.user) / id;
  WriteBytes(folder / "marker.bin", Bytes{7, 7});
  const Bytes header_before = ReadBytes(DlcHeaderPath(env.user, id));

  int failures = 0;
  CHECK(env.Install(four_play, false, nullptr, FailingRename(folder, "files", &failures)).status == ContentStatus::kWriteFailed);
  CHECK(failures == 2);
  CHECK(ReadBytes(folder / "marker.bin") == (Bytes{7, 7}));
  CHECK(fs::exists(folder / "Agents.txt"));
  CHECK(ReadBytes(DlcHeaderPath(env.user, id)) == header_before);
  CHECK(!fs::exists(env.user / "dlc-install-temp"));

  failures = 0;
  CHECK(env.Install(four_play, false, nullptr, FailingRename(DlcHeaderPath(env.user, id), "header.bin", &failures)).status ==
        ContentStatus::kWriteFailed);
  CHECK(failures == 2);
  CHECK(ReadBytes(folder / "marker.bin") == (Bytes{7, 7}));
  CHECK(ReadBytes(DlcHeaderPath(env.user, id)) == header_before);
  CHECK(ListInstalledDlc(env.user).size() == 1 && ListInstalledDlc(env.user)[0].verified);
}

void TestCancelledInstall() {
  std::printf("cancelling an install leaves nothing behind\n");
  Env env;
  const fs::path package = env.Package(FourPlay());
  int calls = 0;
  InstallHooks hooks;
  hooks.progress = [&](const InstallProgress&) { return ++calls < 4; };
  CHECK(InstallDlc(package, env.user, false, hooks).status == ContentStatus::kCancelled);
  CHECK(Listing(env.user).empty());
}

void TestNoSpace() {
  std::printf("not enough space is reported before anything is written\n");
  Env env;
  FileOps ops;
  ops.available_space = [](const fs::path&, std::error_code& ec) {
    ec.clear();
    return uintmax_t(1000);
  };
  CHECK(env.Install(FourPlay(), false, nullptr, ops).status == ContentStatus::kNoSpace);
  CHECK(Listing(env.user).empty());
}

void TestRemoval() {
  std::printf("removal takes out one pack and nothing else\n");
  Env env;
  const Bytes four_play = FourPlay();
  const Bytes free_for_all = FreeForAll();
  CHECK(env.Install(four_play).ok());
  CHECK(env.Install(free_for_all, true).ok());
  const std::string fp_id = IdOf(four_play);
  const std::string ffa_id = IdOf(free_for_all);

  WriteBytes(env.user / "E000000000000001" / "4D5307DC" / "00000001" / "save.bin", Bytes{1, 2});
  WriteBytes(env.user / "crackdown_settings.toml", Bytes{'a'});
  WriteBytes(env.user / "cache" / "x.bin", Bytes{3});
  WriteBytes(env.user / "title_update" / "4D5307DC" / "Default.xexp", Bytes{4});
  const std::set<std::string> others = [&] {
    std::set<std::string> all = Listing(env.user);
    std::set<std::string> kept;
    for (const std::string& path : all) {
      if (path.find("00000002") == std::string::npos && path.find("Headers") == std::string::npos &&
          path != "0000000000000000" && path != "0000000000000000/4D5307DC") {
        kept.insert(path);
      }
    }
    return kept;
  }();

  CHECK(RemoveDlc(env.user, env.game, fp_id).ok());
  CHECK(!fs::exists(DlcFolder(env.user) / fp_id));
  CHECK(!fs::exists(DlcHeaderPath(env.user, fp_id)));
  CHECK(fs::exists(DlcFolder(env.user) / ffa_id / "Digest2.txt"));
  CHECK(fs::exists(DlcHeaderPath(env.user, ffa_id)));
  CHECK(!fs::exists(env.user / "dlc-remove-temp"));
  CHECK(RemoveDlc(env.user, env.game, fp_id).status == ContentStatus::kNotInstalled);
  CHECK(RemoveDlc(env.user, env.game, ffa_id).ok());
  CHECK(ListInstalledDlc(env.user).empty());

  std::set<std::string> after;
  for (const std::string& path : Listing(env.user)) {
    if (path.find("00000002") == std::string::npos && path.find("Headers") == std::string::npos &&
        path != "0000000000000000" && path != "0000000000000000/4D5307DC") {
      after.insert(path);
    }
  }
  CHECK(after == others);
  CHECK(ReadBytes(env.user / "E000000000000001" / "4D5307DC" / "00000001" / "save.bin") == (Bytes{1, 2}));
  CHECK(ReadBytes(env.user / "crackdown_settings.toml") == Bytes{'a'});
  CHECK(Listing(env.game) == std::set<std::string>{"default.xex"});
}

void TestRemovalSafety() {
  std::printf("removal refuses unverified, malformed and game-folder targets\n");
  Env env;
  const Bytes four_play = FourPlay();
  CHECK(env.Install(four_play).ok());
  const std::string id = IdOf(four_play);

  CHECK(RemoveDlc(env.user, env.game, "../../..").status == ContentStatus::kUnsafeTarget);
  CHECK(RemoveDlc(env.user, env.game, "short").status == ContentStatus::kUnsafeTarget);
  CHECK(RemoveDlc(env.user, env.game, "e4ddbc100f97b6954b84e82ed807e79b5a4314c0").status ==
        ContentStatus::kUnsafeTarget);

  WriteBytes(DlcFolder(env.user) / "0123456789ABCDEF0123456789ABCDEF01234567" / "x.bin", Bytes{1});
  const auto listed = ListInstalledDlc(env.user);
  CHECK(listed.size() == 2);
  CHECK(RemoveDlc(env.user, env.game, "0123456789ABCDEF0123456789ABCDEF01234567").status == ContentStatus::kUnverified);
  CHECK(fs::exists(DlcFolder(env.user) / "0123456789ABCDEF0123456789ABCDEF01234567" / "x.bin"));

  fs::remove(DlcHeaderPath(env.user, id));
  CHECK(RemoveDlc(env.user, env.game, id).status == ContentStatus::kUnverified);
  CHECK(fs::exists(DlcFolder(env.user) / id / "Agents.txt"));

  CHECK(RemoveDlc(env.user, DlcFolder(env.user) / id / "game", id).status == ContentStatus::kUnverified);
}

void TestGameFolderInsideContent() {
  std::printf("a game folder inside the pack folder blocks its removal\n");
  Env env;
  const Bytes four_play = FourPlay();
  CHECK(env.Install(four_play).ok());
  const std::string id = IdOf(four_play);
  const fs::path inner_game = DlcFolder(env.user) / id / "Streaming";
  CHECK(RemoveDlc(env.user, inner_game, id).status == ContentStatus::kUnsafeTarget);
  CHECK(fs::exists(DlcFolder(env.user) / id / "Agents.txt"));
}

void TestLinks() {
  std::printf("a link in the content path is never followed\n");
  Env env;
  const fs::path outside = env.dir.path() / "outside";
  fs::create_directories(outside);
  WriteBytes(outside / "keep.bin", Bytes{1});
  fs::create_directories(env.user / kZero);
  std::error_code ec;
  fs::create_directory_symlink(outside, env.user / kZero / kTitle, ec);
  if (ec) {
    std::printf("  (symbolic links are not available here: %s)\n", ec.message().c_str());
    return;
  }
  CHECK(env.Install(FourPlay()).status == ContentStatus::kUnsafeTarget);
  CHECK(Listing(outside) == std::set<std::string>{"keep.bin"});
}

void TestFileNameCase() {
  std::printf("package paths are matched case-sensitively\n");
  Env env;
  std::vector<Item> items = FreeForAllItems();
  items[2] = File("digest2.txt", -1, Pattern(2778, 12));
  CHECK(env.Install(BuildPackage(items, FreeForAllOptions()), true).status == ContentStatus::kUnsupportedDlc);
  CHECK(Listing(env.user).empty());
}

void TestRealPackage(const fs::path& path, const char* expect) {
  std::printf("real package %s\n", path.filename().string().c_str());
  Env env;
  DlcInfo info;
  const ContentResult result = InstallDlc(path, env.user, true, {}, &info);
  CHECK(result.ok());
  if (!result.ok()) {
    std::printf("  %s\n", result.detail.c_str());
    return;
  }
  const bool four_play = std::strcmp(expect, "four-play") == 0;
  CHECK(info.pack == (four_play ? DlcPack::kFourPlay : DlcPack::kFreeForAll));
  CHECK(info.content_id == (four_play ? "E4DDBC100F97B6954B84E82ED807E79B5A4314C0"
                                      : "AA10729E62721B4EAF674486684E31CD9B94AC4F"));
  CHECK(info.license_mask == (four_play ? 0u : 1u));
  const std::vector<DlcInfo> listed = ListInstalledDlc(env.user);
  CHECK(listed.size() == 1 && listed[0].verified && listed[0].pack == info.pack);
  std::printf("  %s, content ID %s, licence mask %08X\n", info.display_name.c_str(), info.content_id.c_str(),
              info.license_mask);
  CHECK(RemoveDlc(env.user, env.game, info.content_id).ok());
  CHECK(ListInstalledDlc(env.user).empty());
}

}  // namespace

int main(int argc, char** argv) {
  TestClassify();
  TestFourPlayInstall();
  TestFreeForAllNeedsUpdate();
  TestRejections();
  TestDamagedAndUnsafe();
  TestReinstallAndSideBySide();
  TestFailedReinstallKeepsPrevious();
  TestCancelledInstall();
  TestNoSpace();
  TestRemoval();
  TestRemovalSafety();
  TestGameFolderInsideContent();
  TestLinks();
  TestFileNameCase();
  for (int i = 1; i + 3 < argc; ++i) {
    if (std::strcmp(argv[i], "--package") == 0 && std::strcmp(argv[i + 2], "--expect") == 0) {
      TestRealPackage(argv[i + 1], argv[i + 3]);
    }
  }
  return check::Finish("dlc_tests");
}
