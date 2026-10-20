#include "squashfs.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <utility>

#include <miniz.h>
#include <zstd.h>

namespace crackdown::bundler::squashfs {
namespace {

constexpr uint16_t kCompressionGzip = 1;
constexpr uint16_t kCompressionZstd = 6;
constexpr uint32_t kMetadataBytes = 8192;
constexpr uint16_t kMetadataUncompressed = 0x8000;
constexpr uint32_t kDataUncompressed = 1u << 24;
constexpr uint32_t kNoFragment = 0xFFFFFFFFu;
constexpr uint64_t kNoTable = ~uint64_t(0);
constexpr size_t kSuperblockBytes = 96;
constexpr uint32_t kFragmentsPerBlock = kMetadataBytes / 16;

constexpr uint16_t kBasicDirectory = 1;
constexpr uint16_t kBasicFile = 2;
constexpr uint16_t kExtendedDirectory = 8;
constexpr uint16_t kExtendedFile = 9;

// Superblock flags of an image written here: every block uncompressed, no fragments, no extended attributes.
constexpr uint16_t kUncompressedInodes = 0x0001;
constexpr uint16_t kUncompressedData = 0x0002;
constexpr uint16_t kUncompressedFragments = 0x0008;
constexpr uint16_t kNoFragments = 0x0010;
constexpr uint16_t kNoXattrs = 0x0200;
constexpr uint16_t kUncompressedIds = 0x0800;

uint16_t Le16(const uint8_t* p) {
  return uint16_t(p[0] | (p[1] << 8));
}

uint32_t Le32(const uint8_t* p) {
  return uint32_t(Le16(p)) | (uint32_t(Le16(p + 2)) << 16);
}

uint64_t Le64(const uint8_t* p) {
  return uint64_t(Le32(p)) | (uint64_t(Le32(p + 4)) << 32);
}

void Put16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(uint8_t(v));
  out.push_back(uint8_t(v >> 8));
}

void Put32(std::vector<uint8_t>& out, uint32_t v) {
  Put16(out, uint16_t(v));
  Put16(out, uint16_t(v >> 16));
}

void Put64(std::vector<uint8_t>& out, uint64_t v) {
  Put32(out, uint32_t(v));
  Put32(out, uint32_t(v >> 32));
}

bool IsPlainName(const std::string& name) {
  return !name.empty() && name != "." && name != ".." && name.find('/') == std::string::npos &&
         name.find('\0') == std::string::npos;
}

std::vector<std::string> SplitPath(const std::string& path) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= path.size()) {
    const size_t end = std::min(path.find('/', start), path.size());
    parts.push_back(path.substr(start, end - start));
    start = end + 1;
  }
  return parts;
}

}  // namespace

// ---- Reader ----

struct Reader::Table {
  uint64_t start = 0;  // relative to the superblock
  struct Block {
    std::vector<uint8_t> data;
    uint64_t next = 0;  // relative to `start`
  };
  std::map<uint64_t, Block> blocks;  // by position relative to `start`
};

struct Reader::Inode {
  uint16_t type = 0;
  uint16_t mode = 0;
  uint32_t mtime = 0;
  // Folders
  uint64_t listing_block = 0;
  uint32_t listing_offset = 0;
  uint32_t listing_size = 0;
  // Files
  uint64_t blocks_start = 0;
  uint64_t size = 0;
  uint32_t fragment = kNoFragment;
  uint32_t fragment_offset = 0;
  std::vector<uint32_t> blocks;

  bool directory() const { return type == kBasicDirectory || type == kExtendedDirectory; }
};

Reader::Reader() = default;
Reader::~Reader() = default;

bool Reader::ReadAt(uint64_t offset, void* out, size_t size) {
  if (offset > image_end_ || size > image_end_ - offset) {
    return false;
  }
  in_.clear();
  in_.seekg(std::streamoff(base_ + offset));
  in_.read(static_cast<char*>(out), std::streamsize(size));
  return in_.gcount() == std::streamsize(size);
}

