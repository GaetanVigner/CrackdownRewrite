#include "stfs.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

#include "sha1.h"

namespace crackdown::stfs {
namespace {

constexpr uint64_t kBlockSize = 0x1000;
constexpr uint64_t kBlocksPerHashTable = 0xAA;
constexpr uint64_t kBlocksPerLevel1 = 0x70E4;
constexpr size_t kHashEntryBytes = 24;
constexpr uint64_t kLastBlock = 0xFFFFFF;
constexpr size_t kHeaderBytes = 0x400;
constexpr size_t kEntryBytes = 0x40;
constexpr size_t kMaxNameBytes = 0x28;
constexpr size_t kMaxEntries = 0x40000;
constexpr size_t kMaxDepth = 32;
constexpr uint16_t kRootParent = 0xFFFF;
constexpr uint8_t kFlagConsecutive = 0x40;
constexpr uint8_t kFlagDirectory = 0x80;
constexpr uint64_t kHashedHeaderStart = 0x344;
constexpr uint64_t kContentIdOffset = 0x32C;
constexpr uint64_t kLicenseOffset = 0x22C;
constexpr size_t kLicenseEntries = 0x10;
constexpr size_t kLicenseBytes = 0x10;
constexpr uint64_t kDisplayNameOffset = 0x411;
constexpr size_t kDisplayNameBytes = 0x100;

uint32_t Be32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

uint32_t Be16(const uint8_t* p) {
  return uint32_t(p[0]) << 8 | p[1];
}

uint32_t Le24(const uint8_t* p) {
  return p[0] | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16;
}

uint32_t Le16(const uint8_t* p) {
  return p[0] | uint32_t(p[1]) << 8;
}

bool HasPackageMagic(const uint8_t* header) {
  return std::memcmp(header, "LIVE", 4) == 0 || std::memcmp(header, "PIRS", 4) == 0 ||
         std::memcmp(header, "CON ", 4) == 0;
}

bool IsDeviceName(std::string name) {
  name.resize(std::min(name.size(), name.find('.')));
  while (!name.empty() && name.back() == ' ') {
    name.pop_back();
  }
  for (char& c : name) {
    c = char(std::toupper(static_cast<unsigned char>(c)));
  }
  for (const char* reserved : {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"}) {
    if (name == reserved) {
      return true;
    }
  }
  return name.size() == 4 && (name.starts_with("COM") || name.starts_with("LPT")) && name[3] >= '1' &&
         name[3] <= '9';
}

// A name is one path component: printable ASCII without separators, drive or stream markers.
bool IsSafeName(const std::string& name) {
  if (name.empty() || name == "." || name == "..") {
    return false;
  }
  for (const unsigned char c : name) {
    if (c < 0x20 || c >= 0x7F || std::strchr("\\/:*?\"<>|", c)) {
      return false;
    }
  }
  if (name.back() == ' ' || name.back() == '.') {
    return false;
  }
  return !IsDeviceName(name);
}

std::string Printable(const std::string& name) {
  std::string out = name;
  for (char& c : out) {
    if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) >= 0x7F) {
      c = '?';
    }
  }
  return out;
}

std::string Lower(std::string text) {
  for (char& c : text) {
    c = char(std::tolower(static_cast<unsigned char>(c)));
  }
  return text;
}

bool IsInside(const std::filesystem::path& root, const std::filesystem::path& path) {
  const std::filesystem::path relative = path.lexically_relative(root);
  return !relative.empty() && relative != "." && *relative.begin() != "..";
}

}  // namespace

bool Package::ReadAt(uint64_t offset, void* out, size_t length) {
  if (offset > file_size_ || length > file_size_ - offset) {
    return false;
  }
  in_.clear();
  in_.seekg(std::streamoff(offset));
  in_.read(static_cast<char*>(out), std::streamsize(length));
  return in_.gcount() == std::streamsize(length);
}

// Data blocks are interleaved with hash tables: one before every 0xAA blocks, and the level 1 table after the first
// 0xAA. Packages above one level 1 table are refused before this is used.
uint64_t Package::BlockOffset(uint64_t block) const {
  uint64_t index = block + block / kBlocksPerHashTable + 1;
  if (block >= kBlocksPerHashTable) {
    index += block / kBlocksPerLevel1 + 1;
  }
  return base_ + index * kBlockSize;
}

