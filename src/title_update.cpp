#include "title_update.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

#include "dlc.h"
#include "sha256.h"
#include "stfs.h"

namespace crackdown {
namespace {

using namespace content_fs;
namespace fs = std::filesystem;

constexpr const char* kWorkFolder = "install-temp";
constexpr const char* kRemoveFolder = "remove-temp";
constexpr const char* kManifestName = "update.manifest";
constexpr const char* kManifestHeader = "crackdown-title-update 1";
constexpr uint64_t kSpaceMargin = 64ull << 20;
constexpr uint32_t kXexpDescriptorKey = 0x000005FF;
constexpr size_t kXexpHeaderBytes = 0x18;

ContentResult Failure(ContentStatus status, std::string detail = {}) {
  return {status, std::move(detail)};
}

ContentStatus FromStfs(stfs::Status status) {
  switch (status) {
    case stfs::Status::kOpenFailed:
      return ContentStatus::kOpenFailed;
    case stfs::Status::kNotPackage:
      return ContentStatus::kNotPackage;
    case stfs::Status::kUnsupported:
      return ContentStatus::kUnsupportedPackage;
    case stfs::Status::kCorrupt:
    case stfs::Status::kUnsafePath:
      return ContentStatus::kDamagedPackage;
    case stfs::Status::kCancelled:
      return ContentStatus::kCancelled;
    case stfs::Status::kWriteFailed:
    case stfs::Status::kOk:
      break;
  }
  return ContentStatus::kWriteFailed;
}

std::string Hex8(uint32_t value) {
  char text[9];
  std::snprintf(text, sizeof(text), "%08X", value);
  return text;
}

uint32_t Be32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

std::string LanguageStringsPath(std::string_view language) {
  return std::string(content::kUpdateLocalFolder) + "/" + std::string(language) + "/" + content::kUpdateStringsName;
}

// The only files an update installs: the patch and the per-language strings the patched game reads from update:\.
bool IsUpdateFile(std::string_view path) {
  if (path == content::kUpdateXexpName) {
    return true;
  }
  return std::any_of(std::begin(content::kUpdateLanguages), std::end(content::kUpdateLanguages),
                     [&](std::string_view language) { return path == LanguageStringsPath(language); });
}

fs::path FromPortable(std::string_view path) {
  fs::path out;
  while (!path.empty()) {
    const size_t end = path.find('/');
    out /= fs::path(std::string(path.substr(0, end)));
    path = end == std::string_view::npos ? std::string_view{} : path.substr(end + 1);
  }
  return out;
}

bool ReadFile(const fs::path& path, std::vector<uint8_t>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return !in.bad();
}

// The patch is an XEX2 file whose delta patch descriptor (optional header 0x5FF) names the target and source versions.
bool ReadXexpVersions(const std::vector<uint8_t>& xexp, uint32_t& target, uint32_t& source) {
  if (xexp.size() < kXexpHeaderBytes || std::memcmp(xexp.data(), "XEX2", 4) != 0) {
    return false;
  }
  const uint32_t count = Be32(&xexp[0x14]);
  if (count > 0x200 || kXexpHeaderBytes + size_t(count) * 8 > xexp.size()) {
    return false;
  }
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* entry = &xexp[kXexpHeaderBytes + size_t(i) * 8];
    if (Be32(entry) != kXexpDescriptorKey) {
      continue;
    }
    const size_t offset = Be32(entry + 4);
    if (offset > xexp.size() || xexp.size() - offset < 12) {
      return false;
    }
    target = Be32(&xexp[offset + 4]);
    source = Be32(&xexp[offset + 8]);
    return true;
  }
  return false;
}

bool MatchesUpdate(const std::vector<uint8_t>& xexp, const TitleUpdateDescriptor& update, std::string& why) {
  if (xexp.size() != update.xexp_size) {
    why = "Default.xexp size " + std::to_string(xexp.size());
    return false;
  }
  uint32_t target = 0;
  uint32_t source = 0;
  if (!ReadXexpVersions(xexp, target, source)) {
    why = "Default.xexp is not an XEX patch";
    return false;
  }
  if (target != update.version || source != update.base_version) {
    why = "Default.xexp patches " + Hex8(source) + " to " + Hex8(target);
    return false;
  }
  Sha256 hash;
  hash.Update(xexp.data(), xexp.size());
  if (hash.Finish() != update.xexp_sha256) {
    why = "Default.xexp is a different build of the patch";
    return false;
  }
  return true;
}

struct Manifest {
  uint32_t version = 0;
  std::vector<std::pair<std::string, uint64_t>> files;
};

std::string FormatManifest(uint32_t version, const std::vector<stfs::FileInfo>& files) {
  std::ostringstream out;
  out << kManifestHeader << '\n' << "version " << Hex8(version) << '\n';
  for (const stfs::FileInfo& file : files) {
    out << file.size << ' ' << file.path << '\n';
  }
  return out.str();
}

bool ParseManifest(const fs::path& path, Manifest& manifest) {
  std::ifstream in(path);
  std::string line;
  if (!in || !std::getline(in, line) || line != kManifestHeader || !std::getline(in, line) ||
      line.rfind("version ", 0) != 0) {
    return false;
  }
  manifest.version = uint32_t(std::strtoul(line.c_str() + 8, nullptr, 16));
  while (std::getline(in, line)) {
    const size_t space = line.find(' ');
    if (space == std::string::npos || space == 0 || space + 1 >= line.size()) {
      return false;
    }
    manifest.files.emplace_back(line.substr(space + 1), std::strtoull(line.c_str(), nullptr, 10));
  }
  return !manifest.files.empty();
}

uintmax_t Available(const fs::path& path, const FileOps& ops, std::error_code& ec) {
  if (ops.available_space) {
    return ops.available_space(path, ec);
  }
  return fs::space(path, ec).available;
}

bool IsSafeWorkFolder(const fs::path& tu_root, const fs::path& work) {
  return tu_root.is_absolute() && (work == tu_root / kWorkFolder || work == tu_root / kRemoveFolder) &&
         IsAbsentOrPlainFolder(work);
}

// Looks through the package before anything is written.
ContentResult CheckPackageFiles(const stfs::Package& pkg, const TitleUpdateDescriptor& update) {
  for (const stfs::FileInfo& file : pkg.files()) {
    if (!IsUpdateFile(file.path)) {
      return Failure(ContentStatus::kUnsupportedUpdate, "unexpected file " + file.path);
    }
  }
  if (!pkg.HasFile(content::kUpdateXexpName)) {
    return Failure(ContentStatus::kUnsupportedUpdate, "no Default.xexp");
  }
  if (!pkg.HasFile(LanguageStringsPath(content::kRequiredUpdateLanguage))) {
    return Failure(ContentStatus::kUnsupportedUpdate, "no strings for " + std::string(content::kRequiredUpdateLanguage));
  }
  for (const stfs::FileInfo& file : pkg.files()) {
    if (file.path == content::kUpdateXexpName && file.size != update.xexp_size) {
      return Failure(ContentStatus::kUnsupportedUpdate, "Default.xexp size " + std::to_string(file.size));
    }
  }
  return {};
}

}  // namespace

