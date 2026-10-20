// Synthetic Xbox 360 (XDVDFS) disc images for tests: no game files needed. Shared by tests/installer_tests.cpp and the
// bundler tests (tools/bundler/tests).
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#endif

namespace fake_disc {

namespace fs = std::filesystem;

using Bytes = std::vector<uint8_t>;

inline Bytes Pattern(size_t size, uint32_t seed) {
  Bytes data(size);
  uint32_t x = seed * 2654435761u + 1;
  for (uint8_t& b : data) {
    x = x * 1664525u + 1013904223u;
    b = uint8_t(x >> 24);
  }
  return data;
}

#if defined(_WIN32)
inline void MarkSparse(const fs::path& path) {
  const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }
  DWORD returned = 0;
  DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr);
  CloseHandle(file);
}
#endif

// ---- Synthetic XDVDFS images ----

struct Node {
  std::string name;
  bool dir = false;
  Bytes data;
  std::vector<Node> children;
};

inline Node MakeFile(std::string name, Bytes data) {
  return {std::move(name), false, std::move(data), {}};
}

inline Node MakeDir(std::string name, std::vector<Node> children = {}) {
  return {std::move(name), true, {}, std::move(children)};
}

enum class Shape { kBalanced, kRightChain, kLeftChain };

struct ImageOptions {
  uint64_t partition_offset = 0;
  Shape shape = Shape::kBalanced;
  size_t cut = 0;  // bytes dropped from the end of the image
  bool trailing_magic = true;
};

struct BuiltImage {
  Bytes bytes;  // from the start of the partition
  uint64_t partition_offset = 0;
  uint32_t root_sector = 0;
  std::map<std::string, size_t> entry_at;     // path -> offset of its entry within `bytes`
  std::map<std::string, uint32_t> sector_of;  // path -> first sector of a directory or file
};

inline constexpr uint32_t kSector = 2048;
inline constexpr size_t kNone = size_t(-1);
inline constexpr size_t kDescriptorAt = 32 * kSector;
inline constexpr size_t kRootFieldsAt = kDescriptorAt + 20;
inline constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";

inline void PutLe16(Bytes& b, size_t at, uint16_t v) {
  b[at] = uint8_t(v);
  b[at + 1] = uint8_t(v >> 8);
}

inline void PutLe32(Bytes& b, size_t at, uint32_t v) {
  PutLe16(b, at, uint16_t(v));
  PutLe16(b, at + 2, uint16_t(v >> 16));
}

inline std::string Lower(std::string text) {
  for (char& c : text) {
    if (c >= 'A' && c <= 'Z') {
      c = char(c - 'A' + 'a');
    }
  }
  return text;
}

struct Flat {
  const Node* node;
  std::string path;
  std::vector<size_t> children;
  uint32_t sector = 0;
  uint32_t size = 0;
};

inline size_t Flatten(const Node& node, const std::string& path, std::vector<Flat>& flat) {
  const size_t index = flat.size();
  flat.push_back({&node, path, {}, 0, 0});
  for (const Node& child : node.children) {
    const size_t child_index = Flatten(child, path.empty() ? child.name : path + "/" + child.name, flat);
    flat[index].children.push_back(child_index);
  }
  return index;
}

struct Linked {
  size_t flat_index;
  size_t left = kNone;
  size_t right = kNone;
};

inline size_t LinkBalanced(const std::vector<size_t>& sorted, size_t lo, size_t hi, std::vector<Linked>& out) {
  if (lo >= hi) {
    return kNone;
  }
  const size_t mid = lo + (hi - lo) / 2;
  const size_t at = out.size();
  out.push_back({sorted[mid]});
  const size_t left = LinkBalanced(sorted, lo, mid, out);
  const size_t right = LinkBalanced(sorted, mid + 1, hi, out);
  out[at].left = left;
  out[at].right = right;
  return at;
}

// The entries of one directory in storage order (the tree root first), linked as the shape asks.
inline std::vector<Linked> LinkDirectory(const std::vector<Flat>& flat, const Flat& dir, Shape shape) {
  std::vector<std::pair<std::string, size_t>> keyed;
  for (const size_t child : dir.children) {
    keyed.emplace_back(Lower(flat[child].node->name), child);
  }
  std::sort(keyed.begin(), keyed.end(), [&](const auto& a, const auto& b) {
    return a.first != b.first ? a.first < b.first : flat[a.second].node->name < flat[b.second].node->name;
  });
  std::vector<size_t> sorted;
  for (const auto& entry : keyed) {
    sorted.push_back(entry.second);
  }
  std::vector<Linked> out;
  switch (shape) {
    case Shape::kBalanced:
      LinkBalanced(sorted, 0, sorted.size(), out);
      break;
    case Shape::kRightChain:
      for (size_t i = 0; i < sorted.size(); ++i) {
        out.push_back({sorted[i], kNone, i + 1 < sorted.size() ? i + 1 : kNone});
      }
      break;
    case Shape::kLeftChain:
      for (size_t i = 0; i < sorted.size(); ++i) {
        out.push_back({sorted[sorted.size() - 1 - i], i + 1 < sorted.size() ? i + 1 : kNone, kNone});
      }
      break;
  }
  return out;
}