uint64_t Package::Level0TableOffset(uint64_t group) const {
  return group == 0 ? base_ : BlockOffset(group * kBlocksPerHashTable) - kBlockSize;
}

std::string Package::content_id_hex() const {
  static constexpr char kDigits[] = "0123456789ABCDEF";
  std::string out;
  for (const uint8_t byte : content_id_) {
    out.push_back(kDigits[byte >> 4]);
    out.push_back(kDigits[byte & 0xF]);
  }
  return out;
}

uint32_t Package::license_mask() const {
  uint32_t mask = 0;
  for (size_t i = 0; i < kLicenseEntries; ++i) {
    const uint8_t* entry = header_ + kLicenseOffset + i * kLicenseBytes;
    if (Be32(entry + 12) != 0) {
      mask |= Be32(entry + 8);
    }
  }
  return mask;
}

bool Package::HasFile(std::string_view path) const {
  return std::any_of(files_.begin(), files_.end(), [&](const FileInfo& file) { return file.path == path; });
}

std::u16string Package::ReadEnglishName() {
  uint8_t raw[kDisplayNameBytes];
  std::u16string name;
  if (!ReadAt(kDisplayNameOffset, raw, sizeof(raw))) {
    return name;
  }
  for (size_t i = 0; i + 1 < sizeof(raw); i += 2) {
    const char16_t c = char16_t(raw[i] << 8 | raw[i + 1]);
    if (c == 0) {
      break;
    }
    name.push_back(c);
  }
  return name;
}

Result Package::Open(const std::filesystem::path& path) {
  in_.close();
  in_.clear();
  entries_.clear();
  files_.clear();
  level0_.clear();
  total_bytes_ = 0;
  total_blocks_ = 0;
  title_id_ = 0;
  content_type_ = 0;
  media_id_ = 0;
  content_id_ = {};
  display_name_.clear();
  // Linux opens a folder or a pipe as a stream.
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) {
    return {Status::kOpenFailed, "not a file"};
  }
  in_.open(path, std::ios::binary);
  if (!in_) {
    return {Status::kOpenFailed, "cannot open the file"};
  }
  in_.seekg(0, std::ios::end);
  const std::streamoff end = in_.tellg();
  if (end < 0) {
    return {Status::kOpenFailed, "cannot read the file size"};
  }
  file_size_ = uint64_t(end);
  if (file_size_ < kHeaderBytes || !ReadAt(0, header_, kHeaderBytes) || !HasPackageMagic(header_)) {
    return {Status::kNotPackage, "missing STFS header"};
  }
  title_id_ = Be32(header_ + 0x360);
  content_type_ = Be32(header_ + 0x344);
  media_id_ = Be32(header_ + 0x354);
  std::memcpy(content_id_.data(), header_ + kContentIdOffset, content_id_.size());
  display_name_ = ReadEnglishName();

  base_ = (uint64_t(Be32(header_ + 0x340)) + 0xFFF) & ~uint64_t(0xFFF);
  if (base_ < 0x1000 || base_ > 0x100000) {
    return {Status::kCorrupt, "invalid header size"};
  }
  std::vector<uint8_t> hashed(size_t(base_ - kHashedHeaderStart));
  if (!ReadAt(kHashedHeaderStart, hashed.data(), hashed.size())) {
    return {Status::kCorrupt, "package header is truncated"};
  }
  if (Sha1Of(hashed.data(), hashed.size()) != content_id_) {
    return {Status::kCorrupt, "header hash mismatch"};
  }
  return {};
}

