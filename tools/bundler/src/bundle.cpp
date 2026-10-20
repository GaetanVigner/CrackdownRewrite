#include "bundle.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "archive.h"
#include "paths.h"
#include "sha256.h"
#include "squashfs.h"

namespace crackdown::bundler {

const char kPersonalUseNotice[] =
    "This package contains game files copied from your own Crackdown disc image on this computer.\n"
    "It is for your personal use only. Do not share, upload, sell or otherwise distribute it: the game files\n"
    "are copyrighted by their owners. Crackdown Recompiled itself contains no game files, and this tool\n"
    "never uploads or shares anything.\n";

const char kNoAndroidBuild[] =
    "No Android build of Crackdown Recompiled exists: the program is built for x86-64 PCs (Windows and Linux,\n"
    "including the Steam Deck) and has never been compiled for ARM64. There is nothing to package for Android.\n";

const char kAppRunWrapper[] =
    "#!/bin/sh\n"
    "# Written by crackdown-bundler: this AppImage carries the player's own game files in game/, for personal use "
    "only.\n"
    "HERE=\"$(dirname \"$(readlink -f \"$0\")\")\"\n"
    "# A restart from the launcher repeats this process's arguments, with the game folder of the previous mount.\n"
    "for arg do\n"
    "  shift\n"
    "  case \"$arg\" in\n"
    "    --game_data_root=*) ;;\n"
    "    *) set -- \"$@\" \"$arg\" ;;\n"
    "  esac\n"
    "done\n"
    "exec \"$HERE/AppRun.release\" \"--game_data_root=$HERE/game\" \"$@\"\n";

namespace {

namespace fs = std::filesystem;

constexpr char kNoticeFile[] = "PERSONAL-USE-ONLY.txt";
constexpr char kWorkMarker[] = ".crackdown-bundler-work";
constexpr uint64_t kSpaceMargin = 256ull << 20;

// Folders a player's copy of a release may hold next to the program; a package never takes them from the release.
const std::set<std::string> kPlayerFolders = {"game", "game-install-temp", "game-install-backup", "user_data",
                                              "logs", "cache"};

void Log(const Options& options, const std::string& line) {
  if (options.log) {
    options.log(line);
  }
}

Result Fail(std::string error) {
  return {false, std::move(error)};
}

uint64_t GameBytes(const GameDescriptor& game) {
  uint64_t total = 0;
  for (const GameDescriptor::File& file : game.required_files) {
    total += file.size;
  }
  return total;
}

bool WriteText(const fs::path& path, std::string_view text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(text.data(), std::streamsize(text.size()));
  out.close();
  return bool(out);
}

std::string NoticeText() {
  return std::string("Crackdown Recompiled - personal package made by crackdown-bundler\n\n") + kPersonalUseNotice;
}

// A percentage line whenever the install moves on by 5% or changes phase.
std::function<bool(const InstallProgress&)> ProgressPrinter(const Options& options) {
  auto last = std::make_shared<std::pair<std::string, int>>("", -1);
  return [&options, last](const InstallProgress& progress) {
    const int percent = progress.total_bytes ? int(progress.done_bytes * 100 / progress.total_bytes) : 100;
    if (progress.phase != last->first || percent / 5 != last->second / 5) {
      *last = {progress.phase, percent};
      Log(options, progress.phase + ": " + std::to_string(percent) + "%");
    }
    return true;
  };
}

// Removes what a failed run created, unless the run succeeded.
class Cleanup {
 public:
  void Add(fs::path path) { paths_.push_back(std::move(path)); }
  void Keep() { paths_.clear(); }
  ~Cleanup() {
    for (const fs::path& path : paths_) {
      std::error_code ec;
      fs::remove_all(path, ec);
    }
  }

