#include "dlc.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string_view>
#include <utility>

#include "stfs.h"

namespace crackdown {
namespace {

using namespace content_fs;
namespace fs = std::filesystem;

constexpr const char* kWorkFolder = "dlc-install-temp";
constexpr const char* kRemoveFolder = "dlc-remove-temp";
constexpr const char* kZeroXuid = "0000000000000000";
constexpr uint64_t kSpaceMargin = 64ull << 20;
constexpr size_t kContentHeaderBytes = 0x148;
constexpr size_t kHeaderDisplayNameOffset = 8;
constexpr size_t kHeaderDisplayNameChars = 128;
constexpr size_t kHeaderFileNameOffset = 264;
constexpr size_t kHeaderFileNameBytes = 42;
constexpr size_t kHeaderTitleOffset = 0x140;
constexpr uint32_t kHddDeviceId = 1;

using Bytes = std::vector<uint8_t>;

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

void PutBe16(Bytes& out, size_t at, uint16_t value) {
  out[at] = uint8_t(value >> 8);
  out[at + 1] = uint8_t(value);
}

void PutBe32(Bytes& out, size_t at, uint32_t value) {
  PutBe16(out, at, uint16_t(value >> 16));
  PutBe16(out, at + 2, uint16_t(value));
}

uint32_t ReadBe32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

uint32_t ReadLe32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
         });
}

std::string CanonicalName(std::string_view name) {
  std::string out;
  for (const char c : name) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!out.empty() && out.back() != ' ') {
        out.push_back(' ');
      }
    } else {
      out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
  }
  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out;
}

bool IsContentId(std::string_view id) {
  return id.size() == 40 && std::all_of(id.begin(), id.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
         });
}

std::string Hex8(uint32_t value) {
  char text[9];
  std::snprintf(text, sizeof(text), "%08X", value);
  return text;
}

fs::path TitleFolder(const fs::path& user_data_root) {
  return user_data_root / kZeroXuid / Hex8(content::kTitleId);
}