bool Reader::Decompress(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, size_t capacity,
                        std::string& error) {
  out.resize(capacity);
  if (compression_ == kCompressionGzip) {
    mz_ulong length = mz_ulong(capacity);
    if (mz_uncompress(out.data(), &length, in.data(), mz_ulong(in.size())) != MZ_OK) {
      error = "a gzip block of the SquashFS image is damaged";
      return false;
    }
    out.resize(length);
    return true;
  }
  if (compression_ == kCompressionZstd) {
    const size_t length = ZSTD_decompress(out.data(), capacity, in.data(), in.size());
    if (ZSTD_isError(length)) {
      error = std::string("a zstd block of the SquashFS image is damaged: ") + ZSTD_getErrorName(length);
      return false;
    }
    out.resize(length);
    return true;
  }
  error = "unsupported SquashFS compression " + std::to_string(compression_) + " (gzip and zstd are supported)";
  return false;
}

bool Reader::ReadMetadata(Table& table, uint64_t block, uint32_t offset, size_t size, std::vector<uint8_t>& out,
                          std::string& error) {
  out.clear();
  while (out.size() < size) {
    auto found = table.blocks.find(block);
    if (found == table.blocks.end()) {
      uint8_t header[2];
      if (table.start > image_end_ || block > image_end_ - table.start || !ReadAt(table.start + block, header, 2)) {
        error = "a SquashFS table lies outside the image";
        return false;
      }
      const uint16_t stored = Le16(header) & 0x7FFF;
      if (stored == 0 || stored > kMetadataBytes) {
        error = "a SquashFS metadata block has an invalid size";
        return false;
      }
      std::vector<uint8_t> raw(stored);
      if (!ReadAt(table.start + block + 2, raw.data(), raw.size())) {
        error = "a SquashFS metadata block lies outside the image";
        return false;
      }
      Table::Block entry;
      entry.next = block + 2 + stored;
      if (Le16(header) & kMetadataUncompressed) {
        entry.data = std::move(raw);
      } else if (!Decompress(raw, entry.data, kMetadataBytes, error)) {
        return false;
      }
      found = table.blocks.emplace(block, std::move(entry)).first;
    }
    const Table::Block& data = found->second;
    if (offset > data.data.size()) {
      error = "a SquashFS reference points past its metadata block";
      return false;
    }
    const size_t take = std::min(size - out.size(), data.data.size() - offset);
    out.insert(out.end(), data.data.begin() + offset, data.data.begin() + offset + take);
    if (out.size() < size) {
      if (data.data.empty()) {
        error = "an empty SquashFS metadata block";
        return false;
      }
      block = data.next;
      offset = 0;
    }
  }
  return true;
}

bool Reader::ReadInode(uint64_t ref, Inode& inode, std::string& error) {
  const uint64_t block = ref >> 16;
  const uint32_t offset = uint32_t(ref & 0xFFFF);
  std::vector<uint8_t> data;
  if (!ReadMetadata(*inodes_, block, offset, 16, data, error)) {
    return false;
  }
  inode = {};
  inode.type = Le16(&data[0]);
  inode.mode = Le16(&data[2]) & 07777;
  inode.mtime = Le32(&data[8]);
  switch (inode.type) {
    case kBasicDirectory:
      if (!ReadMetadata(*inodes_, block, offset, 32, data, error)) {
        return false;
      }
      inode.listing_block = Le32(&data[16]);
      inode.listing_size = Le16(&data[24]);
      inode.listing_offset = Le16(&data[26]);
      return true;
    case kExtendedDirectory:
      if (!ReadMetadata(*inodes_, block, offset, 40, data, error)) {
        return false;
      }
      inode.listing_size = Le32(&data[20]);
      inode.listing_block = Le32(&data[24]);
      inode.listing_offset = Le16(&data[34]);
      return true;
    case kBasicFile:
    case kExtendedFile: {
      const size_t fixed = inode.type == kBasicFile ? 32 : 56;
      if (!ReadMetadata(*inodes_, block, offset, fixed, data, error)) {
        return false;
      }
      if (inode.type == kBasicFile) {
        inode.blocks_start = Le32(&data[16]);
        inode.fragment = Le32(&data[20]);
        inode.fragment_offset = Le32(&data[24]);
        inode.size = Le32(&data[28]);
      } else {
        inode.blocks_start = Le64(&data[16]);
        inode.size = Le64(&data[24]);
        inode.fragment = Le32(&data[44]);
        inode.fragment_offset = Le32(&data[48]);
      }
      const uint64_t count = inode.fragment == kNoFragment ? (inode.size + block_size_ - 1) / block_size_
                                                           : inode.size / block_size_;
      if (count > (1u << 22)) {
        error = "a file in the SquashFS image is implausibly large";
        return false;
      }
      if (!ReadMetadata(*inodes_, block, offset, fixed + count * 4, data, error)) {
        return false;
      }
      for (uint64_t i = 0; i < count; ++i) {
        inode.blocks.push_back(Le32(&data[fixed + i * 4]));
      }
      return true;
    }
    default:
      error = "the SquashFS image holds an entry of type " + std::to_string(inode.type) +
              " (only plain files and folders are supported)";
      return false;
  }
}