 private:
  std::vector<fs::path> paths_;
};

bool Exists(const fs::path& path) {
  std::error_code ec;
  return fs::symlink_status(path, ec).type() != fs::file_type::not_found;
}

// The output must not exist yet; its parent folder is created.
bool CheckNewOutput(const fs::path& out, std::string& error) {
  if (out.empty() || !out.has_filename()) {
    error = "Give the output a name with --out.";
    return false;
  }
  if (Exists(out)) {
    error = "The output already exists: " + Utf8(out) + ". Choose a new name or remove it first.";
    return false;
  }
  std::error_code ec;
  fs::create_directories(out.parent_path().empty() ? fs::path(".") : out.parent_path(), ec);
  if (ec) {
    error = "Cannot create the folder for the output: " + Utf8(out.parent_path());
    return false;
  }
  return true;
}

// A work folder next to the output, on the same drive. A stale one from an interrupted run is recognised by its
// marker file and removed; anything else with that name is left alone.
bool MakeWorkFolder(const fs::path& out, fs::path& work, std::string& error) {
  work = out;
  work += ".bundler-work";
  std::error_code ec;
  if (Exists(work)) {
    if (!fs::is_regular_file(work / kWorkMarker, ec)) {
      error = "The work folder " + Utf8(work) + " exists and was not made by this tool. Remove it first.";
      return false;
    }
    fs::remove_all(work, ec);
    if (ec) {
      error = "Cannot remove the old work folder " + Utf8(work);
      return false;
    }
  }
  fs::create_directories(work, ec);
  if (ec || !WriteText(work / kWorkMarker, "crackdown-bundler\n")) {
    error = "Cannot create the work folder " + Utf8(work);
    return false;
  }
  return true;
}

bool CheckSpace(const fs::path& dir, uint64_t needed, std::string& error) {
  std::error_code ec;
  const fs::space_info space = fs::space(dir, ec);
  if (!ec && space.available < needed) {
    error = "Not enough disk space next to the output: " + std::to_string(needed >> 20) + " MiB needed, " +
            std::to_string(space.available >> 20) + " MiB free.";
    return false;
  }
  return true;
}

bool CopyReleaseFolder(const fs::path& from, const fs::path& to, std::string& error) {
  std::error_code ec;
  for (fs::recursive_directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec)) {
    const fs::path relative = it->path().lexically_relative(from);
    if (it.depth() == 0 && kPlayerFolders.count(LowerAscii(Utf8(relative)))) {
      it.disable_recursion_pending();
      continue;
    }
    if (it->is_symlink(ec)) {
      error = "The release folder holds a link, which is not copied: " + Utf8(relative);
      return false;
    }
    if (it->is_directory(ec)) {
      fs::create_directories(to / relative, ec);
    } else if (it->is_regular_file(ec)) {
      fs::copy_file(it->path(), to / relative, fs::copy_options::overwrite_existing, ec);
    }
    if (ec) {
      error = "Cannot copy " + Utf8(it->path());
      return false;
    }
  }
  if (ec) {
    error = "Cannot read the release folder " + Utf8(from);
    return false;
  }
  return true;
}

bool HasExtension(const fs::path& path, std::string_view extension) {
  return LowerAscii(Utf8(path.extension())) == extension;
}

// The Windows release, from its ZIP or a folder, into `dest`.
bool StageWindowsRelease(const fs::path& release, const fs::path& dest, std::string& error) {
  std::error_code ec;
  if (fs::is_regular_file(release, ec) && HasExtension(release, ".zip")) {
    if (!ExtractZip(release, dest, kPlayerFolders, error)) {
      return false;
    }
  } else if (fs::is_directory(release, ec)) {
    if (!CopyReleaseFolder(release, dest, error)) {
      return false;
    }
  } else {
    error = "The Windows release must be the release .zip or the folder extracted from it: " + Utf8(release);
    return false;
  }
  if (!fs::is_regular_file(dest / "crackdown.exe", ec)) {
    error = "This is not a Crackdown Recompiled Windows release: it has no crackdown.exe at its top.";
    return false;
  }
  return true;
}

bool FindReleaseAppImage(const fs::path& release, fs::path& appimage, std::string& error) {
  std::error_code ec;
  if (fs::is_regular_file(release, ec)) {
    appimage = release;
    return true;
  }
  if (!fs::is_directory(release, ec)) {
    error = "The Linux release must be the release .AppImage or a folder that holds it: " + Utf8(release);
    return false;
  }
  appimage.clear();
  for (const fs::directory_entry& entry : fs::directory_iterator(release, ec)) {
    if (entry.is_regular_file(ec) && HasExtension(entry.path(), ".appimage")) {
      if (!appimage.empty()) {
        error = "The release folder holds more than one AppImage: pass the one to use.";
        return false;
      }
      appimage = entry.path();
    }
  }
  if (appimage.empty()) {
    error = "No .AppImage in the release folder " + Utf8(release);
    return false;
  }
  return true;
}

Result InstallInto(const Options& options, const GameDescriptor& game, const fs::path& game_dir) {
  Log(options, "Checking and copying the game files from " + Utf8(options.iso));
  InstallHooks hooks;
  hooks.progress = ProgressPrinter(options);
  const InstallResult result = InstallGame(options.iso, game_dir, hooks, game);
  if (!result.ok()) {
    const InstallMessage message = DescribeInstall(result.status);
    return Fail(message.title + ": " + message.text + (result.detail.empty() ? "" : " (" + result.detail + ")"));
  }
  return {true, {}};
}

// ---- Linux AppImage ----

struct GameFile {
  std::string path;  // relative to the game folder, forward slashes
  fs::path source;
  uint64_t size;
};

bool ListFolder(const fs::path& root, std::vector<std::string>& folders, std::vector<GameFile>& files,
                std::string& error) {
  std::error_code ec;
  for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
    const std::string relative = GenericUtf8(it->path().lexically_relative(root));
    if (it->is_symlink(ec)) {
      error = "unexpected link in the game files: " + relative;
      return false;
    }
    if (it->is_directory(ec)) {
      folders.push_back(relative);
    } else if (it->is_regular_file(ec)) {
      files.push_back({relative, it->path(), it->file_size(ec)});
    }
    if (ec) {
      break;
    }
  }
  if (ec) {
    error = "cannot list " + Utf8(root);
    return false;
  }
  return true;
}

