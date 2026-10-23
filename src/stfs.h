#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace crackdown::stfs {

enum class Status {
  kOk,
  kOpenFailed,
  kNotPackage,
  kUnsupported,
  kCorrupt,
  kUnsafePath,
  kWriteFailed,
  kCancelled,
};

struct Result {
  Status status = Status::kOk;
  std::string detail;
  bool ok() const { return status == Status::kOk; }
};

// Returns false to cancel the extraction.
using ProgressFn = std::function<bool(uint64_t done, uint64_t total)>;

struct FileInfo {
  std::string path;  // forward-slash separated, relative to the package root
  uint64_t size = 0;
};

// Reads a read-only STFS package (LIVE, PIRS or CON) whose files are stored in consecutive blocks, which is how
// Marketplace content and title updates are laid out. The header hash, the hash tables and every data block are
// checked against their SHA-1 hashes; the package signature is not (it needs Microsoft's keys).
class Package {
 public:
  // Reads the metadata and checks the header hash.
  Result Open(const std::filesystem::path& path);
  // Reads the file table, rejects any entry that could write outside the extraction root and checks the hash tables.
  Result LoadFiles();

  uint32_t title_id() const { return title_id_; }
  uint32_t content_type() const { return content_type_; }
  uint32_t media_id() const { return media_id_; }
  // Upper-case hexadecimal, 40 digits.
  std::string content_id_hex() const;
  // The English display name from the package metadata, empty when there is none.
  const std::u16string& display_name() const { return display_name_; }
  // Licence bits of the entries that have flags set, combined as the SDK's content manager does.
  uint32_t license_mask() const;
  uint64_t total_bytes() const { return total_bytes_; }
  size_t file_count() const { return files_.size(); }
  const std::vector<FileInfo>& files() const { return files_; }
  // Exact, case-sensitive match of a forward-slash path.
  bool HasFile(std::string_view path) const;

  // `root` must be an empty folder the caller owns. Every block is verified before it is written.
  Result ExtractTo(const std::filesystem::path& root, const ProgressFn& progress);

 private:
  struct Entry {
    std::string path;
    uint32_t start_block = 0;
    uint32_t size = 0;
    bool directory = false;
  };
  using Block = std::array<uint8_t, 0x1000>;

  bool ReadAt(uint64_t offset, void* out, size_t length);
  uint64_t BlockOffset(uint64_t block) const;
  uint64_t Level0TableOffset(uint64_t group) const;
  Result LoadHashTables();
  std::u16string ReadEnglishName();

  std::ifstream in_;
  uint64_t file_size_ = 0;
  uint8_t header_[0x400] = {};
  uint32_t title_id_ = 0;
  uint32_t content_type_ = 0;
  uint32_t media_id_ = 0;
  std::array<uint8_t, 20> content_id_ = {};
  std::u16string display_name_;
  uint64_t base_ = 0;
  uint64_t total_blocks_ = 0;
  uint64_t total_bytes_ = 0;
  std::vector<Entry> entries_;
  std::vector<FileInfo> files_;
  std::vector<Block> level0_;
};

}  // namespace crackdown::stfs