bool Reader::Walk(const Inode& dir, const std::string& prefix, size_t depth, std::string& error) {
  if (depth > kMaxDepth) {
    error = "the SquashFS image nests folders too deeply";
    return false;
  }
  if (dir.listing_size < 3) {
    error = "a SquashFS folder has an invalid size";
    return false;
  }
  const size_t size = dir.listing_size - 3;
  std::vector<uint8_t> listing;
  if (!ReadMetadata(*directories_, dir.listing_block, dir.listing_offset, size, listing, error)) {
    return false;
  }
  size_t at = 0;
  while (at < size) {
    if (size - at < 12) {
      error = "a SquashFS folder listing is truncated";
      return false;
    }
    const uint32_t count = Le32(&listing[at]) + 1;
    const uint32_t start = Le32(&listing[at + 4]);
    at += 12;
    if (count > 256) {
      error = "a SquashFS folder listing is damaged";
      return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
      if (size - at < 8) {
        error = "a SquashFS folder listing is truncated";
        return false;
      }
      const uint16_t offset = Le16(&listing[at]);
      const size_t name_size = size_t(Le16(&listing[at + 6])) + 1;
      at += 8;
      if (size - at < name_size) {
        error = "a SquashFS folder listing is truncated";
        return false;
      }
      const std::string name(reinterpret_cast<const char*>(&listing[at]), name_size);
      at += name_size;
      if (!IsPlainName(name)) {
        error = "the SquashFS image holds an unsafe name";
        return false;
      }
      auto inode = std::make_shared<Inode>();
      if (!ReadInode((uint64_t(start) << 16) | offset, *inode, error)) {
        return false;
      }
      const std::string path = prefix.empty() ? name : prefix + "/" + name;
      if (entries_.size() >= kMaxEntries || index_.count(path)) {
        error = "the SquashFS image has too many or repeated entries";
        return false;
      }
      index_[path] = entries_.size();
      entries_.push_back({path, inode->directory(), inode->mode, inode->directory() ? 0 : inode->size, inode->mtime});
      if (inode->directory()) {
        if (!Walk(*inode, path, depth + 1, error)) {
          return false;
        }
      } else {
        file_inodes_[path] = inode;
      }
    }
  }
  return true;
}

