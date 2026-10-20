// Tests for crackdown-bundler, on a synthetic disc and fake releases built here: no game files, nothing downloaded.
// Run: crackdown_bundler_tests
//      crackdown_bundler_tests --write-image <folder> <image>            (SquashFS of a folder, for unsquashfs)
//      crackdown_bundler_tests --compare-image <image> <offset> <folder>  (reads an image, compares it with a folder)
//      crackdown_bundler_tests --list-image <image> <offset>              (every entry, with size and SHA-256)
// The two extra modes let CI check the writer against squashfs-tools and the reader against mksquashfs images.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <miniz.h>

#include "archive.h"
#include "bundle.h"
#include "fake_disc.h"
#include "game_files.h"
#include "paths.h"
#include "sha256.h"
#include "squashfs.h"

namespace fs = std::filesystem;
using namespace crackdown;
using namespace crackdown::bundler;
using fake_disc::Bytes;
using fake_disc::MakeDir;
using fake_disc::MakeFile;
using fake_disc::Pattern;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

#define CHECK_OK(result)                                                                         \
  do {                                                                                           \
    ++g_checks;                                                                                  \
    const Result result_ = (result);                                                             \
    if (!result_.ok) {                                                                           \
      ++g_failures;                                                                              \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, result_.error.c_str());              \
    }                                                                                            \
  } while (0)

void Section(const char* name) {
  std::printf("%s\n", name);
}

// ---- Files ----

void WriteBytes(const fs::path& path, const Bytes& data) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}

Bytes ReadBytes(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

Bytes Text(const std::string& text) {
  return Bytes(text.begin(), text.end());
}

class Scratch {
 public:
  Scratch() {
    static int counter = 0;
    path_ = fs::temp_directory_path() / ("crackdown-bundler-test-" + std::to_string(++counter));
    fs::remove_all(path_);
    fs::create_directories(path_);
  }
  Scratch(const Scratch&) = delete;
  Scratch& operator=(const Scratch&) = delete;
  ~Scratch() {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }
  const fs::path& path() const { return path_; }

 private:
  fs::path path_;
};

std::set<std::string> Listing(const fs::path& root) {
  std::set<std::string> out;
  for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root)) {
    out.insert(GenericUtf8(entry.path().lexically_relative(root)));
  }
  return out;
}

// ---- The synthetic game ----

constexpr size_t kBigSize = 3 * squashfs::kBlockSize + 17;

std::vector<fake_disc::Node> GameTree(const Bytes& xex) {
  return {
      MakeFile("Default.xex", xex),
      MakeDir("AUDIO", {MakeFile("a.bin", Pattern(3000, 2)), MakeDir("Deep", {MakeFile("b.bin", Pattern(10, 3))})}),
      MakeDir("Big", {MakeFile("big.bin", Pattern(kBigSize, 5))}),
      MakeFile("empty.txt", {}),
      MakeDir("$SystemUpdate", {MakeFile("su123", Pattern(4096, 6))}),
  };
}

GameDescriptor TinyGame(const Bytes& xex) {
  GameDescriptor game;
  game.xex_name = "default.xex";
  game.xex_size = xex.size();
  Sha256 hash;
  hash.Update(xex.data(), xex.size());
  game.xex_sha256 = hash.Finish();
  game.required_files = {{"default.xex", xex.size()}, {"Audio/a.bin", 3000}, {"Audio/Deep/b.bin", 10},
                         {"Big/big.bin", kBigSize}, {"empty.txt", 0}};
  game.optional_prefixes = {"$SystemUpdate/"};
  return game;
}

// ---- Fake releases ----

void WriteZipFile(const fs::path& zip, const std::map<std::string, Bytes>& files) {
  mz_zip_archive archive;
  mz_zip_zero_struct(&archive);
  mz_zip_writer_init_file(&archive, Utf8(zip).c_str(), 0);
  for (const auto& [name, data] : files) {
    mz_zip_writer_add_mem(&archive, name.c_str(), data.data(), data.size(), MZ_DEFAULT_LEVEL);
  }
  mz_zip_writer_finalize_archive(&archive);
  mz_zip_writer_end(&archive);
}