Result Package::LoadHashTables() {
  total_blocks_ = Be32(header_ + 0x395);
  if (total_blocks_ == 0) {
    return {Status::kCorrupt, "package has no blocks"};
  }
  if (total_blocks_ > kBlocksPerLevel1) {
    return {Status::kUnsupported, "package needs more than one level 1 hash table"};
  }
  if (BlockOffset(total_blocks_ - 1) + kBlockSize > file_size_) {
    return {Status::kCorrupt, "package is truncated"};
  }
  const uint64_t groups = (total_blocks_ + kBlocksPerHashTable - 1) / kBlocksPerHashTable;
  level0_.assign(size_t(groups), Block{});
  for (uint64_t g = 0; g < groups; ++g) {
    if (!ReadAt(Level0TableOffset(g), level0_[size_t(g)].data(), kBlockSize)) {
      return {Status::kCorrupt, "hash table is outside the package"};
    }
  }

  const uint8_t* expected_top = header_ + 0x381;
  if (groups == 1) {
    if (std::memcmp(Sha1Of(level0_[0].data(), kBlockSize).data(), expected_top, 20) != 0) {
      return {Status::kCorrupt, "top hash mismatch"};
    }
    return {};
  }
  Block level1;
  if (!ReadAt(BlockOffset(kBlocksPerHashTable) - 2 * kBlockSize, level1.data(), kBlockSize)) {
    return {Status::kCorrupt, "hash table is outside the package"};
  }
  if (std::memcmp(Sha1Of(level1.data(), kBlockSize).data(), expected_top, 20) != 0) {
    return {Status::kCorrupt, "top hash mismatch"};
  }
  for (uint64_t g = 0; g < groups; ++g) {
    if (std::memcmp(level1.data() + g * kHashEntryBytes, Sha1Of(level0_[size_t(g)].data(), kBlockSize).data(), 20) !=
        0) {
      return {Status::kCorrupt, "hash table mismatch"};
    }
  }
  return {};
}

Result Package::LoadFiles() {
  entries_.clear();
  files_.clear();
  total_bytes_ = 0;

  if (header_[0x379] != 0x24) {
    return {Status::kUnsupported, "unknown volume descriptor"};
  }
  if (!(header_[0x37B] & 1)) {
    return {Status::kUnsupported, "read/write package layout"};
  }
  if (Result tables = LoadHashTables(); !tables.ok()) {
    return tables;
  }
  const size_t table_blocks = Le16(header_ + 0x37C);
  const uint64_t first_block = Le24(header_ + 0x37E);
  const size_t entries_per_block = kBlockSize / kEntryBytes;
  if (table_blocks == 0 || table_blocks * entries_per_block > kMaxEntries ||
      first_block + table_blocks > total_blocks_) {
    return {Status::kCorrupt, "invalid file table size"};
  }

  std::vector<uint8_t> table(table_blocks * kBlockSize);
  for (size_t i = 0; i < table_blocks; ++i) {
    const uint64_t block = first_block + i;
    uint8_t* data = &table[i * kBlockSize];
    if (!ReadAt(BlockOffset(block), data, kBlockSize)) {
      return {Status::kCorrupt, "file table is outside the package"};
    }
    const uint8_t* expected = level0_[size_t(block / kBlocksPerHashTable)].data() +
                              (block % kBlocksPerHashTable) * kHashEntryBytes;
    if (std::memcmp(Sha1Of(data, kBlockSize).data(), expected, 20) != 0) {
      return {Status::kCorrupt, "file table hash mismatch"};
    }
  }

  struct Raw {
    std::string name;
    uint8_t flags = 0;
    uint32_t start_block = 0;
    uint32_t size = 0;
    uint16_t parent = kRootParent;
    bool used = false;
  };
  const size_t count = table_blocks * entries_per_block;
  std::vector<Raw> raw(count);
  bool ended = false;
  for (size_t i = 0; i < count; ++i) {
    const uint8_t* p = &table[i * kEntryBytes];
    const size_t name_length = p[0x28] & 0x3F;
    if (name_length == 0) {
      ended = true;
      continue;
    }
    if (ended) {
      return {Status::kCorrupt, "file table entry after the end of the table"};
    }
    if (name_length > kMaxNameBytes) {
      return {Status::kCorrupt, "invalid entry name length"};
    }
    raw[i] = {std::string(reinterpret_cast<const char*>(p), name_length),
              p[0x28],
              Le24(p + 0x2F),
              Be32(p + 0x34),
              uint16_t(Be16(p + 0x32)),
              true};
  }

  std::set<std::string> seen;
  for (size_t i = 0; i < count; ++i) {
    if (!raw[i].used) {
      continue;
    }
    std::vector<const std::string*> parts;
    for (size_t index = i, depth = 0;; ++depth) {
      if (depth > kMaxDepth) {
        return {Status::kCorrupt, "directories are nested too deeply"};
      }
      const Raw& entry = raw[index];
      if (!IsSafeName(entry.name)) {
        return {Status::kUnsafePath, "unsafe entry name: " + Printable(entry.name)};
      }
      parts.push_back(&entry.name);
      if (entry.parent == kRootParent) {
        break;
      }
      if (entry.parent >= count || !raw[entry.parent].used || !(raw[entry.parent].flags & kFlagDirectory)) {
        return {Status::kCorrupt, "entry has no parent directory: " + Printable(entry.name)};
      }
      index = entry.parent;
    }

    Entry entry;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
      entry.path += entry.path.empty() ? **it : "/" + **it;
    }
    if (!seen.insert(Lower(entry.path)).second) {
      return {Status::kCorrupt, "duplicate entry: " + entry.path};
    }
    entry.directory = (raw[i].flags & kFlagDirectory) != 0;
    if (!entry.directory) {
      entry.start_block = raw[i].start_block;
      entry.size = raw[i].size;
      if (entry.size > 0) {
        const uint64_t blocks = (uint64_t(entry.size) + kBlockSize - 1) / kBlockSize;
        if (blocks > 1 && !(raw[i].flags & kFlagConsecutive)) {
          return {Status::kUnsupported, "fragmented file: " + entry.path};
        }
        if (uint64_t(entry.start_block) + blocks > total_blocks_ || entry.start_block >= kLastBlock) {
          return {Status::kCorrupt, "package is truncated: " + entry.path};
        }
      }
      total_bytes_ += entry.size;
      files_.push_back({entry.path, entry.size});
    }
    entries_.push_back(std::move(entry));
  }
  return {};
}