bool Reader::Open(const std::filesystem::path& file, uint64_t offset, std::string& error) {
  in_.open(file, std::ios::binary);
  if (!in_) {
    error = "cannot open the file";
    return false;
  }
  in_.seekg(0, std::ios::end);
  const uint64_t file_size = uint64_t(in_.tellg());
  if (offset > file_size) {
    error = "the SquashFS image starts past the end of the file";
    return false;
  }
  base_ = offset;
  image_end_ = file_size - offset;
  uint8_t sb[kSuperblockBytes];
  if (!ReadAt(0, sb, sizeof(sb)) || Le32(sb) != kMagic) {
    error = "no SquashFS image at offset " + std::to_string(offset);
    return false;
  }
  if (Le16(sb + 28) != 4 || Le16(sb + 30) != 0) {
    error = "unsupported SquashFS version " + std::to_string(Le16(sb + 28)) + "." + std::to_string(Le16(sb + 30));
    return false;
  }
  block_size_ = Le32(sb + 12);
  const uint16_t block_log = Le16(sb + 22);
  if (block_log < 12 || block_log > 20 || block_size_ != (1u << block_log)) {
    error = "the SquashFS block size is invalid";
    return false;
  }
  mod_time_ = Le32(sb + 8);
  fragment_count_ = Le32(sb + 16);
  compression_ = Le16(sb + 20);
  const uint64_t bytes_used = Le64(sb + 40);
  if (bytes_used > image_end_) {
    error = "the SquashFS image is truncated";
    return false;
  }
  image_end_ = bytes_used;
  inodes_ = std::make_unique<Table>();
  inodes_->start = Le64(sb + 64);
  directories_ = std::make_unique<Table>();
  directories_->start = Le64(sb + 72);
  fragments_ = std::make_unique<Table>();
  fragment_table_ = Le64(sb + 80);

  Inode root;
  if (!ReadInode(Le64(sb + 32), root, error)) {
    return false;
  }
  if (!root.directory()) {
    error = "the SquashFS root is not a folder";
    return false;
  }
  return Walk(root, "", 0, error);
}

const Entry* Reader::Find(const std::string& path) const {
  const auto found = index_.find(path);
  return found == index_.end() ? nullptr : &entries_[found->second];
}

bool Reader::ReadDataBlock(uint64_t start, uint32_t stored, size_t expected, std::vector<uint8_t>& out,
                           std::string& error) {
  const uint32_t length = stored & ~kDataUncompressed;
  if (length > block_size_) {
    error = "a SquashFS data block has an invalid size";
    return false;
  }
  std::vector<uint8_t> raw(length);
  if (!ReadAt(start, raw.data(), raw.size())) {
    error = "a SquashFS data block lies outside the image";
    return false;
  }
  if (stored & kDataUncompressed) {
    out = std::move(raw);
  } else if (!Decompress(raw, out, block_size_, error)) {
    return false;
  }
  if (expected && out.size() != expected) {
    error = "a SquashFS data block has the wrong length";
    return false;
  }
  return true;
}

bool Reader::ReadFile(const std::string& path, const Sink& sink, std::string& error) {
  const auto found = file_inodes_.find(path);
  if (found == file_inodes_.end()) {
    error = "no file " + path + " in the SquashFS image";
    return false;
  }
  const Inode& inode = *found->second;
  uint64_t remaining = inode.size;
  uint64_t position = inode.blocks_start;
  std::vector<uint8_t> block;
  for (const uint32_t stored : inode.blocks) {
    const size_t expected = size_t(std::min<uint64_t>(remaining, block_size_));
    if ((stored & ~kDataUncompressed) == 0) {
      block.assign(expected, 0);  // a sparse block
    } else if (!ReadDataBlock(position, stored, expected, block, error)) {
      return false;
    }
    if (!sink(block.data(), block.size())) {
      error = "stopped";
      return false;
    }
    position += stored & ~kDataUncompressed;
    remaining -= expected;
  }
  if (remaining == 0) {
    return true;
  }
  if (inode.fragment == kNoFragment || inode.fragment >= fragment_count_) {
    error = "a file in the SquashFS image is shorter than its size";
    return false;
  }
  uint8_t pointer[8];
  if (!ReadAt(fragment_table_ + uint64_t(inode.fragment / kFragmentsPerBlock) * 8, pointer, 8)) {
    error = "the SquashFS fragment table lies outside the image";
    return false;
  }
  std::vector<uint8_t> entry;
  if (!ReadMetadata(*fragments_, Le64(pointer), (inode.fragment % kFragmentsPerBlock) * 16, 16, entry, error) ||
      !ReadDataBlock(Le64(&entry[0]), Le32(&entry[8]), 0, block, error)) {
    return false;
  }
  if (inode.fragment_offset > block.size() || remaining > block.size() - inode.fragment_offset) {
    error = "a SquashFS fragment is shorter than the file needs";
    return false;
  }
  if (!sink(block.data() + inode.fragment_offset, size_t(remaining))) {
    error = "stopped";
    return false;
  }
  return true;
}