std::map<std::string, Bytes> ReadZipFile(const fs::path& zip) {
  std::map<std::string, Bytes> out;
  mz_zip_archive archive;
  mz_zip_zero_struct(&archive);
  if (!mz_zip_reader_init_file(&archive, Utf8(zip).c_str(), 0)) {
    return out;
  }
  for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&archive); ++i) {
    mz_zip_archive_file_stat stat;
    mz_zip_reader_file_stat(&archive, i, &stat);
    size_t size = 0;
    void* data = mz_zip_reader_extract_to_heap(&archive, i, &size, 0);
    out[stat.m_filename] = Bytes(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
    mz_free(data);
  }
  mz_zip_reader_end(&archive);
  return out;
}

const std::map<std::string, Bytes> kWindowsRelease = {
    {"crackdown.exe", Pattern(5000, 11)},
    {"rexruntime.dll", Pattern(3000, 12)},
    {"crackdown.toml", Text("[gpu]\n")},
    {"licenses/THIRD-PARTY-LICENSES.txt", Text("licenses\n")},
};

constexpr uint64_t kRuntimeBytes = 4096;

// A stand-in AppImage runtime: an ELF header whose section headers end at kRuntimeBytes.
Bytes FakeRuntime() {
  Bytes runtime(kRuntimeBytes, 0);
  std::memcpy(runtime.data(), "\x7F" "ELF", 4);
  runtime[4] = 2;  // 64-bit
  runtime[5] = 1;  // little-endian
  const uint64_t section_headers = kRuntimeBytes - 3 * 64;
  for (int i = 0; i < 8; ++i) {
    runtime[0x28 + i] = uint8_t(section_headers >> (8 * i));
  }
  runtime[0x3A] = 64;
  runtime[0x3C] = 3;
  return runtime;
}

squashfs::Item Blob(std::string path, Bytes data, uint16_t mode = 0644) {
  squashfs::Item item;
  item.entry = {std::move(path), false, mode, data.size(), 1000};
  item.contents = [data = std::move(data)](const squashfs::Sink& sink, std::string&) {
    return data.empty() || sink(data.data(), data.size());
  };
  return item;
}

const Bytes kAppRun = Text("#!/bin/sh\nexec \"$(dirname \"$0\")/usr/bin/crackdown\" \"$@\"\n");

void WriteFakeAppImage(const fs::path& path, std::vector<squashfs::Item> items) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  const Bytes runtime = FakeRuntime();
  out.write(reinterpret_cast<const char*>(runtime.data()), std::streamsize(runtime.size()));
  std::string error;
  if (!squashfs::Write(out, std::move(items), 1000, error)) {
    std::printf("  test bug: %s\n", error.c_str());
    std::abort();
  }
}

void WriteLinuxRelease(const fs::path& path) {
  WriteFakeAppImage(path, {Blob("AppRun", kAppRun, 0755), Blob("usr/bin/crackdown", Pattern(7000, 21), 0755),
                           Blob("usr/bin/librexruntime.so", Pattern(4000, 22)), Blob(".DirIcon", Pattern(50, 23)),
                           Blob("crackdown.desktop", Text("[Desktop Entry]\n"))});
}

// A disc, a game descriptor and fake releases in a scratch folder.
struct Site {
  Scratch scratch;
  Bytes xex = Pattern(700, 1);
  GameDescriptor game = TinyGame(xex);
  fs::path iso = scratch.path() / "disc.iso";
  fs::path windows_zip = scratch.path() / "release" / "Crackdown-v9.9.9-windows-x64.zip";
  fs::path windows_dir = scratch.path() / "release-dir";
  fs::path appimage = scratch.path() / "release" / "CrackdownRecompiled-v9.9.9-x86_64.AppImage";
  std::vector<std::string> log;