inline size_t EntryBytes(const std::string& name) {
  return (14 + name.size() + 3) & ~size_t(3);
}

// Entry offsets within a directory; an entry never straddles a sector.
inline std::vector<size_t> PlaceEntries(const std::vector<Flat>& flat, const std::vector<Linked>& linked,
                                        size_t& total) {
  std::vector<size_t> offsets;
  size_t at = 0;
  for (const Linked& item : linked) {
    const size_t bytes = EntryBytes(flat[item.flat_index].node->name);
    if (at % kSector + bytes > kSector) {
      at = (at + kSector - 1) / kSector * kSector;
    }
    offsets.push_back(at);
    at += bytes;
  }
  total = std::max<size_t>(kSector, (at + kSector - 1) / kSector * kSector);
  return offsets;
}

inline BuiltImage BuildImage(const std::vector<Node>& top, const ImageOptions& options = {}) {
  const Node root = MakeDir("", top);
  std::vector<Flat> flat;
  Flatten(root, "", flat);

  std::vector<std::vector<Linked>> links(flat.size());
  std::vector<std::vector<size_t>> offsets(flat.size());
  for (size_t i = 0; i < flat.size(); ++i) {
    if (flat[i].node->dir) {
      links[i] = LinkDirectory(flat, flat[i], options.shape);
      size_t total = 0;
      offsets[i] = PlaceEntries(flat, links[i], total);
      flat[i].size = uint32_t(total);
      if (total > 0x3FFFC) {
        std::printf("  test bug: directory '%s' is too large\n", flat[i].path.c_str());
        std::abort();
      }
    }
  }

  uint32_t next = 33;
  for (Flat& item : flat) {
    if (item.node->dir) {
      item.sector = next;
      next += item.size / kSector;
    }
  }
  for (Flat& item : flat) {
    if (!item.node->dir) {
      item.size = uint32_t(item.node->data.size());
      item.sector = item.size == 0 ? 0 : next;
      next += (item.size + kSector - 1) / kSector;
    }
  }

  BuiltImage image;
  image.partition_offset = options.partition_offset;
  image.root_sector = flat[0].sector;
  image.bytes.assign(size_t(next) * kSector, 0);
  std::memcpy(&image.bytes[kDescriptorAt], kMagic, 20);
  PutLe32(image.bytes, kRootFieldsAt, flat[0].sector);
  PutLe32(image.bytes, kRootFieldsAt + 4, flat[0].size);
  if (options.trailing_magic) {
    std::memcpy(&image.bytes[kDescriptorAt + 0x7EC], kMagic, 20);
  }

  for (size_t i = 0; i < flat.size(); ++i) {
    const Flat& item = flat[i];
    image.sector_of[item.path] = item.sector;
    if (!item.node->dir) {
      if (item.size > 0) {
        std::memcpy(&image.bytes[size_t(item.sector) * kSector], item.node->data.data(), item.size);
      }
      continue;
    }
    const size_t base = size_t(item.sector) * kSector;
    std::memset(&image.bytes[base], 0xFF, item.size);
    for (size_t k = 0; k < links[i].size(); ++k) {
      const Flat& entry = flat[links[i][k].flat_index];
      const size_t at = base + offsets[i][k];
      const auto child_offset = [&](size_t position) {
        return position == kNone ? uint16_t(0) : uint16_t(offsets[i][position] / 4);
      };
      PutLe16(image.bytes, at, child_offset(links[i][k].left));
      PutLe16(image.bytes, at + 2, child_offset(links[i][k].right));
      PutLe32(image.bytes, at + 4, entry.sector);
      PutLe32(image.bytes, at + 8, entry.size);
      image.bytes[at + 12] = entry.node->dir ? 0x10 : 0x20;
      image.bytes[at + 13] = uint8_t(entry.node->name.size());
      std::memcpy(&image.bytes[at + 14], entry.node->name.data(), entry.node->name.size());
      image.entry_at[entry.path] = at;
    }
  }
  image.bytes.resize(image.bytes.size() - std::min(options.cut, image.bytes.size()));
  return image;
}

inline void WriteImage(const fs::path& path, const BuiltImage& image) {
  fs::create_directories(path.parent_path());
  {
    std::ofstream create(path, std::ios::binary | std::ios::trunc);
  }
#if defined(_WIN32)
  if (image.partition_offset > 0) {
    MarkSparse(path);
  }
#endif
  std::error_code ec;
  fs::resize_file(path, image.partition_offset + image.bytes.size(), ec);
  std::fstream out(path, std::ios::binary | std::ios::in | std::ios::out);
  out.seekp(std::streamoff(image.partition_offset));
  out.write(reinterpret_cast<const char*>(image.bytes.data()), std::streamsize(image.bytes.size()));
}

}  // namespace fake_disc
