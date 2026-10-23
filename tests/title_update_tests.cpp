// Title update installer: validation, the installed layout, transactional install and removal, and the installed check.
// Run: title_update_tests [--package FILE]  (with --package, the real Title Update 6 package is installed too)

#include <cstring>
#include <set>
#include <string>
#include <system_error>

#include "check.h"
#include "dlc.h"
#include "sha256.h"
#include "stfs_builder.h"
#include "title_update.h"

namespace fs = std::filesystem;
using namespace stfs_test;
using namespace crackdown;

namespace {

constexpr uint32_t kTargetVersion = 0x00000601;
constexpr uint32_t kSourceVersion = 0x00000001;
constexpr size_t kXexpSize = 6000;

Bytes Xexp(uint32_t target = kTargetVersion, uint32_t source = kSourceVersion, uint32_t seed = 1) {
  Bytes data = Pattern(kXexpSize, seed);
  std::memcpy(data.data(), "XEX2", 4);
  PutBe32(data, 0x14, 1);
  PutBe32(data, 0x18, 0x000005FF);
  PutBe32(data, 0x1C, 0x100);
  PutBe32(data, 0x100, 0x2394);
  PutBe32(data, 0x104, target);
  PutBe32(data, 0x108, source);
  return data;
}

TitleUpdateDescriptor DescriptorFor(const Bytes& xexp) {
  Sha256 hash;
  hash.Update(xexp.data(), xexp.size());
  return {xexp.size(), hash.Finish(), kTargetVersion, kSourceVersion};
}

struct Spec {
  Bytes xexp = Xexp();
  std::string xexp_name = "Default.xexp";
  std::string strings_name = "GameStrings_DLC.str";
  std::string en_folder = "en-US";
  bool with_xexp = true;
  bool with_en_strings = true;
  bool extra_file = false;
  uint32_t strings_seed = 20;
};

std::vector<Item> Items(const Spec& spec = {}) {
  std::vector<Item> items;
  if (spec.with_xexp) {
    items.push_back(File(spec.xexp_name, -1, spec.xexp));
  }
  items.push_back(Dir("Local", -1));
  const int local = int(items.size()) - 1;
  items.push_back(Dir(spec.en_folder, local));
  const int en = int(items.size()) - 1;
  items.push_back(Dir("de-DE", local));
  const int de = int(items.size()) - 1;
  if (spec.with_en_strings) {
    items.push_back(File(spec.strings_name, en, Pattern(4100, spec.strings_seed)));
  }
  items.push_back(File("GameStrings_DLC.str", de, Pattern(300, spec.strings_seed + 1)));
  if (spec.extra_file) {
    items.push_back(File("extra.bin", -1, Pattern(10, 5)));
  }
  return items;
}

BuildOptions UpdateOptions() {
  BuildOptions options;
  options.content_type = content::kTitleUpdateContentType;
  options.media_id = content::kMediaId;
  options.display_name = u"Crackdown Title Update #6";
  return options;
}

Bytes Package(const Spec& spec = {}, const BuildOptions& options = UpdateOptions()) {
  return BuildPackage(Items(spec), options);
}

class Env {
 public:
  Env() {
    user = dir.path() / "user_data";
    game = dir.path() / "game";
    fs::create_directories(user);
    WriteBytes(game / "default.xex", Pattern(5000, 77));
    game_xex = ReadBytes(game / "default.xex");
  }
  fs::path File(const Bytes& bytes) const {
    const fs::path file = dir.path() / "package.bin";
    WriteBytes(file, bytes);
    return file;
  }
  ContentResult Install(const Bytes& bytes, const TitleUpdateDescriptor& update, const FileOps& ops = {}) const {
    return InstallTitleUpdate(File(bytes), user, {}, update, ops);
  }
  bool GameUntouched() const {
    std::set<std::string> names;
    for (const auto& e : fs::recursive_directory_iterator(game)) {
      names.insert(fs::relative(e.path(), game).generic_string());
    }
    return names == std::set<std::string>{"default.xex"} && ReadBytes(game / "default.xex") == game_xex;
  }