const TitleUpdateDescriptor& TitleUpdate6() {
  static const TitleUpdateDescriptor update{content::kTitleUpdate6XexpSize, content::kTitleUpdate6XexpSha256,
                                            content::kTitleUpdate6Version, content::kBaseVersion};
  return update;
}

fs::path TitleUpdateRoot(const fs::path& user_data_root) {
  return user_data_root / "title_update";
}

fs::path TitleUpdateFolder(const fs::path& user_data_root) {
  return TitleUpdateRoot(user_data_root) / Hex8(content::kTitleId);
}

TitleUpdateCheck CheckTitleUpdate(const fs::path& user_data_root, const TitleUpdateDescriptor& update) {
  if (user_data_root.empty()) {
    return {};
  }
  const fs::path folder = TitleUpdateFolder(user_data_root);
  if (IsAbsent(folder)) {
    return {};
  }
  const auto damaged = [](std::string detail) {
    return TitleUpdateCheck{TitleUpdateState::kDamaged, std::move(detail)};
  };
  if (!IsPlainFolder(folder)) {
    return damaged("not a plain folder");
  }
  Manifest manifest;
  if (!ParseManifest(folder / kManifestName, manifest)) {
    return damaged("no readable manifest");
  }
  if (manifest.version != update.version) {
    return damaged("manifest names version " + Hex8(manifest.version));
  }
  bool has_xexp = false;
  bool has_required_strings = false;
  for (const auto& [path, size] : manifest.files) {
    const fs::path file = folder / FromPortable(path);
    std::error_code ec;
    if (!IsUpdateFile(path) || !IsPlainFile(file) || fs::file_size(file, ec) != size || ec) {
      return damaged("missing or changed: " + path);
    }
    has_xexp |= path == content::kUpdateXexpName;
    has_required_strings |= path == LanguageStringsPath(content::kRequiredUpdateLanguage);
  }
  if (!has_xexp || !has_required_strings) {
    return damaged("required file not listed");
  }
  std::vector<uint8_t> xexp;
  std::string why;
  if (!ReadFile(folder / content::kUpdateXexpName, xexp) || !MatchesUpdate(xexp, update, why)) {
    return damaged(why.empty() ? "cannot read Default.xexp" : why);
  }
  return {TitleUpdateState::kInstalled, {}};
}