  Site() {
    fake_disc::WriteImage(iso, fake_disc::BuildImage(GameTree(xex)));
    fs::create_directories(windows_zip.parent_path());
    // Release folders and archives never contribute game files or player data.
    std::map<std::string, Bytes> zipped;
    for (const auto& [name, data] : kWindowsRelease) {
      zipped["Crackdown-v9.9.9-windows-x64/" + name] = data;
    }
    zipped["Crackdown-v9.9.9-windows-x64/game/stale.bin"] = Pattern(10, 30);
    WriteZipFile(windows_zip, zipped);
    for (const auto& [name, data] : kWindowsRelease) {
      WriteBytes(windows_dir / FromPortable(name), data);
    }
    WriteBytes(windows_dir / "user_data" / "settings.toml", Text("player"));
    WriteBytes(windows_dir / "logs" / "crackdown_001.log", Text("log"));
    WriteLinuxRelease(appimage);
  }

  Options Make(Target target, const fs::path& release, const fs::path& out) {
    Options options;
    options.iso = iso;
    options.release = release;
    options.target = target;
    options.out = out;
    options.game = &game;
    options.log = [this](const std::string& line) { log.push_back(line); };
    return options;
  }

  bool GameReady(const fs::path& dir) const { return CheckGameFiles(dir, game).state == GameFiles::kReady; }
};

// ---- SquashFS ----

void TestSquashfsRoundTrip() {
  Section("SquashFS: what the writer writes, the reader reads back");
  Scratch scratch;
  std::vector<squashfs::Item> items;
  std::map<std::string, Bytes> expected;
  const auto add = [&](const std::string& path, Bytes data, uint16_t mode = 0644) {
    expected[path] = data;
    items.push_back(Blob(path, std::move(data), mode));
  };
  add("empty", {});
  add("one", Pattern(1, 1));
  add("block", Pattern(squashfs::kBlockSize, 2));
  add("blocks/three-and-a-bit", Pattern(3 * squashfs::kBlockSize + 5, 3), 0755);
  add("a/b/c/d/deep.txt", Text("deep"));
  add(std::string(255, 'n'), Pattern(9, 4));
  // Enough entries for several listing headers and inode metadata blocks.
  for (int i = 0; i < 700; ++i) {
    add("many/file" + std::to_string(i), Pattern(size_t(i % 13), uint32_t(i)));
  }
  items.push_back({{"emptydir", true, 0700, 0, 5}, {}});

  const fs::path image = scratch.path() / "image.sqfs";
  {
    std::ofstream out(image, std::ios::binary);
    out.write("prefix", 6);
    std::string error;
    CHECK(squashfs::Write(out, items, 1234, error));
  }
  CHECK(fs::file_size(image) % 4096 == 6);

  squashfs::Reader reader;
  std::string error;
  CHECK(reader.Open(image, 6, error));
  CHECK(reader.mod_time() == 1234);
  for (const auto& [path, data] : expected) {
    const squashfs::Entry* entry = reader.Find(path);
    CHECK(entry && !entry->directory && entry->size == data.size());
    Bytes read;
    CHECK(reader.ReadFile(path, [&](const uint8_t* p, size_t n) {
      read.insert(read.end(), p, p + n);
      return true;
    }, error));
    if (read != data) {
      std::printf("  contents differ: %s\n", path.c_str());
    }
    CHECK(read == data);
  }
  const squashfs::Entry* dir = reader.Find("emptydir");
  CHECK(dir && dir->directory && dir->mode == 0700 && dir->mtime == 5);
  const squashfs::Entry* implied = reader.Find("a/b/c");
  CHECK(implied && implied->directory && implied->mode == 0755);
  CHECK(reader.Find("blocks/three-and-a-bit")->mode == 0755);
  // Folders come before what they hold, and names are sorted within a folder.
  std::vector<std::string> many;
  for (const squashfs::Entry& entry : reader.entries()) {
    if (entry.path.starts_with("many/")) {
      many.push_back(entry.path);
    }
  }
  CHECK(many.size() == 700 && std::is_sorted(many.begin(), many.end()));
  CHECK(reader.entries().size() == expected.size() + 7);  // + a, a/b, a/b/c, a/b/c/d, blocks, many, emptydir

  std::string write_error;
  std::ostringstream sink;
  std::vector<squashfs::Item> duplicate = {Blob("x", Pattern(1, 1)), Blob("x", Pattern(1, 2))};
  CHECK(!squashfs::Write(sink, duplicate, 0, write_error));
  std::vector<squashfs::Item> file_and_folder = {Blob("x", Pattern(1, 1)), Blob("x/y", Pattern(1, 2))};
  CHECK(!squashfs::Write(sink, file_and_folder, 0, write_error));
  std::vector<squashfs::Item> unsafe = {Blob("a/../b", Pattern(1, 1))};
  CHECK(!squashfs::Write(sink, unsafe, 0, write_error));
  squashfs::Item liar = Blob("short", Pattern(10, 1));
  liar.entry.size = 20;
  CHECK(!squashfs::Write(sink, {liar}, 0, write_error));
}