// ---- Writer ----

namespace {

struct Node {
  Entry entry;
  std::function<bool(const Sink&, std::string&)> contents;
  bool explicit_item = false;
  std::vector<size_t> children;
  size_t parent = 0;
  uint32_t number = 0;
  uint64_t inode_at = 0;    // in the uncompressed inode table
  uint64_t listing_at = 0;  // in the uncompressed directory table
  uint32_t listing_size = 0;
  uint64_t blocks_start = 0;
  std::vector<uint32_t> blocks;
};

std::string Name(const Node& node) {
  const size_t slash = node.entry.path.rfind('/');
  return slash == std::string::npos ? node.entry.path : node.entry.path.substr(slash + 1);
}

uint64_t Reference(uint64_t uncompressed) {
  // Every metadata block is stored uncompressed: 2 header bytes and 8 KiB of data.
  return ((uncompressed / kMetadataBytes) * (kMetadataBytes + 2) << 16) | (uncompressed % kMetadataBytes);
}

void PutMetadata(std::ostream& out, const std::vector<uint8_t>& stream) {
  for (size_t at = 0; at < stream.size(); at += kMetadataBytes) {
    const size_t length = std::min<size_t>(kMetadataBytes, stream.size() - at);
    const uint16_t header = uint16_t(length) | kMetadataUncompressed;
    const uint8_t bytes[2] = {uint8_t(header), uint8_t(header >> 8)};
    out.write(reinterpret_cast<const char*>(bytes), 2);
    out.write(reinterpret_cast<const char*>(stream.data() + at), std::streamsize(length));
  }
}

uint64_t MetadataBytes(size_t stream_size) {
  return stream_size + 2 * ((stream_size + kMetadataBytes - 1) / kMetadataBytes);
}

}  // namespace