ContentResult InstallTitleUpdate(const fs::path& package, const fs::path& user_data_root, const InstallHooks& hooks,
                                 const TitleUpdateDescriptor& update, const FileOps& ops) {
  const auto report = [&](const char* phase, uint64_t done, uint64_t total) {
    return !hooks.progress || hooks.progress(InstallProgress{phase, done, total});
  };
  if (user_data_root.empty()) {
    return Failure(ContentStatus::kNoUserFolder);
  }
  std::error_code ec;
  const fs::path user_root = fs::absolute(user_data_root, ec).lexically_normal();
  if (ec) {
    return Failure(ContentStatus::kNoUserFolder, ec.message());
  }

  report("Reading the package", 0, 0);
  stfs::Package pkg;
  stfs::Result read = pkg.Open(package);
  if (!read.ok()) {
    return Failure(FromStfs(read.status), read.detail);
  }
  if (pkg.title_id() != content::kTitleId) {
    return Failure(ContentStatus::kWrongTitle, "title ID " + Hex8(pkg.title_id()));
  }
  if (pkg.content_type() == content::kMarketplaceContentType) {
    read = pkg.LoadFiles();
    if (!read.ok()) {
      return Failure(FromStfs(read.status), read.detail);
    }
    const content::DlcPack pack = ClassifyDlcName(pkg.content_id_hex(), pkg.display_name());
    if (pack != content::DlcPack::kUnknown) {
      ContentResult wrong{ContentStatus::kWrongPackage, "content ID " + pkg.content_id_hex()};
      wrong.expected = ContentKind::kTitleUpdate;
      wrong.actual = KindOfPack(pack);
      return wrong;
    }
  }
  if (pkg.content_type() != content::kTitleUpdateContentType) {
    return Failure(ContentStatus::kNotTitleUpdate, "content type " + Hex8(pkg.content_type()));
  }
  if (pkg.media_id() != content::kMediaId) {
    return Failure(ContentStatus::kWrongMedia, "media ID " + Hex8(pkg.media_id()));
  }
  read = pkg.LoadFiles();
  if (!read.ok()) {
    return Failure(FromStfs(read.status), read.detail);
  }
  if (ContentResult files = CheckPackageFiles(pkg, update); !files.ok()) {
    return files;
  }

  const fs::path tu_root = TitleUpdateRoot(user_root);
  fs::create_directories(tu_root, ec);
  if (ec) {
    return Failure(ContentStatus::kWriteFailed, "cannot create " + PathToUtf8(tu_root) + ": " + ec.message());
  }
  if (IsLink(tu_root) || IsLink(user_root)) {
    return Failure(ContentStatus::kUnsafeTarget, "link in " + PathToUtf8(tu_root));
  }
  const fs::path work = tu_root / kWorkFolder;
  if (!IsSafeWorkFolder(tu_root, work)) {
    return Failure(ContentStatus::kUnsafeTarget, "unsafe work folder " + PathToUtf8(work));
  }
  if (RemoveWorkFolder(work, ops) != Removal::kGone) {
    return Failure(ContentStatus::kWriteFailed, "cannot clear " + PathToUtf8(work));
  }
  const uintmax_t space = Available(tu_root, ops, ec);
  if (!ec && space < pkg.total_bytes() * 2 + kSpaceMargin) {
    return Failure(ContentStatus::kNoSpace, "needs " + std::to_string(pkg.total_bytes() * 2) + " bytes, " +
                                                std::to_string(space) + " available");
  }
  WorkFolderGuard guard(work, ops);
  const fs::path staged = work / "staged";
  const fs::path backup = work / "backup";
  fs::create_directories(staged, ec);
  if (ec) {
    return Failure(ContentStatus::kWriteFailed, "cannot create " + PathToUtf8(staged) + ": " + ec.message());
  }
  fs::create_directories(backup, ec);
  if (ec) {
    return Failure(ContentStatus::kWriteFailed, "cannot create " + PathToUtf8(backup) + ": " + ec.message());
  }

  const stfs::Result extracted = pkg.ExtractTo(staged, [&](uint64_t done, uint64_t total) {
    return report("Installing", done, total);
  });
  if (!extracted.ok()) {
    return Failure(FromStfs(extracted.status), extracted.detail);
  }

  std::vector<uint8_t> xexp;
  std::string why;
  if (!ReadFile(staged / content::kUpdateXexpName, xexp) || !MatchesUpdate(xexp, update, why)) {
    return Failure(ContentStatus::kUnsupportedUpdate, why.empty() ? "cannot read Default.xexp" : why);
  }
  {
    std::ofstream manifest(staged / kManifestName, std::ios::binary | std::ios::trunc);
    manifest << FormatManifest(update.version, pkg.files());
    manifest.close();
    if (manifest.fail()) {
      return Failure(ContentStatus::kWriteFailed, "cannot write the manifest");
    }
  }
  if (!report("Finishing", pkg.total_bytes(), pkg.total_bytes())) {
    return Failure(ContentStatus::kCancelled);
  }

  const fs::path live = TitleUpdateFolder(user_root);
  if (!IsSafeWorkFolder(tu_root, work) || !IsAbsentOrPlainFolder(live)) {
    return Failure(ContentStatus::kUnsafeTarget, "unsafe installed update " + PathToUtf8(live));
  }
  bool had_previous = false;
  const SwapResult swap = SwapIn(staged, live, backup / "previous", ops, had_previous);
  if (swap != SwapResult::kActivated) {
    std::string detail = "cannot activate " + PathToUtf8(live);
    if (swap == SwapResult::kStranded) {
      guard.Release();
      detail += "; previous update kept in " + PathToUtf8(backup);
    }
    return Failure(ContentStatus::kWriteFailed, std::move(detail));
  }
  if (CheckTitleUpdate(user_root, update).state != TitleUpdateState::kInstalled) {
    if (!RevertSwap(live, backup / "previous", had_previous, ops)) {
      guard.Release();
    }
    return Failure(ContentStatus::kWriteFailed, "the installed update did not verify");
  }
  return {};
}