void TestSquashfsRejectsDamage() {
  Section("SquashFS: damaged or foreign images are refused");
  Scratch scratch;
  const fs::path image = scratch.path() / "image.sqfs";
  {
    std::ofstream out(image, std::ios::binary);
    std::string error;
    squashfs::Write(out, {Blob("a", Pattern(10, 1))}, 0, error);
  }
  std::string error;
  squashfs::Reader wrong_offset;
  CHECK(!wrong_offset.Open(image, 1, error));
  Bytes bytes = ReadBytes(image);
  bytes[28] = 3;  // version 3
  WriteBytes(scratch.path() / "v3.sqfs", bytes);
  squashfs::Reader old;
  CHECK(!old.Open(scratch.path() / "v3.sqfs", 0, error));
  bytes = ReadBytes(image);
  bytes.resize(200);
  WriteBytes(scratch.path() / "cut.sqfs", bytes);
  squashfs::Reader cut;
  CHECK(!cut.Open(scratch.path() / "cut.sqfs", 0, error));
}

void TestFindSquashfsOffset() {
  Section("AppImage: the SquashFS image starts after the runtime's section headers");
  Site site;
  uint64_t offset = 0;
  std::string error;
  CHECK(FindSquashfsOffset(site.appimage, offset, error) && offset == kRuntimeBytes);
  WriteBytes(site.scratch.path() / "not-elf", Pattern(5000, 1));
  CHECK(!FindSquashfsOffset(site.scratch.path() / "not-elf", offset, error));
  Bytes runtime_only = FakeRuntime();
  runtime_only.resize(kRuntimeBytes + 100, 0);
  WriteBytes(site.scratch.path() / "runtime-only", runtime_only);
  CHECK(!FindSquashfsOffset(site.scratch.path() / "runtime-only", offset, error));
}

// ---- Archives ----

void TestZipExtraction() {
  Section("ZIP: a single top folder is dropped, player folders and unsafe names are refused");
  Scratch scratch;
  const fs::path zip = scratch.path() / "r.zip";
  WriteZipFile(zip, {{"top/a.txt", Text("a")}, {"top/sub/b.txt", Text("b")}, {"top/logs/x.log", Text("x")}});
  std::string error;
  CHECK(ExtractZip(zip, scratch.path() / "out", {"logs"}, error));
  CHECK(Listing(scratch.path() / "out") == (std::set<std::string>{"a.txt", "sub", "sub/b.txt"}));

  const fs::path flat = scratch.path() / "flat.zip";
  WriteZipFile(flat, {{"a.txt", Text("a")}, {"sub/b.txt", Text("b")}});
  CHECK(ExtractZip(flat, scratch.path() / "flat", {}, error));
  CHECK(Listing(scratch.path() / "flat") == (std::set<std::string>{"a.txt", "sub", "sub/b.txt"}));

  for (const char* name : {"../evil.txt", "a/../../evil.txt", "C:/evil.txt", "/abs.txt", "a\\..\\..\\evil.txt"}) {
    const fs::path bad = scratch.path() / "bad.zip";
    fs::remove(bad);
    WriteZipFile(bad, {{"ok.txt", Text("ok")}, {name, Text("evil")}});
    if (ReadZipFile(bad).count(name) == 0) {
      continue;  // miniz refuses to write some of these names in the first place
    }
    if (ExtractZip(bad, scratch.path() / "bad", {}, error)) {
      std::printf("  accepted: %s\n", name);
    }
    CHECK(!ExtractZip(bad, scratch.path() / "bad", {}, error));
  }
  CHECK(!fs::exists(scratch.path() / "evil.txt"));
  CHECK(!ExtractZip(scratch.path() / "missing.zip", scratch.path() / "x", {}, error));
}