bool StreamFile(const fs::path& path, const squashfs::Sink& sink, std::string& error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "cannot open " + Utf8(path);
    return false;
  }
  std::vector<char> buffer(1 << 20);
  while (in) {
    in.read(buffer.data(), std::streamsize(buffer.size()));
    const size_t got = size_t(in.gcount());
    if (got > 0 && !sink(reinterpret_cast<const uint8_t*>(buffer.data()), got)) {
      error = "the file changed while it was read: " + Utf8(path);
      return false;
    }
  }
  if (!in.eof()) {
    error = "cannot read " + Utf8(path);
    return false;
  }
  return true;
}

squashfs::Item TextItem(std::string path, std::string text, uint16_t mode, uint32_t mtime) {
  squashfs::Item item;
  item.entry = {std::move(path), false, mode, text.size(), mtime};
  item.contents = [text = std::move(text)](const squashfs::Sink& sink, std::string&) {
    return sink(reinterpret_cast<const uint8_t*>(text.data()), text.size());
  };
  return item;
}

// Reads the written AppImage back: the executable must hash right and every required file must be there, full size.
bool VerifyAppImage(const fs::path& file, uint64_t offset, const GameDescriptor& game, std::string& error) {
  squashfs::Reader reader;
  if (!reader.Open(file, offset, error)) {
    return false;
  }
  std::map<std::string, const squashfs::Entry*> lower;
  for (const squashfs::Entry& entry : reader.entries()) {
    lower[LowerAscii(entry.path)] = &entry;
  }
  for (const GameDescriptor::File& required : game.required_files) {
    const auto found = lower.find("game/" + LowerAscii(required.path));
    if (found == lower.end() || found->second->directory || found->second->size != required.size) {
      error = "the written AppImage lacks game/" + required.path;
      return false;
    }
  }
  const auto xex = lower.find("game/" + LowerAscii(game.xex_name));
  if (xex == lower.end()) {
    error = "the written AppImage lacks the game executable";
    return false;
  }
  Sha256 hash;
  if (!reader.ReadFile(xex->second->path, [&](const uint8_t* data, size_t size) {
        hash.Update(data, size);
        return true;
      }, error)) {
    return false;
  }
  if (hash.Finish() != game.xex_sha256) {
    error = "the game executable in the written AppImage does not match";
    return false;
  }
  for (const char* required : {"AppRun", "AppRun.release", "usr/bin/crackdown"}) {
    if (!reader.Find(required)) {
      error = std::string("the written AppImage lacks ") + required;
      return false;
    }
  }
  return true;
}