std::string ToUtf8(const std::u16string& text) {
  std::string out;
  for (size_t i = 0; i < text.size(); ++i) {
    uint32_t c = text[i];
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
      c = 0x10000 + ((c - 0xD800) << 10) + (text[i + 1] - 0xDC00);
      ++i;
    } else if (c >= 0xD800 && c <= 0xDFFF) {
      c = 0xFFFD;
    }
    if (c < 0x80) {
      out.push_back(char(c));
    } else if (c < 0x800) {
      out.push_back(char(0xC0 | (c >> 6)));
      out.push_back(char(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
      out.push_back(char(0xE0 | (c >> 12)));
      out.push_back(char(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(char(0x80 | (c & 0x3F)));
    } else {
      out.push_back(char(0xF0 | (c >> 18)));
      out.push_back(char(0x80 | ((c >> 12) & 0x3F)));
      out.push_back(char(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(char(0x80 | (c & 0x3F)));
    }
  }
  return out;
}

// The .header file the SDK reads for Marketplace content: its content record, then the licence mask when there is one.
bool WriteContentHeader(const fs::path& path, const std::string& file_name, const std::u16string& display_name,
                        uint32_t license_mask) {
  Bytes header(kContentHeaderBytes, 0);
  PutBe32(header, 0, kHddDeviceId);
  PutBe32(header, 4, content::kMarketplaceContentType);
  for (size_t i = 0; i < std::min(display_name.size(), kHeaderDisplayNameChars - 1); ++i) {
    PutBe16(header, kHeaderDisplayNameOffset + 2 * i, uint16_t(display_name[i]));
  }
  std::memcpy(&header[kHeaderFileNameOffset], file_name.data(), std::min(file_name.size(), kHeaderFileNameBytes));
  PutBe32(header, kHeaderTitleOffset, content::kTitleId);
  if (license_mask != 0) {
    for (int shift = 0; shift < 32; shift += 8) {
      header.push_back(uint8_t(license_mask >> shift));
    }
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(header.data()), std::streamsize(header.size()));
  out.close();
  return !out.fail();
}

// Marks `info` verified only when the header names this title, the Marketplace content type and `info.content_id`.
void ReadVerifiedHeader(const fs::path& path, DlcInfo& info) {
  std::error_code ec;
  if (!IsPlainFile(path) || fs::file_size(path, ec) < kContentHeaderBytes || ec) {
    return;
  }
  Bytes data(kContentHeaderBytes + 4, 0);
  std::ifstream in(path, std::ios::binary);
  in.read(reinterpret_cast<char*>(data.data()), std::streamsize(data.size()));
  const size_t read = size_t(in.gcount());
  const std::string file_name(reinterpret_cast<const char*>(&data[kHeaderFileNameOffset]), kHeaderFileNameBytes);
  if (ReadBe32(&data[4]) != content::kMarketplaceContentType || ReadBe32(&data[kHeaderTitleOffset]) != content::kTitleId ||
      std::string(file_name.c_str()) != info.content_id) {
    return;
  }
  info.verified = true;

  std::u16string name;
  for (size_t i = 0; i < kHeaderDisplayNameChars; ++i) {
    const size_t at = kHeaderDisplayNameOffset + 2 * i;
    const char16_t c = char16_t(data[at] << 8 | data[at + 1]);
    if (c == 0) {
      break;
    }
    name.push_back(c);
  }
  if (!name.empty()) {
    info.display_name = ToUtf8(name);
  }
  info.pack = ClassifyDlc(info.content_id, info.display_name);
  if (read >= kContentHeaderBytes + 4) {
    info.license_mask = ReadLe32(&data[kContentHeaderBytes]);
  }
}

uint64_t CountFiles(const fs::path& dir) {
  uint64_t count = 0;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    if (it->is_regular_file(ec)) {
      ++count;
    }
  }
  return count;
}

// Nothing on the way from the user data folder to the content folders may be a link.
bool ChainHasLink(const fs::path& root, const fs::path& leaf_parent) {
  for (fs::path p = leaf_parent; IsInside(root, p); p = p.parent_path()) {
    if (!IsAbsent(p) && IsLink(p)) {
      return true;
    }
  }
  return false;
}

uintmax_t Available(const fs::path& path, const FileOps& ops, std::error_code& ec) {
  if (ops.available_space) {
    return ops.available_space(path, ec);
  }
  return fs::space(path, ec).available;
}

bool IsSafeWorkFolder(const fs::path& root, const fs::path& work) {
  return root.is_absolute() && (work == root / kWorkFolder || work == root / kRemoveFolder) &&
         IsAbsentOrPlainFolder(work);
}

}  // namespace

fs::path DlcFolder(const fs::path& user_data_root) {
  return TitleFolder(user_data_root) / Hex8(content::kMarketplaceContentType);
}

fs::path DlcHeaderPath(const fs::path& user_data_root, const std::string& content_id) {
  return TitleFolder(user_data_root) / "Headers" / Hex8(content::kMarketplaceContentType) / (content_id + ".header");
}

content::DlcPack ClassifyDlc(std::string_view content_id, std::string_view display_name) {
  for (const content::DlcIdentity& identity : content::kDlcIdentities) {
    if (EqualsIgnoreCase(content_id, identity.content_id)) {
      return identity.pack;
    }
  }
  const std::string name = CanonicalName(display_name);
  for (const content::DlcIdentity& identity : content::kDlcIdentities) {
    if (name == CanonicalName(identity.name)) {
      return identity.pack;
    }
  }
  return content::DlcPack::kUnknown;
}

content::DlcPack ClassifyDlcName(std::string_view content_id, const std::u16string& display_name) {
  return ClassifyDlc(content_id, ToUtf8(display_name));
}

content::DlcPack PackOfKind(ContentKind kind) {
  switch (kind) {
    case ContentKind::kFourPlay:
      return content::DlcPack::kFourPlay;
    case ContentKind::kFreeForAll:
      return content::DlcPack::kFreeForAll;
    case ContentKind::kNone:
    case ContentKind::kTitleUpdate:
      break;
  }
  return content::DlcPack::kUnknown;
}

ContentKind KindOfPack(content::DlcPack pack) {
  switch (pack) {
    case content::DlcPack::kFourPlay:
      return ContentKind::kFourPlay;
    case content::DlcPack::kFreeForAll:
      return ContentKind::kFreeForAll;
    case content::DlcPack::kUnknown:
      break;
  }
  return ContentKind::kNone;
}

namespace {

ContentResult WrongPackage(ContentKind expected, ContentKind actual, std::string detail) {
  ContentResult result{ContentStatus::kWrongPackage, std::move(detail)};
  result.expected = expected;
  result.actual = actual;
  return result;
}

}  // namespace

ContentResult InstallDlc(const fs::path& package, const fs::path& user_data_root, bool title_update_usable,
                         const InstallHooks& hooks, DlcInfo* installed, const FileOps& ops,
                         content::DlcPack expected) {
  const auto report = [&](const char* phase, uint64_t done, uint64_t total) {
    return !hooks.progress || hooks.progress(InstallProgress{phase, done, total});
  };
  if (user_data_root.empty()) {
    return Failure(ContentStatus::kNoUserFolder);
  }
  std::error_code ec;
  const fs::path root = fs::absolute(user_data_root, ec).lexically_normal();
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
  const ContentKind expected_kind = KindOfPack(expected);
  if (pkg.content_type() == content::kTitleUpdateContentType && expected_kind != ContentKind::kNone) {
    return WrongPackage(expected_kind, ContentKind::kTitleUpdate, "title update package");
  }
  if (pkg.content_type() != content::kMarketplaceContentType) {
    return Failure(ContentStatus::kNotDlcPackage, "content type " + Hex8(pkg.content_type()));
  }
  read = pkg.LoadFiles();
  if (!read.ok()) {
    return Failure(FromStfs(read.status), read.detail);
  }
  const std::string content_id = pkg.content_id_hex();
  if (pkg.file_count() == 0) {
    return Failure(ContentStatus::kDamagedPackage, "no files");
  }
  const std::u16string display_name =
      pkg.display_name().empty() ? std::u16string(content_id.begin(), content_id.end()) : pkg.display_name();
  const content::DlcPack pack = ClassifyDlc(content_id, ToUtf8(pkg.display_name()));
  if (expected_kind != ContentKind::kNone && pack != content::DlcPack::kUnknown && pack != expected) {
    return WrongPackage(expected_kind, KindOfPack(pack), "content ID " + content_id);
  }
  const content::DlcIdentity* identity = content::IdentityOf(pack);
  if (!identity || !pkg.HasFile(identity->marker_file)) {
    return Failure(ContentStatus::kUnsupportedDlc, "content ID " + content_id);
  }
  if (!content::CanInstallDlc(pack, {.title_update_usable = title_update_usable})) {
    return Failure(ContentStatus::kTitleUpdateRequired, identity->name);
  }

  fs::create_directories(root, ec);
  if (ec) {
    return Failure(ContentStatus::kWriteFailed, "cannot create " + PathToUtf8(root) + ": " + ec.message());
  }
  const fs::path work = root / kWorkFolder;
  if (!IsSafeWorkFolder(root, work)) {
    return Failure(ContentStatus::kUnsafeTarget, "unsafe DLC work folder " + PathToUtf8(work));
  }
  if (RemoveWorkFolder(work, ops) != Removal::kGone) {
    return Failure(ContentStatus::kWriteFailed, "cannot clear " + PathToUtf8(work));
  }
  const uintmax_t space = Available(root, ops, ec);
  if (!ec && space < pkg.total_bytes() + kSpaceMargin) {
    return Failure(ContentStatus::kNoSpace, "needs " + std::to_string(pkg.total_bytes()) + " bytes, " +
                                                std::to_string(space) + " available");
  }
  WorkFolderGuard guard(work, ops);
  const fs::path staged = work / "staged";
  const fs::path backup = work / "backup";
  const fs::path files = staged / "files";
  fs::create_directories(files, ec);
  if (ec) {
    return Failure(ContentStatus::kWriteFailed, "cannot create " + PathToUtf8(files) + ": " + ec.message());
  }
  fs::create_directories(backup, ec);
  if (ec) {
    return Failure(ContentStatus::kWriteFailed, "cannot create " + PathToUtf8(backup) + ": " + ec.message());
  }

  const stfs::Result extracted = pkg.ExtractTo(files, [&](uint64_t done, uint64_t total) {
    return report("Installing", done, total);
  });
  if (!extracted.ok()) {
    return Failure(FromStfs(extracted.status), extracted.detail);
  }

  if (CountFiles(files) != pkg.file_count()) {
    return Failure(ContentStatus::kWriteFailed, "extracted files do not match the package");
  }
  const fs::path staged_header = staged / "header.bin";
  if (!WriteContentHeader(staged_header, content_id, display_name, pkg.license_mask())) {
    return Failure(ContentStatus::kWriteFailed, "cannot write " + PathToUtf8(staged_header));
  }
  if (!report("Finishing", pkg.total_bytes(), pkg.total_bytes())) {
    return Failure(ContentStatus::kCancelled);
  }

  const fs::path folder = DlcFolder(root) / content_id;
  const fs::path header = DlcHeaderPath(root, content_id);
  if (!IsSafeWorkFolder(root, work) || ChainHasLink(root, folder.parent_path()) ||
      ChainHasLink(root, header.parent_path())) {
    return Failure(ContentStatus::kUnsafeTarget, "unsafe DLC folder " + PathToUtf8(folder));
  }
  if (!IsAbsentOrPlainFolder(folder) || !IsAbsentOrPlainFile(header)) {
    return Failure(ContentStatus::kUnsafeTarget, "unsafe installed DLC " + PathToUtf8(folder));
  }
  // A previous copy that could not be put back stays in the work folder, so that folder is kept.
  const auto activation_failed = [&](const fs::path& target, bool keep_work_folder) {
    std::string detail = "cannot activate " + PathToUtf8(target);
    if (keep_work_folder) {
      guard.Release();
      detail += "; previous copy kept in " + PathToUtf8(backup);
    }
    return Failure(ContentStatus::kWriteFailed, std::move(detail));
  };
  bool folder_had_previous = false;
  const SwapResult folder_swap = SwapIn(files, folder, backup / "folder", ops, folder_had_previous);
  if (folder_swap != SwapResult::kActivated) {
    return activation_failed(folder, folder_swap == SwapResult::kStranded);
  }
  bool header_had_previous = false;
  const SwapResult header_swap = SwapIn(staged_header, header, backup / "header", ops, header_had_previous);
  if (header_swap != SwapResult::kActivated) {
    const bool reverted = RevertSwap(folder, backup / "folder", folder_had_previous, ops);
    return activation_failed(header, !reverted || header_swap == SwapResult::kStranded);
  }

  if (installed) {
    installed->content_id = content_id;
    installed->display_name = ToUtf8(display_name);
    installed->license_mask = pkg.license_mask();
    installed->pack = pack;
    installed->verified = true;
  }
  return {};
}

std::vector<DlcInfo> ListInstalledDlc(const fs::path& user_data_root) {
  std::vector<DlcInfo> installed;
  if (user_data_root.empty()) {
    return installed;
  }
  std::error_code ec;
  for (fs::directory_iterator it(DlcFolder(user_data_root), ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_directory(ec)) {
      continue;
    }
    DlcInfo info;
    info.content_id = it->path().filename().string();
    info.display_name = info.content_id;
    info.pack = ClassifyDlc(info.content_id, {});
    ReadVerifiedHeader(DlcHeaderPath(user_data_root, info.content_id), info);
    installed.push_back(std::move(info));
  }
  std::sort(installed.begin(), installed.end(),
            [](const DlcInfo& a, const DlcInfo& b) { return a.content_id < b.content_id; });
  return installed;
}

ContentResult RemoveDlc(const fs::path& user_data_root, const fs::path& game_dir, const std::string& content_id,
                        const FileOps& ops) {
  if (!IsContentId(content_id)) {
    return Failure(ContentStatus::kUnsafeTarget, "malformed content ID");
  }
  if (user_data_root.empty()) {
    return Failure(ContentStatus::kNotInstalled);
  }
  std::error_code ec;
  const fs::path root = fs::absolute(user_data_root, ec).lexically_normal();
  if (ec) {
    return Failure(ContentStatus::kUnsafeTarget, ec.message());
  }
  const std::vector<DlcInfo> installed = ListInstalledDlc(root);
  const auto entry = std::find_if(installed.begin(), installed.end(),
                                  [&](const DlcInfo& info) { return info.content_id == content_id; });
  if (entry == installed.end()) {
    return Failure(ContentStatus::kNotInstalled);
  }
  if (!entry->verified) {
    return Failure(ContentStatus::kUnverified, content_id);
  }

  const fs::path dlc_root = DlcFolder(root);
  const fs::path folder = dlc_root / content_id;
  const fs::path header = DlcHeaderPath(root, content_id);
  if (ChainHasLink(root, dlc_root) || !IsPlainChildDirectory(dlc_root, folder) || !IsPlainFile(header) ||
      ContainsLink(folder) || GameFolderWouldBeRemoved(folder, game_dir)) {
    return Failure(ContentStatus::kUnsafeTarget, content_id);
  }

  const fs::path work = root / kRemoveFolder;
  if (!IsSafeWorkFolder(root, work)) {
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
  const fs::path moved = work / "folder";
  if (!RenameWithRetry(folder, moved, ops, ec)) {
    return Failure(ContentStatus::kWriteFailed, "cannot move " + PathToUtf8(folder));
  }
  const fs::path moved_header = work / "header";
  if (!RenameWithRetry(header, moved_header, ops, ec)) {
    if (!RenameWithRetry(moved, folder, ops, ec)) {
      guard.Release();
      return Failure(ContentStatus::kWriteFailed,
                     "cannot restore " + PathToUtf8(folder) + " from " + PathToUtf8(moved));
    }
    return Failure(ContentStatus::kWriteFailed, "cannot remove " + PathToUtf8(header));
  }
  for (const DlcInfo& left : ListInstalledDlc(root)) {
    if (left.content_id == content_id) {
      return Failure(ContentStatus::kWriteFailed, "still installed: " + content_id);
    }
  }
  return {};
}

}  // namespace crackdown
