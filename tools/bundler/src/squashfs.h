#pragma once

// SquashFS 4.0, the file system inside an AppImage. The reader handles what appimagetool writes (gzip or zstd, with
// fragments); the writer writes every block and table uncompressed, which any SquashFS reader accepts and which needs
// no compressor. Game data barely compresses, so an uncompressed image costs little space and no CPU at run time.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace crackdown::bundler::squashfs {

inline constexpr uint32_t kMagic = 0x73717368;  // "hsqs"
inline constexpr uint32_t kBlockSize = 128 * 1024;
inline constexpr size_t kMaxEntries = 1 << 20;
inline constexpr size_t kMaxDepth = 64;

// Receives consecutive pieces of a file's contents. Returns false to stop.
using Sink = std::function<bool(const uint8_t* data, size_t size)>;

struct Entry {
  std::string path;  // forward slashes, relative to the root, never empty
  bool directory = false;
  uint16_t mode = 0;  // permission bits only
  uint64_t size = 0;  // files only
  uint32_t mtime = 0;
};

class Reader {
 public:
  Reader();
  ~Reader();
  Reader(const Reader&) = delete;
  Reader& operator=(const Reader&) = delete;

  // `offset` is where the file system starts in `file` (after the runtime, in an AppImage).
  bool Open(const std::filesystem::path& file, uint64_t offset, std::string& error);

  // Every file and folder, a folder before what it holds, names sorted as stored.
  const std::vector<Entry>& entries() const { return entries_; }
  uint32_t mod_time() const { return mod_time_; }
  const Entry* Find(const std::string& path) const;

  bool ReadFile(const std::string& path, const Sink& sink, std::string& error);

 private:
  struct Inode;
  struct Table;
  bool ReadAt(uint64_t offset, void* out, size_t size);
  bool Decompress(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, size_t expected, std::string& error);
  bool ReadMetadata(Table& table, uint64_t block, uint32_t offset, size_t size, std::vector<uint8_t>& out,
                    std::string& error);
  bool ReadInode(uint64_t ref, Inode& inode, std::string& error);
  bool Walk(const Inode& dir, const std::string& prefix, size_t depth, std::string& error);
  bool ReadDataBlock(uint64_t start, uint32_t stored, size_t expected, std::vector<uint8_t>& out, std::string& error);

  std::ifstream in_;
  uint64_t base_ = 0;
  uint64_t image_end_ = 0;
  uint16_t compression_ = 0;
  uint32_t block_size_ = 0;
  uint32_t mod_time_ = 0;
  uint32_t fragment_count_ = 0;
  uint64_t fragment_table_ = 0;
  std::unique_ptr<Table> inodes_;
  std::unique_ptr<Table> directories_;
  std::unique_ptr<Table> fragments_;
  std::vector<Entry> entries_;
  std::map<std::string, size_t> index_;
  std::map<std::string, std::shared_ptr<Inode>> file_inodes_;
};

// One item of the image being written. Folders that are only implied by a file's path are created with mode 0755.
struct Item {
  Entry entry;
  // Writes exactly entry.size bytes to the sink (files only). Returns false, with `error` set, on failure.
  std::function<bool(const Sink& sink, std::string& error)> contents;
};

struct WriteProgress {
  uint64_t done_bytes = 0;
  uint64_t total_bytes = 0;
};

// Writes a complete file system at the current end of `out` (an AppImage runtime may precede it). The image is padded
// to a multiple of 4 KiB, like mksquashfs does.
bool Write(std::ostream& out, std::vector<Item> items, uint32_t mod_time, std::string& error,
           const std::function<void(const WriteProgress&)>& progress = {});

}  // namespace crackdown::bundler::squashfs