// ---- Bundles ----

void TestWindowsFolder() {
  Section("windows: a folder with the release, game\\ next to crackdown.exe and the notice");
  Site site;
  const fs::path out = site.scratch.path() / "out" / "Crackdown";
  CHECK_OK(Bundle(site.Make(Target::kWindows, site.windows_zip, out)));
  CHECK(ReadBytes(out / "crackdown.exe") == kWindowsRelease.at("crackdown.exe"));
  CHECK(ReadBytes(out / "licenses" / "THIRD-PARTY-LICENSES.txt") ==
        kWindowsRelease.at("licenses/THIRD-PARTY-LICENSES.txt"));
  CHECK(site.GameReady(out / "game"));
  CHECK(!fs::exists(out / "game" / "stale.bin"));
  CHECK(!fs::exists(out / "game" / "$SystemUpdate"));
  CHECK(fs::exists(out / "PERSONAL-USE-ONLY.txt"));
  CHECK(ReadBytes(out / "game" / "Big" / "big.bin") == Pattern(kBigSize, 5));
  CHECK(!site.log.empty());

  // From the extracted release folder: the player's own saves and logs are not copied.
  const fs::path from_dir = site.scratch.path() / "out" / "FromFolder";
  CHECK_OK(Bundle(site.Make(Target::kWindows, site.windows_dir, from_dir)));
  CHECK(site.GameReady(from_dir / "game"));
  CHECK(!fs::exists(from_dir / "user_data"));
  CHECK(!fs::exists(from_dir / "logs"));
}

void TestWindowsZip() {
  Section("windows: the same folder as a ZIP64 archive, nothing left beside it");
  Site site;
  const fs::path out = site.scratch.path() / "out" / "Crackdown-personal.zip";
  CHECK_OK(Bundle(site.Make(Target::kWindows, site.windows_dir, out)));
  const std::map<std::string, Bytes> files = ReadZipFile(out);
  CHECK(files.count("Crackdown-personal/crackdown.exe") &&
        files.at("Crackdown-personal/crackdown.exe") == kWindowsRelease.at("crackdown.exe"));
  // The installer names the executable as the game looks it up.
  CHECK(files.count("Crackdown-personal/game/default.xex") &&
        files.at("Crackdown-personal/game/default.xex") == site.xex);
  CHECK(files.count("Crackdown-personal/game/Big/big.bin") &&
        files.at("Crackdown-personal/game/Big/big.bin") == Pattern(kBigSize, 5));
  CHECK(files.count("Crackdown-personal/PERSONAL-USE-ONLY.txt"));
  CHECK(Listing(site.scratch.path() / "out") == (std::set<std::string>{"Crackdown-personal.zip"}));
}