  check::TempDir dir;
  fs::path user;
  fs::path game;
  Bytes game_xex;
};

std::set<std::string> Listing(const fs::path& root) {
  std::set<std::string> out;
  if (!fs::exists(root)) {
    return out;
  }
  for (const auto& e : fs::recursive_directory_iterator(root)) {
    out.insert(fs::relative(e.path(), root).generic_string());
  }
  return out;
}

std::set<std::string> Files(const fs::path& root) {
  std::set<std::string> out;
  for (const std::string& path : Listing(root)) {
    if (fs::is_regular_file(root / path)) {
      out.insert(path);
    }
  }
  return out;
}

void TestConstants() {
  std::printf("the names the update uses keep their case\n");
  CHECK(std::string(content::kBaseXexName) == "default.xex");
  CHECK(std::string(content::kUpdateXexpName) == "Default.xexp");
  CHECK(std::string(content::kUpdateStringsName) == "GameStrings_DLC.str");
  CHECK(std::string(content::kUpdateLocalFolder) == "Local");
  CHECK(content::kUpdateLanguages[2] == "en-US");
  const TitleUpdateDescriptor& real = TitleUpdate6();
  CHECK(real.xexp_size == 1853440 && real.version == 0x601 && real.base_version == 1);
  CHECK(ToHex(real.xexp_sha256) == "A0D4FE06D6A4A2FD96D18F5DE426F761FE1B8CCE5D5BC7F981D738C3519C819E");
  CHECK(content::kMediaId == 0x596F9615 && content::kTitleUpdateContentType == 0xB0000);
  const fs::path user = "user_data";
  CHECK(TitleUpdateFolder(user) == user / "title_update" / "4D5307DC");
}

void TestInstall() {
  std::printf("a valid update installs outside the game folder\n");
  Env env;
  const Bytes xexp = Xexp();
  const TitleUpdateDescriptor update = DescriptorFor(xexp);
  CHECK(CheckTitleUpdate(env.user, update).state == TitleUpdateState::kNotInstalled);
  const ContentResult result = env.Install(Package(), update);
  CHECK(result.ok());
  CHECK(CheckTitleUpdate(env.user, update).state == TitleUpdateState::kInstalled);

  const fs::path folder = env.user / "title_update" / "4D5307DC";
  CHECK(folder == TitleUpdateFolder(env.user));
  CHECK(ReadBytes(folder / "Default.xexp") == xexp);
  CHECK(fs::exists(folder / "Local" / "en-US" / "GameStrings_DLC.str"));
  CHECK(fs::exists(folder / "Local" / "de-DE" / "GameStrings_DLC.str"));
  CHECK(fs::exists(folder / "update.manifest"));
  CHECK(Listing(env.user / "title_update") ==
        (std::set<std::string>{"4D5307DC", "4D5307DC/Default.xexp", "4D5307DC/Local", "4D5307DC/Local/de-DE",
                               "4D5307DC/Local/de-DE/GameStrings_DLC.str", "4D5307DC/Local/en-US",
                               "4D5307DC/Local/en-US/GameStrings_DLC.str", "4D5307DC/update.manifest"}));
  CHECK(env.GameUntouched());
}

void TestRejections() {
  std::printf("packages that are not the supported update\n");
  Env env;
  const TitleUpdateDescriptor update = DescriptorFor(Xexp());
  {
    BuildOptions options = UpdateOptions();
    options.title = 0x58410A18;
    CHECK(env.Install(Package({}, options), update).status == ContentStatus::kWrongTitle);
  }
  {
    BuildOptions options = UpdateOptions();
    options.media_id = 0x12345678;
    CHECK(env.Install(Package({}, options), update).status == ContentStatus::kWrongMedia);
  }
  for (const uint32_t type : {uint32_t(0x00000002), uint32_t(0x000D0000)}) {
    BuildOptions options = UpdateOptions();
    options.content_type = type;
    CHECK(env.Install(Package({}, options), update).status == ContentStatus::kNotTitleUpdate);
  }
  CHECK(env.Install(Bytes(1000, 1), update).status == ContentStatus::kNotPackage);
  {
    Spec spec;
    spec.xexp = Xexp(0x00000501, kSourceVersion);
    const ContentResult result = env.Install(Package(spec), update);
    CHECK(result.status == ContentStatus::kUnsupportedUpdate);
    CHECK(result.detail.find("patches") != std::string::npos);
  }
  {
    Spec spec;
    spec.xexp = Xexp(kTargetVersion, 0x00000002);
    CHECK(env.Install(Package(spec), update).status == ContentStatus::kUnsupportedUpdate);
  }
  {
    Spec spec;
    spec.xexp = Xexp(kTargetVersion, kSourceVersion, 2);
    CHECK(env.Install(Package(spec), update).status == ContentStatus::kUnsupportedUpdate);
  }
  {
    Spec spec;
    spec.xexp = Bytes(kXexpSize, 7);
    CHECK(env.Install(Package(spec), update).status == ContentStatus::kUnsupportedUpdate);
  }
  CHECK(Files(env.user).empty());
  CHECK(env.GameUntouched());
}

void TestRequiredFiles() {
  std::printf("Default.xexp and the English strings are required, nothing else is accepted\n");
  Env env;
  const TitleUpdateDescriptor update = DescriptorFor(Xexp());
  {
    Spec spec;
    spec.with_xexp = false;
    CHECK(env.Install(Package(spec), update).status == ContentStatus::kUnsupportedUpdate);
  }
  {
    Spec spec;
    spec.with_en_strings = false;
    CHECK(env.Install(Package(spec), update).status == ContentStatus::kUnsupportedUpdate);
  }
  {
    Spec spec;
    spec.extra_file = true;
    CHECK(env.Install(Package(spec), update).status == ContentStatus::kUnsupportedUpdate);
  }
  for (const char* wrong : {"default.xexp", "DEFAULT.XEXP", "Default.xex"}) {
    Spec spec;
    spec.xexp_name = wrong;
    CHECK(env.Install(Package(spec), update).status == ContentStatus::kUnsupportedUpdate);
  }
  {
    Spec spec;
    spec.strings_name = "gamestrings_dlc.str";
    CHECK(env.Install(Package(spec), update).status == ContentStatus::kUnsupportedUpdate);
  }
  {
    Spec spec;
    spec.en_folder = "en-us";
    CHECK(env.Install(Package(spec), update).status == ContentStatus::kUnsupportedUpdate);
  }
  {
    Spec spec;
    spec.en_folder = "xx-XX";
    CHECK(env.Install(Package(spec), update).status == ContentStatus::kUnsupportedUpdate);
  }
  CHECK(Files(env.user).empty());
}

void TestDamaged() {
  std::printf("damaged and unsafe update packages change nothing\n");
  Env env;
  const TitleUpdateDescriptor update = DescriptorFor(Xexp());
  for (int item = 0; item < 5; ++item) {
    BuildOptions options = UpdateOptions();
    options.corrupt_data_byte_of_item = item;
    const std::vector<Item> items = Items();
    if (items[size_t(item)].dir || items[size_t(item)].data.empty()) {
      continue;
    }
    CHECK(env.Install(BuildPackage(items, options), update).status == ContentStatus::kDamagedPackage);
  }
  {
    BuildOptions options = UpdateOptions();
    options.corrupt_header = true;
    CHECK(env.Install(Package({}, options), update).status == ContentStatus::kDamagedPackage);
  }
  {
    BuildOptions options = UpdateOptions();
    options.truncate = 3 * 4096;
    CHECK(env.Install(Package({}, options), update).status == ContentStatus::kDamagedPackage);
  }
  {
    std::vector<Item> items = Items();
    items.push_back(File("..", -1, Pattern(10, 9)));
    CHECK(env.Install(BuildPackage(items, UpdateOptions()), update).status == ContentStatus::kDamagedPackage);
  }
  CHECK(Files(env.user).empty());
}

void TestReinstall() {
  std::printf("reinstall replaces the update, and a failure keeps the previous one\n");
  Env env;
  const TitleUpdateDescriptor update = DescriptorFor(Xexp());
  CHECK(env.Install(Package(), update).ok());
  const fs::path folder = TitleUpdateFolder(env.user);
  const Bytes strings_before = ReadBytes(folder / "Local" / "en-US" / "GameStrings_DLC.str");

  Spec other;
  other.strings_seed = 90;
  CHECK(env.Install(Package(other), update).ok());
  const Bytes strings_after = ReadBytes(folder / "Local" / "en-US" / "GameStrings_DLC.str");
  CHECK(strings_after != strings_before);
  CHECK(CheckTitleUpdate(env.user, update).state == TitleUpdateState::kInstalled);

  int failures = 0;
  FileOps ops;
  ops.rename_attempts = 2;
  ops.retry_delay = std::chrono::milliseconds(0);
  ops.rename = [&](const fs::path& from, const fs::path& to, std::error_code& ec) {
    if (to == folder && from.filename() == "staged") {
      ++failures;
      ec = std::make_error_code(std::errc::permission_denied);
      return;
    }
    fs::rename(from, to, ec);
  };
  CHECK(env.Install(Package(), update, ops).status == ContentStatus::kWriteFailed);
  CHECK(failures == 2);
  CHECK(ReadBytes(folder / "Local" / "en-US" / "GameStrings_DLC.str") == strings_after);
  CHECK(CheckTitleUpdate(env.user, update).state == TitleUpdateState::kInstalled);
  CHECK(!fs::exists(env.user / "title_update" / "install-temp"));
  CHECK(env.GameUntouched());

  int calls = 0;
  InstallHooks hooks;
  hooks.progress = [&](const InstallProgress&) { return ++calls < 3; };
  CHECK(InstallTitleUpdate(env.File(Package()), env.user, hooks, update).status == ContentStatus::kCancelled);
  CHECK(ReadBytes(folder / "Local" / "en-US" / "GameStrings_DLC.str") == strings_after);
  CHECK(!fs::exists(env.user / "title_update" / "install-temp"));

  FileOps no_space;
  no_space.available_space = [](const fs::path&, std::error_code& ec) {
    ec.clear();
    return uintmax_t(10);
  };
  CHECK(env.Install(Package(), update, no_space).status == ContentStatus::kNoSpace);
  CHECK(CheckTitleUpdate(env.user, update).state == TitleUpdateState::kInstalled);
}

void TestInstalledCheck() {
  std::printf("a stale or partial update does not count as installed\n");
  Env env;
  const Bytes xexp = Xexp();
  const TitleUpdateDescriptor update = DescriptorFor(xexp);
  CHECK(env.Install(Package(), update).ok());
  const fs::path folder = TitleUpdateFolder(env.user);

  fs::remove(folder / "Local" / "de-DE" / "GameStrings_DLC.str");
  CHECK(CheckTitleUpdate(env.user, update).state == TitleUpdateState::kDamaged);
  CHECK(env.Install(Package(), update).ok());
  CHECK(CheckTitleUpdate(env.user, update).state == TitleUpdateState::kInstalled);

  Bytes changed = xexp;
  changed[0x400] ^= 1;
  WriteBytes(folder / "Default.xexp", changed);
  CHECK(CheckTitleUpdate(env.user, update).state == TitleUpdateState::kDamaged);
  WriteBytes(folder / "Default.xexp", Bytes(10, 1));
  CHECK(CheckTitleUpdate(env.user, update).state == TitleUpdateState::kDamaged);
  CHECK(env.Install(Package(), update).ok());

  fs::remove(folder / "Default.xexp");
  CHECK(CheckTitleUpdate(env.user, update).state == TitleUpdateState::kDamaged);
  CHECK(env.Install(Package(), update).ok());

  fs::remove(folder / "update.manifest");
  CHECK(CheckTitleUpdate(env.user, update).state == TitleUpdateState::kDamaged);
  CHECK(env.Install(Package(), update).ok());

  TitleUpdateDescriptor newer = update;
  newer.version = 0x00000701;
  CHECK(CheckTitleUpdate(env.user, newer).state == TitleUpdateState::kDamaged);

  fs::remove_all(folder / "Local");
  CHECK(CheckTitleUpdate(env.user, update).state == TitleUpdateState::kDamaged);

  fs::remove_all(folder);
  CHECK(CheckTitleUpdate(env.user, update).state == TitleUpdateState::kNotInstalled);
  CHECK(CheckTitleUpdate({}, update).state == TitleUpdateState::kNotInstalled);
}

void TestRemoval() {
  std::printf("removal takes out the update and nothing else\n");
  Env env;
  const TitleUpdateDescriptor update = DescriptorFor(Xexp());
  CHECK(RemoveTitleUpdate(env.user, env.game).status == ContentStatus::kNotInstalled);
  CHECK(env.Install(Package(), update).ok());
  WriteBytes(env.user / "E000000000000001" / "4D5307DC" / "00000001" / "save.bin", Bytes{1, 2});
  WriteBytes(env.user / "crackdown_settings.toml", Bytes{'a'});
  WriteBytes(env.user / "cache" / "x.bin", Bytes{3});

  CHECK(RemoveTitleUpdate(env.user, env.game).ok());
  CHECK(!fs::exists(TitleUpdateFolder(env.user)));
  CHECK(!fs::exists(env.user / "title_update" / "remove-temp"));
  CHECK(CheckTitleUpdate(env.user, update).state == TitleUpdateState::kNotInstalled);
  CHECK(ReadBytes(env.user / "E000000000000001" / "4D5307DC" / "00000001" / "save.bin") == (Bytes{1, 2}));
  CHECK(ReadBytes(env.user / "crackdown_settings.toml") == Bytes{'a'});
  CHECK(fs::exists(env.user / "cache" / "x.bin"));
  CHECK(env.GameUntouched());

  CHECK(env.Install(Package(), update).ok());
  CHECK(RemoveTitleUpdate(env.user, TitleUpdateFolder(env.user) / "Local").status == ContentStatus::kUnsafeTarget);
  CHECK(CheckTitleUpdate(env.user, update).state == TitleUpdateState::kInstalled);
}

void TestRemovalBlockedByFreeForAll() {
  std::printf("the update cannot be removed while Free-For-All is installed\n");
  Env env;
  const TitleUpdateDescriptor update = DescriptorFor(Xexp());
  CHECK(env.Install(Package(), update).ok());

  const std::string id = "AA10729E62721B4EAF674486684E31CD9B94AC4F";
  WriteBytes(DlcFolder(env.user) / id / "Digest2.txt", Bytes{1});
  CHECK(RemoveTitleUpdate(env.user, env.game).status == ContentStatus::kTitleUpdateInUse);
  CHECK(CheckTitleUpdate(env.user, update).state == TitleUpdateState::kInstalled);

  fs::remove_all(DlcFolder(env.user) / id);
  CHECK(RemoveTitleUpdate(env.user, env.game).ok());

  WriteBytes(DlcFolder(env.user) / "E4DDBC100F97B6954B84E82ED807E79B5A4314C0" / "Agents.txt", Bytes{1});
  CHECK(env.Install(Package(), update).ok());
  CHECK(RemoveTitleUpdate(env.user, env.game).ok());
}

void TestLinks() {
  std::printf("a link where the update goes is never followed\n");
  Env env;
  const TitleUpdateDescriptor update = DescriptorFor(Xexp());
  const fs::path outside = env.dir.path() / "outside";
  fs::create_directories(outside);
  WriteBytes(outside / "keep.bin", Bytes{1});
  fs::create_directories(env.user / "title_update");
  std::error_code ec;
  fs::create_directory_symlink(outside, TitleUpdateFolder(env.user), ec);
  if (ec) {
    std::printf("  (symbolic links are not available here: %s)\n", ec.message().c_str());
    return;
  }
  CHECK(CheckTitleUpdate(env.user, update).state == TitleUpdateState::kDamaged);
  CHECK(env.Install(Package(), update).status == ContentStatus::kUnsafeTarget);
  CHECK(RemoveTitleUpdate(env.user, env.game).status == ContentStatus::kUnsafeTarget);
  CHECK(Listing(outside) == std::set<std::string>{"keep.bin"});
}

bool FileSystemIsCaseSensitive(const fs::path& dir) {
  WriteBytes(dir / "probe-case.bin", Bytes{1});
  const bool sensitive = !fs::exists(dir / "PROBE-CASE.bin");
  fs::remove(dir / "probe-case.bin");
  return sensitive;
}

void TestNamesAreCaseSensitive() {
  std::printf("the installed update uses the exact names, and a differently cased copy is not the update\n");
  Env env;
  const TitleUpdateDescriptor update = DescriptorFor(Xexp());
  CHECK(env.Install(Package(), update).ok());
  const fs::path folder = TitleUpdateFolder(env.user);
  CHECK(fs::exists(folder / "Default.xexp"));
  CHECK(fs::exists(folder / "Local" / "en-US" / "GameStrings_DLC.str"));
  if (!FileSystemIsCaseSensitive(env.dir.path())) {
    std::printf("  (this file system ignores case; the rename checks need one that does not)\n");
    return;
  }
  CHECK(!fs::exists(folder / "default.xexp"));
  CHECK(!fs::exists(folder / "local"));
  fs::rename(folder / "Default.xexp", folder / "default.xexp");
  CHECK(CheckTitleUpdate(env.user, update).state == TitleUpdateState::kDamaged);
  fs::rename(folder / "default.xexp", folder / "Default.xexp");
  CHECK(CheckTitleUpdate(env.user, update).state == TitleUpdateState::kInstalled);
  fs::rename(folder / "Local" / "en-US" / "GameStrings_DLC.str", folder / "Local" / "en-US" / "gamestrings_dlc.str");
  CHECK(CheckTitleUpdate(env.user, update).state == TitleUpdateState::kDamaged);
}

void TestRealPackage(const fs::path& path) {
  std::printf("real package %s\n", path.filename().string().c_str());
  Env env;
  const ContentResult result = InstallTitleUpdate(path, env.user);
  CHECK(result.ok());
  if (!result.ok()) {
    std::printf("  %s\n", result.detail.c_str());
    return;
  }
  CHECK(CheckTitleUpdate(env.user).state == TitleUpdateState::kInstalled);
  const fs::path folder = TitleUpdateFolder(env.user);
  CHECK(fs::file_size(folder / "Default.xexp") == 1853440);
  int languages = 0;
  for (const std::string_view language : content::kUpdateLanguages) {
    languages += fs::exists(folder / "Local" / std::string(language) / "GameStrings_DLC.str");
  }
  CHECK(languages == 13);
  std::printf("  installed %d languages\n", languages);
  CHECK(env.GameUntouched());
  CHECK(RemoveTitleUpdate(env.user, env.game).ok());
}

}  // namespace

int main(int argc, char** argv) {
  TestConstants();
  TestInstall();
  TestRejections();
  TestRequiredFiles();
  TestDamaged();
  TestReinstall();
  TestInstalledCheck();
  TestRemoval();
  TestRemovalBlockedByFreeForAll();
  TestLinks();
  TestNamesAreCaseSensitive();
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--package") == 0) {
      TestRealPackage(argv[i + 1]);
    }
  }
  return check::Finish("title_update_tests");
}