Result BundleAppImage(const Options& options, const GameDescriptor& game) {
  std::string error;
  fs::path appimage;
  uint64_t offset = 0;
  if (!FindReleaseAppImage(options.release, appimage, error)) {
    return Fail(error);
  }
  if (!FindSquashfsOffset(appimage, offset, error)) {
    return Fail("This is not an AppImage this tool can read: " + Utf8(appimage) + " (" + error + ")");
  }
  squashfs::Reader release;
  if (!release.Open(appimage, offset, error)) {
    return Fail("Cannot read the AppImage " + Utf8(appimage) + ": " + error);
  }
  const squashfs::Entry* app_run = release.Find("AppRun");
  if (!app_run || app_run->directory || !release.Find("usr/bin/crackdown")) {
    return Fail("This is not a Crackdown Recompiled AppImage: it has no AppRun or usr/bin/crackdown.");
  }
  for (const squashfs::Entry& entry : release.entries()) {
    const std::string top = LowerAscii(entry.path.substr(0, entry.path.find('/')));
    if (kPlayerFolders.count(top) || top == "apprun.release") {
      return Fail("The release AppImage already holds " + entry.path + ": use a release as published.");
    }
  }

  if (!CheckNewOutput(options.out, error)) {
    return Fail(error);
  }
  fs::path work;
  if (!MakeWorkFolder(options.out, work, error)) {
    return Fail(error);
  }
  Cleanup cleanup;
  cleanup.Add(work);
  const fs::path game_dir = work / "game";
  if (Result installed = InstallInto(options, game, game_dir); !installed.ok) {
    return installed;
  }
  std::error_code ec;
  if (!CheckSpace(work, GameBytes(game) + fs::file_size(appimage, ec) * 4 + kSpaceMargin, error)) {
    return Fail(error);
  }

  const uint32_t mtime = release.mod_time();
  std::vector<squashfs::Item> items;
  for (const squashfs::Entry& entry : release.entries()) {
    squashfs::Item item;
    item.entry = entry;
    if (entry.path == "AppRun") {
      item.entry.path = "AppRun.release";
    }
    if (!entry.directory) {
      item.contents = [&release, path = entry.path](const squashfs::Sink& sink, std::string& reason) {
        return release.ReadFile(path, sink, reason);
      };
    }
    items.push_back(std::move(item));
  }
  items.push_back(TextItem("AppRun", kAppRunWrapper, 0755, mtime));
  items.push_back(TextItem("usr/share/doc/crackdown/" + std::string(kNoticeFile), NoticeText(), 0644, mtime));
  std::vector<std::string> folders;
  std::vector<GameFile> files;
  if (!ListFolder(game_dir, folders, files, error)) {
    return Fail("Cannot read the copied game files: " + error);
  }
  items.push_back({{"game", true, 0755, 0, mtime}, {}});
  for (const std::string& folder : folders) {
    items.push_back({{"game/" + folder, true, 0755, 0, mtime}, {}});
  }
  for (const GameFile& file : files) {
    squashfs::Item item;
    item.entry = {"game/" + file.path, false, 0644, file.size, mtime};
    item.contents = [source = file.source](const squashfs::Sink& sink, std::string& reason) {
      return StreamFile(source, sink, reason);
    };
    items.push_back(std::move(item));
  }

  fs::path partial = options.out;
  partial += ".partial";
  cleanup.Add(partial);
  {
    std::ifstream runtime(appimage, std::ios::binary);
    std::ofstream out(partial, std::ios::binary | std::ios::trunc);
    std::vector<char> head(static_cast<size_t>(offset));
    runtime.read(head.data(), std::streamsize(head.size()));
    out.write(head.data(), std::streamsize(head.size()));
    if (!runtime || !out) {
      return Fail("Cannot write " + Utf8(partial));
    }
    Log(options, "Writing the AppImage");
    int last = -1;
    const auto progress = [&](const squashfs::WriteProgress& state) {
      const int percent = state.total_bytes ? int(state.done_bytes * 100 / state.total_bytes) : 100;
      if (percent / 5 != last / 5) {
        last = percent;
        Log(options, "Writing the AppImage: " + std::to_string(percent) + "%");
      }
    };
    if (!squashfs::Write(out, std::move(items), mtime, error, progress)) {
      return Fail("Cannot write the AppImage: " + error);
    }
    out.close();
    if (!out) {
      return Fail("Cannot write " + Utf8(partial));
    }
  }
  Log(options, "Checking the AppImage");
  if (!VerifyAppImage(partial, offset, game, error)) {
    return Fail("The written AppImage failed its check: " + error);
  }
  fs::rename(partial, options.out, ec);
  if (ec) {
    return Fail("Cannot rename " + Utf8(partial) + " to " + Utf8(options.out));
  }
  fs::permissions(options.out, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                                   fs::perms::others_read | fs::perms::others_exec,
                  ec);
  return {true, {}};
}

