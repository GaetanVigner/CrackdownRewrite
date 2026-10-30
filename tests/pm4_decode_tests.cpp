// The PM4 decoder of the draw records (src/pm4_decode.h): synthetic big-endian buffers in a fake guest physical
// memory, each packet the title emits checked for the op it becomes - state writes (registers, float, fetch,
// bool/loop constants), draws (DRAW_INDX with DMA indices, DRAW_INDX_2), shader loads (IM_LOAD, IM_LOAD_IMMEDIATE),
// bin predication, indirect buffers - and the stop at a packet the title does not emit.
// Run: pm4_decode_tests

#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

#include "check.h"
#include "pm4_decode.h"

namespace {

using crackdown::HleStateSpace;
using crackdown::pm4::DecodedPm4;
using crackdown::pm4::DecodePm4;
using crackdown::pm4::WalkOp;

constexpr uint32_t kBuffer = 0x1000;  // the PM4's guest physical address

uint32_t Type0(uint32_t first_register, uint32_t count, bool one_register = false) {
  return ((count - 1) << 16) | (one_register ? 0x8000u : 0u) | first_register;
}
uint32_t Type3(uint32_t opcode, uint32_t count, bool predicated = false) {
  return 0xC0000000u | ((count - 1) << 16) | (opcode << 8) | (predicated ? 1u : 0u);
}

struct Memory {
  std::vector<uint8_t> bytes = std::vector<uint8_t>(0x10000);
  uint32_t cursor = kBuffer;
  void Put(uint32_t address, uint32_t value) {
    const uint32_t big = std::byteswap(value);
    std::memcpy(bytes.data() + address, &big, 4);
  }
  void Emit(std::initializer_list<uint32_t> dwords) {
    for (uint32_t d : dwords) {
      Put(cursor, d);
      cursor += 4;
    }
  }
  uint32_t Dwords() const { return (cursor - kBuffer) / 4; }
  DecodedPm4 Decode() const {
    DecodedPm4 out;
    DecodePm4(bytes.data(), kBuffer, Dwords(), out);
    return out;
  }
};

uint32_t PoolValue(const DecodedPm4& decoded, const WalkOp& op, uint32_t k) {
  return std::byteswap(decoded.pool[op.values + k]);
}

void RegisterWrites() {
  Memory m;
  m.Emit({Type0(0x2200, 2), 0x11, 0x22});              // RB_DEPTHCONTROL, RB_BLENDCONTROL0
  m.Emit({Type0(0x2080, 3, true), 0x1, 0x2, 0x3});     // one register three times: the last stays
  m.Emit({Type3(0x2D, 3), (4u << 16) | 0x181, 0x5, 0x6});  // SET_CONSTANT registers from 0x2181
  const DecodedPm4 d = m.Decode();
  CHECK(d.ops.size() == 3);
  CHECK(d.ops[0].kind == WalkOp::kWrite && HleStateSpace(d.ops[0].code) == HleStateSpace::kRegister);
  CHECK(d.ops[0].first == 0x200 && d.ops[0].count == 2 && PoolValue(d, d.ops[0], 1) == 0x22);
  CHECK(d.ops[1].first == 0x80 && d.ops[1].count == 1 && PoolValue(d, d.ops[1], 0) == 0x3);
  CHECK(d.ops[2].first == 0x181 && d.ops[2].count == 2 && PoolValue(d, d.ops[2], 0) == 0x5);
}

void ConstantWrites() {
  Memory m;
  m.Emit({Type0(0x4004, 4), 1, 2, 3, 4});                   // VS c1
  m.Emit({Type0(0x4806, 2), 0xA, 0xB});                     // fetch slot 1
  m.Emit({Type0(0x4902, 1), 0xC});                          // bool constants 64-95
  m.Emit({Type3(0x2D, 2), (2u << 16) | 3, 0xD});            // SET_CONSTANT bool 3
  m.Emit({Type3(0x2D, 2), (3u << 16) | 5, 0xE});            // SET_CONSTANT loop 5 -> 0x4908 + 5
  m.Emit({Type3(0x2D, 3, true), (0u << 16) | 0x100, 7, 8});  // SET_CONSTANT float, predicated
  const DecodedPm4 d = m.Decode();
  CHECK(d.ops.size() == 6);
  CHECK(HleStateSpace(d.ops[0].code) == HleStateSpace::kFloat && d.ops[0].first == 4 && d.ops[0].count == 4);
  CHECK(HleStateSpace(d.ops[1].code) == HleStateSpace::kFetch && d.ops[1].first == 6);
  CHECK(HleStateSpace(d.ops[2].code) == HleStateSpace::kBoolLoop && d.ops[2].first == 2);
  CHECK(HleStateSpace(d.ops[3].code) == HleStateSpace::kBoolLoop && d.ops[3].first == 3);
  CHECK(HleStateSpace(d.ops[4].code) == HleStateSpace::kBoolLoop && d.ops[4].first == 8 + 5);
  CHECK(HleStateSpace(d.ops[5].code) == HleStateSpace::kFloat && d.ops[5].predicated && d.ops[5].count == 2);
}

void Draws() {
  Memory m;
  const uint32_t dma_initiator = 4 | (0u << 6) | (1u << 11);  // triangle list, DMA indices, 32-bit
  m.Emit({Type3(0x22, 4), 0, dma_initiator, 0x00ABC000, 0x40000123});  // DRAW_INDX: viz, initiator, base, size
  const uint32_t auto_initiator = 4 | (2u << 6);
  m.Emit({Type3(0x22, 2), 0, auto_initiator});                         // DRAW_INDX, auto indices
  m.Emit({Type3(0x36, 3), auto_initiator | (6u << 16), 0x00010002, 0x00030004});  // DRAW_INDX_2, inline data
  const DecodedPm4 d = m.Decode();
  CHECK(d.ops.size() == 3);
  CHECK(d.ops[0].kind == WalkOp::kDraw && d.ops[0].code == 0x22 && d.ops[0].first == dma_initiator);
  CHECK(d.ops[0].count == 0x00ABC000 && d.ops[0].values == 0x40000123);
  CHECK(d.ops[1].kind == WalkOp::kDraw && d.ops[1].count == 0 && d.ops[1].values == 0);
  CHECK(d.ops[2].kind == WalkOp::kDraw && d.ops[2].code == 0x36 && (d.ops[2].first & 0x3F) == 4);
}

void ShaderLoads() {
  Memory m;
  m.Emit({Type3(0x27, 2), 0x00123400 | 0, 0x00000040});  // IM_LOAD vertex: address | type, size
  m.Emit({Type3(0x27, 2), 0x00567800 | 1, 0x00000080});  // IM_LOAD pixel
  const uint32_t immediate_at = m.cursor;
  m.Emit({Type3(0x2B, 4), 1, 0x00000002, 0xDEAD0001, 0xDEAD0002});  // IM_LOAD_IMMEDIATE pixel, 2 dwords
  const DecodedPm4 d = m.Decode();
  CHECK(d.ops.size() == 3);
  CHECK(d.ops[0].kind == WalkOp::kShader && d.ops[0].code == 0 && d.ops[0].first == 0x00123400 &&
        d.ops[0].count == 0x40);
  CHECK(d.ops[1].code == 1 && d.ops[1].first == 0x00567800 && d.ops[1].count == 0x80);
  // The immediate microcode is the packet's own data, after its two header dwords.
  CHECK(d.ops[2].code == 1 && d.ops[2].first == immediate_at + 12 && d.ops[2].count == 2);
}

void PredicationAndIndirect() {
  Memory m;
  m.Emit({Type3(0x50, 2), 0x0, 0x3});        // SET_BIN_MASK
  m.Emit({Type3(0x62, 1), 0x1});             // SET_BIN_SELECT_LO
  m.Emit({Type3(0x3F, 2), 0x00200000, 64});  // INDIRECT_BUFFER
  m.Emit({Type3(0x3F, 2), 0x00300000, 0x20000});  // too large: not followed
  const DecodedPm4 d = m.Decode();
  CHECK(d.ops.size() == 3);
  CHECK(d.ops[0].kind == WalkOp::kBin && d.ops[0].code == 0x50 && d.ops[0].count == 3);
  CHECK(d.ops[1].kind == WalkOp::kBin && d.ops[1].code == 0x62 && d.ops[1].first == 1);
  CHECK(d.ops[2].kind == WalkOp::kIndirect && d.ops[2].first == 0x00200000 && d.ops[2].count == 64);
}

uint32_t g_stop_index = UINT32_MAX;

void StopsAtUnknownPacket() {
  Memory m;
  m.Emit({0x80000000});                       // type-2 filler
  m.Emit({Type0(0x2200, 1), 0x1});
  m.Emit({Type3(0x7E, 1), 0x0});              // an opcode the title does not emit: data, not packets
  m.Emit({Type0(0x2201, 1), 0x2});            // never reached
  DecodedPm4 d;
  DecodePm4(m.bytes.data(), kBuffer, m.Dwords(), d,
            [](uint32_t, uint32_t index, uint32_t, uint32_t) { g_stop_index = index; });
  CHECK(d.ops.size() == 1);
  CHECK(g_stop_index == 3);
  // A packet running past the buffer's end stops too.
  Memory short_buffer;
  short_buffer.Emit({Type0(0x2200, 4), 1, 2});
  DecodedPm4 cut;
  DecodePm4(short_buffer.bytes.data(), kBuffer, short_buffer.Dwords(), cut);
  CHECK(cut.ops.empty());
}

}  // namespace

int main() {
  RegisterWrites();
  ConstantWrites();
  Draws();
  ShaderLoads();
  PredicationAndIndirect();
  StopsAtUnknownPacket();
  return check::Finish("pm4_decode_tests");
}