void TestLinuxAppImage() {
  Section("linux: one AppImage with game/ inside; the original AppRun is started with that folder");
  Site site;
  const fs::path out = site.scratch.path() / "out" / "Crackdown.AppImage";
  CHECK_OK(Bundle(site.Make(Target::kLinux, site.appimage, out)));
  CHECK(Listing(site.scratch.path() / "out") == (std::set<std::string>{"Crackdown.AppImage"}));
  const Bytes runtime = FakeRuntime();
  const Bytes written = ReadBytes(out);
  CHECK(written.size() > kRuntimeBytes && std::equal(runtime.begin(), runtime.end(), written.begin()));

  uint64_t offset = 0;
  std::string error;
  CHECK(FindSquashfsOffset(out, offset, error) && offset == kRuntimeBytes);
  squashfs::Reader reader;
  CHECK(reader.Open(out, offset, error));
  const auto read = [&](const std::string& path) {
    Bytes data;
    reader.ReadFile(path, [&](const uint8_t* p, size_t n) {
      data.insert(data.end(), p, p + n);
      return true;
    }, error);
    return data;
  };
  CHECK(read("AppRun") == Text(kAppRunWrapper));
  CHECK(reader.Find("AppRun")->mode == 0755);
  CHECK(read("AppRun.release") == kAppRun);
  CHECK(reader.Find("AppRun.release")->mode == 0755);
  CHECK(read("usr/bin/crackdown") == Pattern(7000, 21));
  CHECK(read("game/default.xex") == site.xex);
  CHECK(read("game/Big/big.bin") == Pattern(kBigSize, 5));
  CHECK(reader.Find("game/empty.txt") && reader.Find("game/empty.txt")->size == 0);
  CHECK(!reader.Find("game/$SystemUpdate"));
  CHECK(reader.Find("usr/share/doc/crackdown/PERSONAL-USE-ONLY.txt"));
  // No user_data or logs inside: the program keeps them next to the AppImage, as in the published release.
  for (const squashfs::Entry& entry : reader.entries()) {
    CHECK(!entry.path.starts_with("user_data") && !entry.path.starts_with("logs"));
  }
  const std::string wrapper = kAppRunWrapper;
  CHECK(wrapper.find("--game_data_root=$HERE/game") != std::string::npos);

  // A folder holding the AppImage works as the release too.
  const fs::path again = site.scratch.path() / "again.AppImage";
  CHECK_OK(Bundle(site.Make(Target::kLinux, site.appimage.parent_path(), again)));
}

void TestLinuxFolder() {
  Section("linux: a folder with the unchanged AppImage and game/ beside it");
  Site site;
  const fs::path out = site.scratch.path() / "deck";
  CHECK_OK(Bundle(site.Make(Target::kLinux, site.appimage, out)));
  CHECK(ReadBytes(out / site.appimage.filename()) == ReadBytes(site.appimage));
  CHECK(site.GameReady(out / "game"));
  CHECK(fs::exists(out / "PERSONAL-USE-ONLY.txt"));
}