// ---- Folders and the Windows ZIP ----

Result BundleLinuxFolder(const Options& options, const GameDescriptor& game) {
  std::string error;
  fs::path appimage;
  uint64_t offset = 0;
  if (!FindReleaseAppImage(options.release, appimage, error)) {
    return Fail(error);
  }
  if (!FindSquashfsOffset(appimage, offset, error)) {
    return Fail("This is not an AppImage this tool can read: " + Utf8(appimage) + " (" + error + ")");
  }
  if (!CheckNewOutput(options.out, error)) {
    return Fail(error);
  }
  Cleanup cleanup;
  cleanup.Add(options.out);
  std::error_code ec;
  fs::create_directories(options.out, ec);
  const fs::path copy = options.out / appimage.filename();
  fs::copy_file(appimage, copy, ec);
  if (ec) {
    return Fail("Cannot copy the AppImage to " + Utf8(copy));
  }
  fs::permissions(copy, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                  fs::perm_options::add, ec);
  if (Result installed = InstallInto(options, game, options.out / "game"); !installed.ok) {
    return installed;
  }
  if (!WriteText(options.out / kNoticeFile, NoticeText())) {
    return Fail("Cannot write " + Utf8(options.out / kNoticeFile));
  }
  cleanup.Keep();
  return {true, {}};
}

// The Windows folder: the release files, game\ next to crackdown.exe and the notice.
Result FillWindowsFolder(const Options& options, const GameDescriptor& game, const fs::path& folder) {
  std::string error;
  std::error_code ec;
  fs::create_directories(folder, ec);
  if (ec) {
    return Fail("Cannot create " + Utf8(folder));
  }
  Log(options, "Unpacking the release " + Utf8(options.release));
  if (!StageWindowsRelease(options.release, folder, error)) {
    return Fail(error);
  }
  if (Result installed = InstallInto(options, game, folder / "game"); !installed.ok) {
    return installed;
  }
  if (!WriteText(folder / kNoticeFile, NoticeText())) {
    return Fail("Cannot write " + Utf8(folder / kNoticeFile));
  }
  return {true, {}};
}

Result BundleWindowsFolder(const Options& options, const GameDescriptor& game) {
  std::string error;
  if (!CheckNewOutput(options.out, error)) {
    return Fail(error);
  }
  Cleanup cleanup;
  cleanup.Add(options.out);
  Result result = FillWindowsFolder(options, game, options.out);
  if (result.ok) {
    cleanup.Keep();
  }
  return result;
}

Result BundleWindowsZip(const Options& options, const GameDescriptor& game) {
  std::string error;
  if (!CheckNewOutput(options.out, error)) {
    return Fail(error);
  }
  fs::path work;
  if (!MakeWorkFolder(options.out, work, error)) {
    return Fail(error);
  }
  Cleanup cleanup;
  cleanup.Add(work);
  const std::string top = Utf8(options.out.stem());
  const fs::path folder = work / options.out.stem();
  if (Result result = FillWindowsFolder(options, game, folder); !result.ok) {
    return result;
  }
  if (!CheckSpace(work, GameBytes(game) + kSpaceMargin, error)) {
    return Fail(error);
  }
  fs::path partial = options.out;
  partial += ".partial";
  cleanup.Add(partial);
  const auto log = [&](const std::string& line) { Log(options, line); };
  if (!WriteZip(folder, partial, top, "game/", log, error)) {
    return Fail(error);
  }
  std::error_code ec;
  fs::rename(partial, options.out, ec);
  if (ec) {
    return Fail("Cannot rename " + Utf8(partial) + " to " + Utf8(options.out));
  }
  return {true, {}};
}

}  // namespace