bool Write(std::ostream& out, std::vector<Item> items, uint32_t mod_time, std::string& error,
           const std::function<void(const WriteProgress&)>& progress) {
  std::vector<Node> nodes(1);
  nodes[0].entry.directory = true;
  nodes[0].entry.mode = 0755;
  nodes[0].entry.mtime = mod_time;
  std::map<std::string, size_t> by_path;
  uint64_t total = 0;

  for (Item& item : items) {
    const std::vector<std::string> parts = SplitPath(item.entry.path);
    size_t parent = 0;
    std::string path;
    for (size_t i = 0; i < parts.size(); ++i) {
      if (!IsPlainName(parts[i])) {
        error = "invalid path in the image: '" + item.entry.path + "'";
        return false;
      }
      path = path.empty() ? parts[i] : path + "/" + parts[i];
      const bool last = i + 1 == parts.size();
      auto found = by_path.find(path);
      if (found == by_path.end()) {
        Node node;
        node.entry = {path, true, 0755, 0, mod_time};
        node.parent = parent;
        found = by_path.emplace(path, nodes.size()).first;
        nodes[parent].children.push_back(nodes.size());
        nodes.push_back(std::move(node));
      } else if (last && nodes[found->second].explicit_item) {
        error = "the image would hold " + path + " twice";
        return false;
      }
      Node& node = nodes[found->second];
      if (last) {
        if (!node.children.empty() && !item.entry.directory) {
          error = path + " is both a file and a folder";
          return false;
        }
        node.entry = item.entry;
        node.entry.path = path;
        node.entry.mode &= 07777;
        node.contents = std::move(item.contents);
        node.explicit_item = true;
        if (!node.entry.directory) {
          total += node.entry.size;
        }
      } else if (!node.entry.directory) {
        error = path + " is both a file and a folder";
        return false;
      }
      parent = found->second;
    }
  }
  if (nodes.size() >= kMaxEntries) {
    error = "too many entries for the image";
    return false;
  }
  for (Node& node : nodes) {
    std::sort(node.children.begin(), node.children.end(),
              [&](size_t a, size_t b) { return Name(nodes[a]) < Name(nodes[b]); });
  }

  // Depth-first order: the inode numbers, the inode table and the data all follow it.
  std::vector<size_t> order;
  std::vector<size_t> stack = {0};
  while (!stack.empty()) {
    const size_t index = stack.back();
    stack.pop_back();
    order.push_back(index);
    for (auto it = nodes[index].children.rbegin(); it != nodes[index].children.rend(); ++it) {
      stack.push_back(*it);
    }
  }

  const std::streamoff base = out.tellp();
  if (base < 0) {
    error = "cannot write the image";
    return false;
  }
  const std::vector<char> placeholder(kSuperblockBytes, 0);
  out.write(placeholder.data(), std::streamsize(placeholder.size()));
  uint64_t position = kSuperblockBytes;

  WriteProgress state{0, total};
  std::vector<uint8_t> buffer;
  buffer.reserve(kBlockSize);
  for (const size_t index : order) {
    Node& node = nodes[index];
    if (node.entry.directory || node.entry.size == 0) {
      continue;
    }
    node.blocks_start = position;
    uint64_t written = 0;
    const auto flush = [&] {
      out.write(reinterpret_cast<const char*>(buffer.data()), std::streamsize(buffer.size()));
      node.blocks.push_back(uint32_t(buffer.size()) | kDataUncompressed);
      position += buffer.size();
      state.done_bytes += buffer.size();
      buffer.clear();
      if (progress) {
        progress(state);
      }
      return bool(out);
    };
    const Sink sink = [&](const uint8_t* data, size_t size) {
      if (size > node.entry.size - written) {
        return false;
      }
      written += size;
      while (size > 0) {
        const size_t take = std::min(size, size_t(kBlockSize) - buffer.size());
        buffer.insert(buffer.end(), data, data + take);
        data += take;
        size -= take;
        if (buffer.size() == kBlockSize && !flush()) {
          return false;
        }
      }
      return true;
    };
    std::string reason;
    if (!node.contents || !node.contents(sink, reason)) {
      error = "cannot add " + node.entry.path + " to the image" + (reason.empty() ? "" : ": " + reason);
      return false;
    }
    if (written != node.entry.size) {
      error = node.entry.path + " changed size while it was added to the image";
      return false;
    }
    if (!buffer.empty() && !flush()) {
      error = "cannot write the image";
      return false;
    }
  }

  // Inode numbers and where each inode lands in the uncompressed inode table.
  uint64_t inode_bytes = 0;
  for (size_t i = 0; i < order.size(); ++i) {
    Node& node = nodes[order[i]];
    node.number = uint32_t(i + 1);
    node.inode_at = inode_bytes;
    inode_bytes += node.entry.directory ? 40 : 56 + 4 * node.blocks.size();
  }

  // Folder listings. Entries share a header while their inodes are in one metadata block and their numbers are close.
  std::vector<uint8_t> listings;
  for (const size_t index : order) {
    Node& node = nodes[index];
    if (!node.entry.directory) {
      continue;
    }
    node.listing_at = listings.size();
    size_t i = 0;
    while (i < node.children.size()) {
      const Node& first = nodes[node.children[i]];
      const uint64_t block = first.inode_at / kMetadataBytes;
      size_t end = i;
      while (end < node.children.size() && end - i < 256) {
        const Node& child = nodes[node.children[end]];
        const int64_t delta = int64_t(child.number) - int64_t(first.number);
        if (child.inode_at / kMetadataBytes != block || delta < -32768 || delta > 32767) {
          break;
        }
        ++end;
      }
      Put32(listings, uint32_t(end - i - 1));
      Put32(listings, uint32_t(block * (kMetadataBytes + 2)));
      Put32(listings, first.number);
      for (; i < end; ++i) {
        const Node& child = nodes[node.children[i]];
        const std::string name = Name(child);
        Put16(listings, uint16_t(child.inode_at % kMetadataBytes));
        Put16(listings, uint16_t(int16_t(int64_t(child.number) - int64_t(first.number))));
        Put16(listings, child.entry.directory ? kBasicDirectory : kBasicFile);
        Put16(listings, uint16_t(name.size() - 1));
        listings.insert(listings.end(), name.begin(), name.end());
      }
    }
    node.listing_size = uint32_t(listings.size() - node.listing_at + 3);
  }

  std::vector<uint8_t> inodes;
  inodes.reserve(size_t(inode_bytes));
  for (const size_t index : order) {
    const Node& node = nodes[index];
    Put16(inodes, node.entry.directory ? kExtendedDirectory : kExtendedFile);
    Put16(inodes, node.entry.mode);
    Put16(inodes, 0);  // uid: the only id, root
    Put16(inodes, 0);  // gid
    Put32(inodes, node.entry.mtime);
    Put32(inodes, node.number);
    if (node.entry.directory) {
      uint32_t subfolders = 0;
      for (const size_t child : node.children) {
        subfolders += nodes[child].entry.directory ? 1 : 0;
      }
      Put32(inodes, 2 + subfolders);
      Put32(inodes, node.listing_size);
      Put32(inodes, uint32_t(node.listing_at / kMetadataBytes * (kMetadataBytes + 2)));
      Put32(inodes, index == 0 ? uint32_t(nodes.size() + 1) : nodes[node.parent].number);
      Put16(inodes, 0);  // no index
      Put16(inodes, uint16_t(node.listing_at % kMetadataBytes));
      Put32(inodes, 0xFFFFFFFFu);  // no extended attributes
    } else {
      Put64(inodes, node.blocks_start);
      Put64(inodes, node.entry.size);
      Put64(inodes, 0);  // no sparse bytes
      Put32(inodes, 1);
      Put32(inodes, kNoFragment);
      Put32(inodes, 0);
      Put32(inodes, 0xFFFFFFFFu);
      for (const uint32_t block : node.blocks) {
        Put32(inodes, block);
      }
    }
  }

  const uint64_t inode_table = position;
  PutMetadata(out, inodes);
  position += MetadataBytes(inodes.size());
  const uint64_t directory_table = position;
  PutMetadata(out, listings);
  position += MetadataBytes(listings.size());
  const uint64_t id_block = position;
  PutMetadata(out, std::vector<uint8_t>{0, 0, 0, 0});
  position += MetadataBytes(4);
  const uint64_t id_table = position;
  std::vector<uint8_t> pointer;
  Put64(pointer, id_block);
  out.write(reinterpret_cast<const char*>(pointer.data()), std::streamsize(pointer.size()));
  position += pointer.size();
  const uint64_t bytes_used = position;
  const std::vector<char> padding((4096 - bytes_used % 4096) % 4096, 0);
  out.write(padding.data(), std::streamsize(padding.size()));

  std::vector<uint8_t> sb;
  Put32(sb, kMagic);
  Put32(sb, uint32_t(nodes.size()));
  Put32(sb, mod_time);
  Put32(sb, kBlockSize);
  Put32(sb, 0);  // fragments
  Put16(sb, kCompressionGzip);  // never used: every block is stored uncompressed
  Put16(sb, 17);                // log2(kBlockSize)
  Put16(sb, kUncompressedInodes | kUncompressedData | kUncompressedFragments | kNoFragments | kNoXattrs |
                kUncompressedIds);
  Put16(sb, 1);  // ids
  Put16(sb, 4);
  Put16(sb, 0);
  Put64(sb, Reference(nodes[0].inode_at));
  Put64(sb, bytes_used);
  Put64(sb, id_table);
  Put64(sb, kNoTable);  // extended attributes
  Put64(sb, inode_table);
  Put64(sb, directory_table);
  Put64(sb, id_block);  // the fragment table, empty, would start here
  Put64(sb, kNoTable);  // export table
  const std::streamoff end = out.tellp();
  out.seekp(base);
  out.write(reinterpret_cast<const char*>(sb.data()), std::streamsize(sb.size()));
  out.seekp(end);
  out.flush();
  if (!out) {
    error = "cannot write the image";
    return false;
  }
  return true;
}

}  // namespace crackdown::bundler::squashfs