void TestRefusals() {
  Section("refusals leave nothing behind");
  Site site;
  const fs::path out_dir = site.scratch.path() / "out";
  fs::create_directories(out_dir);
  const auto fails = [&](const Options& options) {
    const Result result = Bundle(options);
    return !result.ok && !result.error.empty();
  };

  WriteBytes(site.scratch.path() / "garbage.iso", Pattern(100000, 9));
  Options garbage = site.Make(Target::kWindows, site.windows_zip, out_dir / "a");
  garbage.iso = site.scratch.path() / "garbage.iso";
  CHECK(fails(garbage));
  Options garbage_appimage = site.Make(Target::kLinux, site.appimage, out_dir / "a.AppImage");
  garbage_appimage.iso = site.scratch.path() / "garbage.iso";
  CHECK(fails(garbage_appimage));
  Options garbage_zip = site.Make(Target::kWindows, site.windows_zip, out_dir / "a.zip");
  garbage_zip.iso = site.scratch.path() / "garbage.iso";
  CHECK(fails(garbage_zip));

  // A disc of another build of the game.
  GameDescriptor other = site.game;
  other.xex_sha256[0] ^= 1;
  Options wrong = site.Make(Target::kWindows, site.windows_zip, out_dir / "b");
  wrong.game = &other;
  CHECK(fails(wrong));

  CHECK(fails(site.Make(Target::kWindows, site.scratch.path() / "missing.zip", out_dir / "c")));
  CHECK(fails(site.Make(Target::kWindows, site.appimage, out_dir / "d")));  // not a Windows release
  CHECK(fails(site.Make(Target::kLinux, site.windows_zip, out_dir / "e.AppImage")));
  CHECK(fails(site.Make(Target::kWindows, site.windows_zip, out_dir / "f.AppImage")));
  CHECK(fails(site.Make(Target::kLinux, site.appimage, out_dir / "g.zip")));
  CHECK(fails(site.Make(Target::kWindows, site.windows_zip, out_dir / "h.exe")));
  CHECK(fails(site.Make(Target::kAndroid, site.windows_zip, out_dir / "i")));
  CHECK(Listing(out_dir).empty());

  // An AppImage that is not a Crackdown Recompiled release, or one that already carries game files.
  const fs::path foreign = site.scratch.path() / "foreign" / "Other.AppImage";
  WriteFakeAppImage(foreign, {Blob("AppRun", kAppRun, 0755)});
  CHECK(fails(site.Make(Target::kLinux, foreign, out_dir / "j.AppImage")));
  const fs::path stuffed = site.scratch.path() / "stuffed" / "Stuffed.AppImage";
  WriteFakeAppImage(stuffed, {Blob("AppRun", kAppRun, 0755), Blob("usr/bin/crackdown", Pattern(5, 1), 0755),
                              Blob("game/default.xex", site.xex)});
  CHECK(fails(site.Make(Target::kLinux, stuffed, out_dir / "k.AppImage")));
  CHECK(Listing(out_dir).empty());

  // An existing output is never overwritten.
  fs::create_directories(out_dir / "exists");
  WriteBytes(out_dir / "exists" / "mine.txt", Text("mine"));
  CHECK(fails(site.Make(Target::kWindows, site.windows_zip, out_dir / "exists")));
  CHECK(ReadBytes(out_dir / "exists" / "mine.txt") == Text("mine"));
  fs::remove_all(out_dir / "exists");

  // A work folder this tool did not make is left alone; a stale one of its own is replaced.
  fs::create_directories(out_dir / "m.zip.bundler-work");
  CHECK(fails(site.Make(Target::kWindows, site.windows_zip, out_dir / "m.zip")));
  CHECK(fs::exists(out_dir / "m.zip.bundler-work"));
  WriteBytes(out_dir / "m.zip.bundler-work" / ".crackdown-bundler-work", Text("crackdown-bundler\n"));
  CHECK_OK(Bundle(site.Make(Target::kWindows, site.windows_zip, out_dir / "m.zip")));
  CHECK(Listing(out_dir) == (std::set<std::string>{"m.zip"}));
}

void TestTargetsAndFormats() {
  Section("targets and formats");
  Target target = Target::kWindows;
  CHECK(ParseTarget("Linux", target) && target == Target::kLinux);
  CHECK(ParseTarget("steamdeck", target) && target == Target::kLinux);
  CHECK(ParseTarget("android", target) && target == Target::kAndroid);
  CHECK(!ParseTarget("macos", target));
  Format format = Format::kFolder;
  std::string error;
  CHECK(FormatFor(Target::kWindows, "x/Out.ZIP", format, error) && format == Format::kZip);
  CHECK(FormatFor(Target::kWindows, "x/Out", format, error) && format == Format::kFolder);
  CHECK(FormatFor(Target::kLinux, "x/Out.appimage", format, error) && format == Format::kAppImage);
  CHECK(FormatFor(Target::kLinux, "x/Out", format, error) && format == Format::kFolder);
  CHECK(!FormatFor(Target::kAndroid, "x/Out", format, error) && error == kNoAndroidBuild);
  CHECK(!FormatFor(Target::kWindows, "x/Out.exe", format, error));
  CHECK(std::string(kPersonalUseNotice).find("personal use only") != std::string::npos);
}

