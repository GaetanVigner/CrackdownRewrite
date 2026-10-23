#pragma once

// Builds small read-only STFS packages with valid SHA-1 hash tables, for the installer tests.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "content_catalog.h"
#include "sha1.h"

namespace stfs_test {

using Bytes = std::vector<uint8_t>;

constexpr uint64_t kBase = 0xB000;
constexpr uint64_t kBlock = 0x1000;

struct Item {
  std::string name;
  int parent = -1;
  bool dir = false;
  Bytes data;
};

inline Item File(std::string name, int parent, Bytes data) {
  return {std::move(name), parent, false, std::move(data)};
}

inline Item Dir(std::string name, int parent) {
  return {std::move(name), parent, true, {}};
}

struct BuildOptions {
  const char* magic = "LIVE";
  uint32_t title = crackdown::content::kTitleId;
  uint32_t content_type = crackdown::content::kMarketplaceContentType;
  uint32_t media_id = 0;
  std::u16string display_name;
  uint32_t license_bits = 0;
  uint32_t license_flags = 0;
  uint32_t license3_bits = 0;  // a second licence entry, as the Free-For-All Pack has
  uint32_t license3_flags = 0;
  bool read_only = true;
  bool consecutive = true;
  size_t truncate = 0;
  // Applied after every hash is computed, so the package is internally inconsistent.
  int corrupt_data_byte_of_item = -1;
  bool corrupt_header = false;
  bool corrupt_level0_table = false;
  bool corrupt_top_hash = false;
};

inline void PutBe32(Bytes& b, size_t at, uint32_t v) {
  b[at] = uint8_t(v >> 24);
  b[at + 1] = uint8_t(v >> 16);
  b[at + 2] = uint8_t(v >> 8);
  b[at + 3] = uint8_t(v);
}

inline void PutLe24(Bytes& b, size_t at, uint32_t v) {
  b[at] = uint8_t(v);
  b[at + 1] = uint8_t(v >> 8);
  b[at + 2] = uint8_t(v >> 16);
}

// Physical index (in blocks after the header) of data block `block`; the hash tables sit between data blocks.
inline uint64_t Physical(uint64_t block) {
  uint64_t index = block + block / 0xAA + 1;
  if (block >= 0xAA) {
    index += 1;
  }
  return kBase + index * kBlock;
}

inline uint64_t Level0Table(uint64_t group) {
  return group == 0 ? kBase : Physical(group * 0xAA) - kBlock;
}

inline Bytes BuildPackage(const std::vector<Item>& items, const BuildOptions& options = {}) {
  const size_t table_blocks = (items.size() * 0x40 + kBlock - 1) / kBlock;
  uint64_t next_block = table_blocks;
  std::vector<uint64_t> start(items.size());
  for (size_t i = 0; i < items.size(); ++i) {
    start[i] = next_block;
    next_block += (items[i].data.size() + kBlock - 1) / kBlock;
  }
  const uint64_t total_blocks = next_block;
  Bytes image(Physical(total_blocks - 1) + kBlock);
  std::memcpy(image.data(), options.magic, 4);
  PutBe32(image, 0x340, 0xAD0E);
  PutBe32(image, 0x344, options.content_type);
  PutBe32(image, 0x354, options.media_id);
  for (size_t i = 0; i < options.display_name.size() && i < 127; ++i) {
    image[0x411 + 2 * i] = uint8_t(options.display_name[i] >> 8);
    image[0x412 + 2 * i] = uint8_t(options.display_name[i]);
  }
  PutBe32(image, 0x22C + 8, options.license_bits);
  PutBe32(image, 0x22C + 12, options.license_flags);
  PutBe32(image, 0x22C + 3 * 16 + 8, options.license3_bits);
  PutBe32(image, 0x22C + 3 * 16 + 12, options.license3_flags);
  PutBe32(image, 0x360, options.title);
  image[0x379] = 0x24;
  image[0x37B] = options.read_only ? 1 : 0;
  image[0x37C] = uint8_t(table_blocks);
  image[0x37D] = uint8_t(table_blocks >> 8);
  PutLe24(image, 0x37E, 0);
  PutBe32(image, 0x395, uint32_t(total_blocks));

  for (size_t i = 0; i < items.size(); ++i) {
    uint8_t* e = &image[Physical(0) + i * 0x40];
    std::memcpy(e, items[i].name.data(), items[i].name.size());
    const uint32_t blocks = uint32_t((items[i].data.size() + kBlock - 1) / kBlock);
    e[0x28] = uint8_t(items[i].name.size() | (options.consecutive ? 0x40 : 0) | (items[i].dir ? 0x80 : 0));
    PutLe24(image, Physical(0) + i * 0x40 + 0x29, blocks);
    PutLe24(image, Physical(0) + i * 0x40 + 0x2C, blocks);
    PutLe24(image, Physical(0) + i * 0x40 + 0x2F, uint32_t(start[i]));
    const uint16_t parent = items[i].parent < 0 ? 0xFFFF : uint16_t(items[i].parent);
    e[0x32] = uint8_t(parent >> 8);
    e[0x33] = uint8_t(parent);
    PutBe32(image, Physical(0) + i * 0x40 + 0x34, uint32_t(items[i].data.size()));
    for (uint64_t b = 0; b * kBlock < items[i].data.size(); ++b) {
      const size_t n = std::min<size_t>(kBlock, items[i].data.size() - b * kBlock);
      std::memcpy(&image[Physical(start[i] + b)], &items[i].data[b * kBlock], n);
    }
  }

  const uint64_t groups = (total_blocks + 0xAA - 1) / 0xAA;
  for (uint64_t b = 0; b < total_blocks; ++b) {
    const crackdown::Sha1Digest hash = crackdown::Sha1Of(&image[Physical(b)], kBlock);
    std::memcpy(&image[Level0Table(b / 0xAA) + (b % 0xAA) * 24], hash.data(), 20);
  }
  crackdown::Sha1Digest top;
  if (groups == 1) {
    top = crackdown::Sha1Of(&image[Level0Table(0)], kBlock);
  } else {
    const uint64_t level1 = Physical(0xAA) - 2 * kBlock;
    for (uint64_t g = 0; g < groups; ++g) {
      const crackdown::Sha1Digest hash = crackdown::Sha1Of(&image[Level0Table(g)], kBlock);
      std::memcpy(&image[level1 + g * 24], hash.data(), 20);
    }
    top = crackdown::Sha1Of(&image[level1], kBlock);
  }
  std::memcpy(&image[0x381], top.data(), 20);
  const crackdown::Sha1Digest content_id = crackdown::Sha1Of(&image[0x344], kBase - 0x344);
  std::memcpy(&image[0x32C], content_id.data(), 20);

  if (options.corrupt_data_byte_of_item >= 0) {
    image[Physical(start[size_t(options.corrupt_data_byte_of_item)]) + 3] ^= 0x5A;
  }
  if (options.corrupt_header) {
    image[0x360 + 3] ^= 0x01;
  }
  if (options.corrupt_level0_table) {
    image[Level0Table(0) + 30] ^= 0x01;
  }
  if (options.corrupt_top_hash) {
    image[0x381] ^= 0x01;
  }
  image.resize(image.size() - options.truncate);
  return image;
}

inline Bytes Pattern(size_t size, uint32_t seed) {
  Bytes data(size);
  uint32_t x = seed * 2654435761u + 1;
  for (uint8_t& b : data) {
    x = x * 1664525u + 1013904223u;
    b = uint8_t(x >> 24);
  }
  return data;
}

inline void WriteBytes(const std::filesystem::path& path, const Bytes& data) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}

inline Bytes ReadBytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace stfs_test
