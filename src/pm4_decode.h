#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

#include "hle_records.h"

// PM4 the title writes (its ring, its command buffer segments) reduced to what the draw records need: state writes,
// draws, shader loads, bin (tile) predication and indirect buffers. Pure decoding from guest physical memory,
// shared by the record walker (hle_capture.cpp) and its tests.
namespace crackdown::pm4 {

// The largest indirect buffer (in dwords) the walk follows.
inline constexpr uint32_t kMaxIndirectDwords = 0x10000;

// The type-3 opcodes the title's D3D library and engine emit (HLE_RENDERER_FEASIBILITY.md 1.2), and NOP: the walk
// stops at anything else, which means the ring holds data (vertices of an unhooked call), not packets.
inline bool KnownOpcode(uint32_t opcode) {
  switch (opcode) {
    case 0x10: case 0x21: case 0x22: case 0x25: case 0x27: case 0x2B: case 0x2D: case 0x2F: case 0x36: case 0x37:
    case 0x3B: case 0x3C: case 0x3E: case 0x3F: case 0x45: case 0x46: case 0x48: case 0x50: case 0x51: case 0x54:
    case 0x58: case 0x5A: case 0x5B: case 0x60: case 0x61: case 0x62: case 0x63: case 0x64:
      return true;
    default:
      return false;
  }
}

// A walked packet reduced to what the records need. kBin: `code` the opcode, `first`/`count` its two data dwords;
// kWrite: `code` the HleStateSpace, `values` an offset in the value pool; kIndirect: `first` address, `count` dwords.
// kDraw: `code` the opcode, `first` VGT_DRAW_INITIATOR, `count` VGT_DMA_BASE, `values` VGT_DMA_SIZE; kShader: `code`
// 0 vertex / 1 pixel, `first` the microcode's guest physical address, `count` its dwords.
struct WalkOp {
  enum Kind : uint8_t { kBin, kWrite, kIndirect, kDraw, kShader } kind;
  uint8_t code;
  bool predicated;
  uint32_t first;
  uint32_t count;
  uint32_t values;
};

struct DecodedPm4 {
  std::vector<WalkOp> ops;
  std::vector<uint32_t> pool;  // big-endian values, as in guest memory
};

inline void AddWrite(const uint8_t* physical_base, DecodedPm4& out, HleStateSpace space, bool predicated,
                     uint32_t first, uint32_t count, uint32_t physical) {
  const uint32_t at = uint32_t(out.pool.size());
  const uint32_t* values = reinterpret_cast<const uint32_t*>(physical_base + (physical & 0x1FFFFFFF));
  out.pool.insert(out.pool.end(), values, values + count);
  out.ops.push_back({WalkOp::kWrite, uint8_t(space), predicated, first, count, at});
}

// Why a decode stopped before the end of its buffer (diagnostics): the packet's address, index and header.
using DecodeStop = void (*)(uint32_t at, uint32_t index, uint32_t dwords, uint32_t header);

// Decodes `dwords` of PM4 at a guest physical address (physical_base: guest physical memory, big-endian), up to the
// first packet the title does not emit.
inline void DecodePm4(const uint8_t* physical_base, uint32_t physical, uint32_t dwords, DecodedPm4& out,
                      DecodeStop on_stop = nullptr) {
  auto ReadPhysical = [physical_base](uint32_t address) {
    uint32_t value;
    std::memcpy(&value, physical_base + (address & 0x1FFFFFFF), sizeof(value));
    return std::byteswap(value);
  };
  for (uint32_t i = 0; i < dwords;) {
    const uint32_t at = physical + 4 * i;
    const uint32_t header = ReadPhysical(at);
    const uint32_t type = header >> 30;
    if (type == 2) {
      ++i;
      continue;
    }
    if (type == 1) {  // two single-register writes below 0x800: none the records keep
      i += 3;
      continue;
    }
    const uint32_t count = ((header >> 16) & 0x3FFF) + 1;
    if (i + 1 + count > dwords || (type == 0 && (header & 0x7FFF) + count > 0x5100) ||
        (type == 3 && !KnownOpcode((header >> 8) & 0x7F))) {
      if (on_stop) on_stop(at, i, dwords, header);
      return;
    }
    if (type == 0) {
      const uint32_t first = header & 0x7FFF;
      const bool one_register = (header >> 15) & 1;
      if (first >= 0x2000 && first < 0x2400) {
        // A one-register run writes every value to the same register: the last one stays.
        AddWrite(physical_base, out, HleStateSpace::kRegister, false, first - 0x2000, one_register ? 1 : count,
                 at + 4 * (one_register ? count : 1));
      } else if (!one_register && first >= 0x4000 && first < 0x4800) {
        AddWrite(physical_base, out, HleStateSpace::kFloat, false, first - 0x4000, count, at + 4);
      } else if (!one_register && first >= 0x4800 && first < 0x48C0) {
        AddWrite(physical_base, out, HleStateSpace::kFetch, false, first - 0x4800, count, at + 4);
      } else if (first >= 0x4900 && first < 0x4928) {
        AddWrite(physical_base, out, HleStateSpace::kBoolLoop, false, first - 0x4900, one_register ? 1 : count,
                 at + 4 * (one_register ? count : 1));
      }
    } else {
      const uint32_t opcode = (header >> 8) & 0x7F;
      const bool predicated = header & 1;
      auto data = [&](uint32_t n) { return ReadPhysical(at + 4 * (1 + n)); };
      if (opcode == 0x50 || opcode == 0x51) {
        out.ops.push_back({WalkOp::kBin, uint8_t(opcode), predicated, data(0), count >= 2 ? data(1) : 0, 0});
      } else if (opcode >= 0x60 && opcode <= 0x63) {
        out.ops.push_back({WalkOp::kBin, uint8_t(opcode), predicated, data(0), 0, 0});
      } else if (opcode == 0x2D && count >= 2) {  // SET_CONSTANT: offset and type, then the values
        const uint32_t offset_type = data(0);
        const uint32_t space = (offset_type >> 16) & 0xFF;
        const uint32_t first = offset_type & 0x7FF;
        if (space == 0) AddWrite(physical_base, out, HleStateSpace::kFloat, predicated, first, count - 1, at + 8);
        if (space == 1) AddWrite(physical_base, out, HleStateSpace::kFetch, predicated, first, count - 1, at + 8);
        if (space == 4) AddWrite(physical_base, out, HleStateSpace::kRegister, predicated, first, count - 1, at + 8);
        if (space == 2) AddWrite(physical_base, out, HleStateSpace::kBoolLoop, predicated, first, count - 1, at + 8);
        if (space == 3) AddWrite(physical_base, out, HleStateSpace::kBoolLoop, predicated, 8 + first, count - 1, at + 8);
      } else if (opcode == 0x2F && count >= 3) {  // LOAD_ALU_CONSTANT: address, offset and type, size
        const uint32_t address = data(0) & 0x1FFFFFFF;
        const uint32_t offset_type = data(1);
        const uint32_t size = data(2) & 0xFFF;
        const uint32_t space = (offset_type >> 16) & 0xFF;
        if (space == 0) AddWrite(physical_base, out, HleStateSpace::kFloat, predicated, offset_type & 0x7FF, size, address);
        if (space == 1) AddWrite(physical_base, out, HleStateSpace::kFetch, predicated, offset_type & 0x7FF, size, address);
      } else if (opcode == 0x22 && count >= 2) {  // DRAW_INDX: viz query, initiator, [DMA base, DMA size]
        const uint32_t initiator = data(1);
        const bool dma = ((initiator >> 6) & 3) == 0;
        out.ops.push_back({WalkOp::kDraw, uint8_t(opcode), predicated, initiator,
                           dma && count >= 4 ? data(2) : 0, dma && count >= 4 ? data(3) : 0});
      } else if (opcode == 0x36 && count >= 1) {  // DRAW_INDX_2: initiator, immediate indices
        out.ops.push_back({WalkOp::kDraw, uint8_t(opcode), predicated, data(0), 0, 0});
      } else if (opcode == 0x27 && count >= 2) {  // IM_LOAD: address | type, start << 16 | size
        out.ops.push_back({WalkOp::kShader, uint8_t(data(0) & 3), predicated, data(0) & 0x1FFFFFFC,
                           data(1) & 0xFFFF, 0});
      } else if (opcode == 0x2B && count >= 2) {  // IM_LOAD_IMMEDIATE: type, start << 16 | size, microcode
        out.ops.push_back({WalkOp::kShader, uint8_t(data(0) & 3), predicated, (at + 12) & 0x1FFFFFFF,
                           data(1) & 0xFFFF, 0});
      } else if ((opcode == 0x3F || opcode == 0x37) && count >= 2 && (data(1) & 0xFFFFF) <= kMaxIndirectDwords) {
        out.ops.push_back({WalkOp::kIndirect, uint8_t(opcode), predicated, data(0) & 0x1FFFFFFF,
                           data(1) & 0xFFFFF, 0});
      }
    }
    i += 1 + count;
  }
}

}  // namespace crackdown::pm4