Result Package::ExtractTo(const std::filesystem::path& root, const ProgressFn& progress) {
  std::error_code ec;
  const std::filesystem::path base = std::filesystem::absolute(root, ec).lexically_normal();
  if (ec) {
    return {Status::kWriteFailed, "invalid extraction folder: " + ec.message()};
  }
  if (progress && !progress(0, total_bytes_)) {
    return {Status::kCancelled, {}};
  }

  std::vector<uint8_t> buffer(kBlocksPerHashTable * kBlockSize);
  uint64_t done = 0;
  for (const Entry& entry : entries_) {
    const std::filesystem::path dest = base / std::filesystem::path(entry.path);
    if (!IsInside(base, dest)) {
      return {Status::kUnsafePath, "entry leaves the extraction folder: " + entry.path};
    }
    if (entry.directory) {
      std::filesystem::create_directories(dest, ec);
      if (ec) {
        return {Status::kWriteFailed, "cannot create " + entry.path + ": " + ec.message()};
      }
      continue;
    }
    std::filesystem::create_directories(dest.parent_path(), ec);
    if (ec) {
      return {Status::kWriteFailed, "cannot create folder for " + entry.path + ": " + ec.message()};
    }
    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
    if (!out) {
      return {Status::kWriteFailed, "cannot create " + entry.path};
    }

    uint64_t block = entry.start_block;
    uint64_t remaining = entry.size;
    while (remaining > 0) {
      const uint64_t blocks_left = (remaining + kBlockSize - 1) / kBlockSize;
      const uint64_t run = std::min(blocks_left, kBlocksPerHashTable - block % kBlocksPerHashTable);
      const uint64_t bytes = std::min(run * kBlockSize, remaining);
      if (!ReadAt(BlockOffset(block), buffer.data(), size_t(run * kBlockSize))) {
        return {Status::kCorrupt, "cannot read " + entry.path};
      }
      const Block& table = level0_[size_t(block / kBlocksPerHashTable)];
      for (uint64_t i = 0; i < run; ++i) {
        const uint8_t* expected = table.data() + ((block + i) % kBlocksPerHashTable) * kHashEntryBytes;
        if (std::memcmp(Sha1Of(&buffer[size_t(i * kBlockSize)], kBlockSize).data(), expected, 20) != 0) {
          return {Status::kCorrupt, "block hash mismatch in " + entry.path};
        }
      }
      out.write(reinterpret_cast<const char*>(buffer.data()), std::streamsize(bytes));
      if (!out) {
        return {Status::kWriteFailed, "cannot write " + entry.path};
      }
      block += run;
      remaining -= bytes;
      done += bytes;
      if (progress && !progress(done, total_bytes_)) {
        return {Status::kCancelled, {}};
      }
    }
    out.close();
    if (out.fail()) {
      return {Status::kWriteFailed, "cannot finish " + entry.path};
    }
  }
  return {};
}

}  // namespace crackdown::stfs