bool ParseTarget(std::string_view text, Target& target) {
  const std::string lower = LowerAscii(std::string(text));
  if (lower == "windows") {
    target = Target::kWindows;
  } else if (lower == "linux" || lower == "steamdeck" || lower == "steam-deck") {
    target = Target::kLinux;
  } else if (lower == "android") {
    target = Target::kAndroid;
  } else {
    return false;
  }
  return true;
}

bool FormatFor(Target target, const fs::path& out, Format& format, std::string& error) {
  if (target == Target::kAndroid) {
    error = kNoAndroidBuild;
    return false;
  }
  const std::string extension = LowerAscii(Utf8(out.extension()));
  if (extension == ".exe") {
    error =
        "A single self-extracting .exe is not possible: the game files alone are about 6.5 GB and Windows refuses to "
        "start an executable larger than 4 GB. Write a folder, or a .zip archive.";
    return false;
  }
  if (extension == ".zip") {
    if (target != Target::kWindows) {
      error = "For Linux, write a .AppImage file or a folder.";
      return false;
    }
    format = Format::kZip;
    return true;
  }
  if (extension == ".appimage") {
    if (target != Target::kLinux) {
      error = "An AppImage is a Linux package: use --target linux, or write a folder or a .zip for Windows.";
      return false;
    }
    format = Format::kAppImage;
    return true;
  }
  format = Format::kFolder;
  return true;
}

bool FindSquashfsOffset(const fs::path& appimage, uint64_t& offset, std::string& error) {
  std::ifstream in(appimage, std::ios::binary);
  uint8_t header[64] = {};
  in.read(reinterpret_cast<char*>(header), sizeof(header));
  if (in.gcount() != std::streamsize(sizeof(header)) || std::memcmp(header, "\x7F" "ELF", 4) != 0 || header[4] != 2 ||
      header[5] != 1) {
    error = "not a 64-bit little-endian ELF file";
    return false;
  }
  uint64_t section_headers = 0;
  for (int i = 7; i >= 0; --i) {
    section_headers = (section_headers << 8) | header[0x28 + i];
  }
  const uint64_t entry_size = header[0x3A] | (header[0x3B] << 8);
  const uint64_t count = header[0x3C] | (header[0x3D] << 8);
  offset = section_headers + entry_size * count;
  uint8_t magic[4] = {};
  in.clear();
  in.seekg(std::streamoff(offset));
  in.read(reinterpret_cast<char*>(magic), 4);
  if (in.gcount() != 4 || std::memcmp(magic, "hsqs", 4) != 0) {
    error = "no SquashFS image after the runtime";
    return false;
  }
  return true;
}

Result Bundle(const Options& options) {
  const GameDescriptor& game = options.game ? *options.game : CrackdownGame();
  Format format = Format::kFolder;
  std::string error;
  if (!FormatFor(options.target, options.out, format, error)) {
    return Fail(error);
  }
  std::error_code ec;
  if (options.iso.empty() || !fs::exists(options.iso, ec)) {
    return Fail("The disc image does not exist: " + Utf8(options.iso));
  }
  if (options.release.empty() || !fs::exists(options.release, ec)) {
    return Fail("The release does not exist: " + Utf8(options.release));
  }
  switch (format) {
    case Format::kFolder:
      return options.target == Target::kWindows ? BundleWindowsFolder(options, game)
                                                : BundleLinuxFolder(options, game);
    case Format::kZip:
      return BundleWindowsZip(options, game);
    case Format::kAppImage:
      return BundleAppImage(options, game);
  }
  return Fail("unknown format");
}

}  // namespace crackdown::bundler