// ---- Modes for CI ----

int WriteImageOfFolder(const fs::path& folder, const fs::path& image) {
  std::vector<squashfs::Item> items;
  for (const fs::directory_entry& entry : fs::recursive_directory_iterator(folder)) {
    const std::string path = GenericUtf8(entry.path().lexically_relative(folder));
    if (entry.is_directory()) {
      items.push_back({{path, true, 0755, 0, 0}, {}});
    } else {
      items.push_back(Blob(path, ReadBytes(entry.path())));
    }
  }
  std::ofstream out(image, std::ios::binary | std::ios::trunc);
  std::string error;
  if (!squashfs::Write(out, std::move(items), 0, error)) {
    std::printf("%s\n", error.c_str());
    return 1;
  }
  return 0;
}

int CompareImageWithFolder(const fs::path& image, uint64_t offset, const fs::path& folder) {
  squashfs::Reader reader;
  std::string error;
  if (!reader.Open(image, offset, error)) {
    std::printf("cannot read %s: %s\n", Utf8(image).c_str(), error.c_str());
    return 1;
  }
  std::set<std::string> seen;
  for (const squashfs::Entry& entry : reader.entries()) {
    seen.insert(entry.path);
    const fs::path on_disk = folder / FromPortable(entry.path);
    if (entry.directory) {
      CHECK(fs::is_directory(on_disk));
      continue;
    }
    Bytes data;
    CHECK(reader.ReadFile(entry.path, [&](const uint8_t* p, size_t n) {
      data.insert(data.end(), p, p + n);
      return true;
    }, error));
    if (data != ReadBytes(on_disk)) {
      std::printf("  differs: %s\n", entry.path.c_str());
    }
    CHECK(data == ReadBytes(on_disk));
  }
  CHECK(seen == Listing(folder));
  std::printf("%zu entries compared, %d failures\n", seen.size(), g_failures);
  return g_failures == 0 ? 0 : 1;
}

int ListImage(const fs::path& image, uint64_t offset) {
  squashfs::Reader reader;
  std::string error;
  if (!reader.Open(image, offset, error)) {
    std::printf("cannot read %s: %s\n", Utf8(image).c_str(), error.c_str());
    return 1;
  }
  for (const squashfs::Entry& entry : reader.entries()) {
    if (entry.directory) {
      std::printf("d %04o %s\n", entry.mode, entry.path.c_str());
      continue;
    }
    Sha256 hash;
    uint64_t size = 0;
    if (!reader.ReadFile(entry.path, [&](const uint8_t* p, size_t n) {
          hash.Update(p, n);
          size += n;
          return true;
        }, error) || size != entry.size) {
      std::printf("cannot read %s: %s\n", entry.path.c_str(), error.c_str());
      return 1;
    }
    std::printf("f %04o %12llu %s %s\n", entry.mode, static_cast<unsigned long long>(size), ToHex(hash.Finish()).c_str(),
                entry.path.c_str());
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 4 && std::string(argv[1]) == "--write-image") {
    return WriteImageOfFolder(argv[2], argv[3]);
  }
  if (argc == 4 && std::string(argv[1]) == "--list-image") {
    return ListImage(argv[2], std::strtoull(argv[3], nullptr, 10));
  }
  if (argc == 5 && std::string(argv[1]) == "--compare-image") {
    return CompareImageWithFolder(argv[2], std::strtoull(argv[3], nullptr, 10), argv[4]);
  }
  if (argc != 1) {
    std::printf("usage: crackdown_bundler_tests [--write-image <folder> <image> | "
                "--compare-image <image> <offset> <folder>]\n");
    return 2;
  }
  TestSquashfsRoundTrip();
  TestSquashfsRejectsDamage();
  TestFindSquashfsOffset();
  TestZipExtraction();
  TestTargetsAndFormats();
  TestWindowsFolder();
  TestWindowsZip();
  TestLinuxAppImage();
  TestLinuxFolder();
  TestRefusals();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