ContentResult RemoveTitleUpdate(const fs::path& user_data_root, const fs::path& game_dir, const FileOps& ops) {
  if (user_data_root.empty()) {
    return Failure(ContentStatus::kNotInstalled);
  }
  std::error_code ec;
  const fs::path user_root = fs::absolute(user_data_root, ec).lexically_normal();
  if (ec) {
    return Failure(ContentStatus::kUnsafeTarget, ec.message());
  }
  const fs::path tu_root = TitleUpdateRoot(user_root);
  const fs::path live = TitleUpdateFolder(user_root);
  if (IsAbsent(live)) {
    return Failure(ContentStatus::kNotInstalled);
  }
  for (const DlcInfo& dlc : ListInstalledDlc(user_root)) {
    if (dlc.pack == content::DlcPack::kFreeForAll) {
      return Failure(ContentStatus::kTitleUpdateInUse, dlc.content_id);
    }
  }
  if (IsLink(tu_root) || !IsPlainChildDirectory(tu_root, live) || ContainsLink(live) ||
      GameFolderWouldBeRemoved(live, game_dir)) {
    return Failure(ContentStatus::kUnsafeTarget, PathToUtf8(live));
  }
  const fs::path work = tu_root / kRemoveFolder;
  if (!IsSafeWorkFolder(tu_root, work)) {
    return Failure(ContentStatus::kUnsafeTarget, "unsafe removal folder " + PathToUtf8(work));
  }
  if (RemoveWorkFolder(work, ops) != Removal::kGone) {
    return Failure(ContentStatus::kWriteFailed, "cannot clear " + PathToUtf8(work));
  }
  fs::create_directories(work, ec);
  if (ec) {
    return Failure(ContentStatus::kWriteFailed, "cannot create " + PathToUtf8(work) + ": " + ec.message());
  }
  WorkFolderGuard guard(work, ops);
  if (!RenameWithRetry(live, work / "update", ops, ec)) {
    return Failure(ContentStatus::kWriteFailed, "cannot move " + PathToUtf8(live));
  }
  return {};
}

}  // namespace crackdown
