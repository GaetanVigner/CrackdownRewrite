// Native translations, batch 10: 25 loop-free leaf functions of 19-82 instructions. Same rules as the earlier batches.
#include "decomp_hooks.h"

#include <cmath>
#include <cstring>
#include <limits>

#include "guest_memory.h"

namespace crackdown::hooks::decomp {

namespace {

using guest::Load32;
using guest::Load8;
using guest::LoadFloat;
using guest::Store32;
using guest::Store8;
using guest::StoreFloat;

uint16_t Load16(uint8_t* base, uint32_t addr) {
  uint16_t value;
  std::memcpy(&value, rex::memory::GuestPtr<const uint8_t*>(base, addr), sizeof(value));
  return std::byteswap(value);
}

void Store16(uint8_t* base, uint32_t addr, uint16_t value) {
  value = std::byteswap(value);
  std::memcpy(rex::memory::GuestPtr<uint8_t*>(base, addr), &value, sizeof(value));
}

uint64_t Load64(uint8_t* base, uint32_t addr) {
  uint64_t value;
  std::memcpy(&value, rex::memory::GuestPtr<const uint8_t*>(base, addr), sizeof(value));
  return std::byteswap(value);
}

void Store64(uint8_t* base, uint32_t addr, uint64_t value) {
  value = std::byteswap(value);
  std::memcpy(rex::memory::GuestPtr<uint8_t*>(base, addr), &value, sizeof(value));
}

double LoadDouble(uint8_t* base, uint32_t addr) {
  const uint64_t bits = Load64(base, addr);
  double value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

void StoreDouble(uint8_t* base, uint32_t addr, double value) {
  uint64_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  Store64(base, addr, bits);
}

int64_t TruncateToInt64(double value) {
  if (std::isnan(value)) return std::numeric_limits<int64_t>::min();
  if (value > double(std::numeric_limits<int64_t>::max())) return std::numeric_limits<int64_t>::max();
  if (value < -9223372036854775808.0) return std::numeric_limits<int64_t>::min();
  return int64_t(value);
}

uint32_t ShiftLeft(uint32_t value, uint64_t amount) { return (uint8_t(amount) & 0x20) ? 0 : value << (uint8_t(amount) & 0x3F); }

float Add(float a, float b) { return float(double(a) + double(b)); }
float Mul(float a, float b) { return float(double(a) * double(b)); }

void Accumulate(uint8_t* base, uint32_t obj, double value) {
  Store16(base, obj + 74, uint16_t(Load16(base, obj + 74) + 1));
  StoreDouble(base, obj + 56, value + LoadDouble(base, obj + 56));
}

constexpr uint32_t kOne = 0x82000A94, kDefault = 0x820722DC, kModeGlobal = 0x8309153C;

}  // namespace

// f1 = a random value in [f1, f2]: the object's LCG at +572 (its +576 cleared), the draw times (f2 - f1) times the
// constant at 0x820ED814, plus f1.
void Sub_82140C90(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  const float range = float(ctx.f2.f64 - ctx.f1.f64);
  const float scale = LoadFloat(base, 0x820ED814);
  Store32(base, obj + 576, 0);
  const uint32_t seed = Load32(base, obj + 572) * 0x41C64E6Du + 12345u;
  Store32(base, obj + 572, seed);
  const float draw = float(double(int64_t((seed >> 16) & 0x7FFF)));
  const float scaled = Mul(draw, range);
  ctx.f1.f64 = double(float(std::fma(double(scaled), double(scale), ctx.f1.f64)));
}

// Accumulates two bytes (+120, +112) and two floats (+108, +104) into the double at +56; the counter advances by
// four.
void Sub_82199AA8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store16(base, obj + 74, uint16_t(Load16(base, obj + 74) + 4));
  double sum = double(Load8(base, obj + 120)) + double(Load8(base, obj + 112));
  sum += double(LoadFloat(base, obj + 108));
  sum += double(LoadFloat(base, obj + 104));
  sum += LoadDouble(base, obj + 56);
  StoreDouble(base, obj + 56, sum);
}

// Accumulates two bytes (+105, +104) and two floats (+112, +108) into the double at +56; the counter advances by four.
void Sub_821A9C00(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store16(base, obj + 74, uint16_t(Load16(base, obj + 74) + 4));
  double sum = double(Load8(base, obj + 105)) + double(Load8(base, obj + 104));
  sum += double(LoadFloat(base, obj + 112));
  sum += double(LoadFloat(base, obj + 108));
  sum += LoadDouble(base, obj + 56);
  StoreDouble(base, obj + 56, sum);
}

// Constructor of a 400-byte curve set: vtable 0x8208A76C, an embedded array header at +16 (pointer to +32, count 0,
// capacity 8 with the top bit), eight groups of eight floats set to the constant 0x820ED650, +4 from 0x82071A84.
void Sub_822979C8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, k = Load32(base, 0x820ED650);
  Store32(base, obj + 0, 0x8208A76C);
  Store32(base, obj + 20, 0);
  Store32(base, obj + 24, 0x80000008u);
  Store32(base, obj + 16, obj + 32);
  for (uint32_t group = 0; group < 8; ++group) {
    for (uint32_t i = 0; i < 32; i += 4) Store32(base, obj + 32 + 48 * group + i, k);
  }
  Store32(base, obj + 20, 0);
  Store32(base, obj + 4, Load32(base, 0x82071A84));
}

// Resets an object from the globals 0x82BAE748 / 0x82BAE744 (three words at +12), flags and the low five bits of +11.
void Sub_823159A0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 12, Load32(base, 0x82BAE748));
  Store32(base, obj + 16, Load32(base, 0x82BAE744));
  Store8(base, obj + 25, 0);
  Store8(base, obj + 24, 0);
  Store8(base, obj + 26, 0);
  Store32(base, obj + 20, Load32(base, 0x82BAE744));
  Store32(base, obj + 28, 0);
  Store8(base, obj + 9, 0);
  Store32(base, obj + 4, 0);
  Store8(base, obj + 10, 1);
  Store8(base, obj + 11, Load8(base, obj + 11) & 0x1F);
}

// Session (*0x82DE3EA8) with a live owner: when its flag at +143 is set and its count at +138 is below five,
// increments the count and clears the flag (the counterpart of batch 9's decrement).
void Sub_8232AD48(PPCContext&, uint8_t* base) {
  const uint32_t session = Load32(base, 0x82DE3EA8);
  if (session == 0) return;
  if (Load8(base, Load32(base, 0x82DE3EA0) + 8) == 0) return;
  if (Load8(base, session + 143) == 0) return;
  const uint32_t count = Load8(base, session + 138);
  if (count >= 5) return;
  Store8(base, session + 138, uint8_t(count + 1));
  Store8(base, session + 143, 0);
}

// Orb map view parameters from the scale f1 (the sibling of batch 1's Sub_823E9BD0 with other constants): the view
// dirty, the pan pair (+544, +548) = (rate * scale + k1) * k2, the zoom pair (+504, +508) = fma(rate * scale, k3, k4) - k4.
void Sub_823EA600(PPCContext& ctx, uint8_t* base) {
  const uint32_t view = ctx.r3.u32;
  const float scaled = float(double(LoadFloat(base, view + 860)) * ctx.f1.f64);
  const float k3 = LoadFloat(base, 0x820EDA8C), k1 = LoadFloat(base, 0x8207047C), k4 = LoadFloat(base, 0x820ED728);
  Store8(base, view + 540, 1);
  const float pan = Mul(Add(scaled, k1), LoadFloat(base, 0x820722E8));
  const float zoom = float(std::fma(double(scaled), double(k3), double(k4)));
  StoreFloat(base, view + 544, pan);
  StoreFloat(base, view + 548, pan);
  const float offset = float(double(zoom) - double(k4));
  StoreFloat(base, view + 504, offset);
  StoreFloat(base, view + 508, offset);
}

// Accumulates four bytes (+105, +104, +89, +88) and two floats (+100, +96) into the double at +56; the counter
// advances by six.
void Sub_8240C038(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store16(base, obj + 74, uint16_t(Load16(base, obj + 74) + 6));
  double sum = double(Load8(base, obj + 105)) + double(Load8(base, obj + 104));
  sum += double(Load8(base, obj + 89));
  sum += double(Load8(base, obj + 88));
  sum += double(LoadFloat(base, obj + 100));
  sum += double(LoadFloat(base, obj + 96));
  sum += LoadDouble(base, obj + 56);
  StoreDouble(base, obj + 56, sum);
}

// Starts five modes on r4 (the mode blocks at +88, +104, +120, +136, +152; counts of 200), the counter byte at +37
// advanced by five; *r3 = the mode global.
void Sub_8247A720(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r4.u32;
  for (uint32_t offset : {88u, 112u, 128u, 144u, 160u, 152u}) Store32(base, obj + offset, 1);
  Store8(base, obj + 100, 1);
  Store32(base, obj + 96, 0);
  Store16(base, obj + 92, 200);
  Store8(base, obj + 101, 0);
  Store16(base, obj + 108, 0);
  Store32(base, obj + 104, 0);
  Store16(base, obj + 124, 0);
  Store8(base, obj + 37, Load8(base, obj + 37) + 5);
  Store32(base, obj + 120, 2);
  Store16(base, obj + 140, 0);
  Store32(base, obj + 136, 2);
  Store16(base, obj + 156, 200);
  Store32(base, ctx.r3.u32, Load32(base, kModeGlobal));
}

// Copies twelve floats of the source's block (+676 -> +80, at +16..+72 skipping every fourth) into the object at
// +272..+328; nothing without a source.
void Sub_82481B08(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, source = Load32(base, obj + 676);
  if (source == 0) return;
  const uint32_t block = Load32(base, source + 80);
  for (uint32_t i = 0; i < 12; ++i) {
    const uint32_t src = 16 + 4 * i + 4 * (i / 3);
    Store32(base, obj + 272 + 4 * i + 4 * (i / 3), Load32(base, block + src));
  }
}

// Reports five fields of r3 (an int at +4, a byte at +8, a word at +12, two ints at +16 / +20) into the statistics
// block at r4+16, each as one sample.
void Sub_8258C098(PPCContext& ctx, uint8_t* base) {
  const uint32_t src = ctx.r3.u32, stats = ctx.r4.u32 + 16;
  Accumulate(base, stats, double(int64_t(int32_t(Load32(base, src + 4)))));
  Accumulate(base, stats, double(Load8(base, src + 8)));
  Accumulate(base, stats, double(Load32(base, src + 12)));
  Accumulate(base, stats, double(int64_t(int32_t(Load32(base, src + 16)))));
  Accumulate(base, stats, double(int64_t(int32_t(Load32(base, src + 20)))));
}

// Blends two colours byte by byte (r4 towards r5 by f1) into r6: byte = a + trunc((b - a) * f1), in the order
// 1, 2, 3, 0.
void Sub_825DD2B0(PPCContext& ctx, uint8_t* base) {
  const uint32_t a = ctx.r4.u32, b = ctx.r5.u32, out = ctx.r6.u32;
  for (uint32_t i : {1u, 2u, 3u, 0u}) {
    const int32_t difference = int32_t(uint32_t(Load8(base, b + i)) - uint32_t(Load8(base, a + i)));
    const float scaled = float(double(float(double(int64_t(difference)))) * ctx.f1.f64);
    const int64_t step = TruncateToInt64(double(scaled));
    Store8(base, out + i, uint8_t(uint32_t(step) + Load8(base, a + i)));
  }
}

// Constructor of a texture view from a stream cursor (*r4, aligned to 4, advanced by 68): the header pointer at
// +12, five words of it, and the texel scales (constant 0x82C63FE4 over the width at +8 and the height at +12).
void Sub_825F1890(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, cursor = ctx.r4.u32;
  Store32(base, obj + 4, 0);
  Store32(base, obj + 0, 0x820DB150);
  Store32(base, obj + 8, 0);
  const uint32_t header = (Load32(base, cursor) + 3) & 0xFFFFFFFCu;
  Store32(base, cursor, header);
  Store32(base, obj + 12, header);
  Store32(base, cursor, Load32(base, cursor) + 68);
  const uint32_t h = Load32(base, obj + 12);
  for (uint32_t i = 0; i < 5; ++i) Store32(base, obj + 16 + 4 * i, Load32(base, h + 28 + 8 * i));
  const float k = LoadFloat(base, 0x82C63FE4);
  const float height = float(double(Load32(base, h + 12)));
  const float width = float(double(Load32(base, h + 8)));
  const float inverse_height = float(double(k) / double(height));
  const float inverse_width = float(double(k) / double(width));
  StoreFloat(base, obj + 160, inverse_width);
  StoreFloat(base, obj + 168, inverse_width);
  StoreFloat(base, obj + 164, float(double(Load32(base, h + 8))));
  StoreFloat(base, obj + 176, inverse_width);
  StoreFloat(base, obj + 184, inverse_width);
  StoreFloat(base, obj + 180, inverse_height);
  StoreFloat(base, obj + 188, inverse_height);
  StoreFloat(base, obj + 172, float(double(Load32(base, h + 8))));
}

// Constructor of a 64-byte track: the argument at +0, 1.0f at +4, +40 and +44, -1 at +28 and +36, the rest cleared.
void Sub_8272A530(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, one = Load32(base, kOne);
  Store32(base, obj + 0, ctx.r4.u32);
  Store32(base, obj + 4, one);
  for (uint32_t offset : {12u, 16u, 20u, 24u, 52u, 56u, 60u}) Store32(base, obj + offset, 0);
  Store32(base, obj + 40, one);
  Store32(base, obj + 44, one);
  Store32(base, obj + 28, 0xFFFFFFFFu);
  Store16(base, obj + 32, 0);
  Store32(base, obj + 36, 0xFFFFFFFFu);
}

// Constructor: vtable 0x8200DF9C, kind 3, two zero words, 32 bytes copied from r4.
void Sub_82793DF8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, src = ctx.r4.u32;
  Store32(base, obj + 0, 0x8200DF9C);
  Store32(base, obj + 8, 0);
  Store32(base, obj + 4, 3);
  Store32(base, obj + 12, 0);
  for (uint32_t i = 0; i < 32; i += 8) Store64(base, obj + 16 + i, Load64(base, src + i));
}

// Clears a 64-byte stream descriptor, with -1 at +48 and +52.
void Sub_827C5CC8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  for (uint32_t offset = 0; offset < 64; offset += 4) Store32(base, obj + offset, 0);
  Store32(base, obj + 48, 0xFFFFFFFFu);
  Store32(base, obj + 52, 0xFFFFFFFFu);
}

// Sets channel r5 of entry r4 (36-byte entries at r3) to f1: the channel's bit in the mask at bits 20-23 of the
// entry's word at +16, its 2-bit state cleared, the float at +20 + 8 * channel; returns 0.
void Sub_828397B8(PPCContext& ctx, uint8_t* base) {
  const uint32_t table = ctx.r3.u32, channel = ctx.r5.u32;
  const uint32_t entry = uint32_t(ctx.r4.s64 * 36) + table;
  const uint32_t word = Load32(base, entry + 16) | ((ShiftLeft(1, channel) & 0xF) << 20);
  const uint32_t keep = (~ShiftLeft(3, (channel << 1) & 0xFFFFFFFEu) << 12) & 0xFFFFF000u & word;
  Store32(base, entry + 16, (word & 0xFFF00FFFu) | (keep & 0x000FF000u));
  StoreFloat(base, entry + ((channel << 3) & 0xFFFFFFF8u) + 20, float(ctx.f1.f64));
  ctx.r3.u64 = 0;
}

// Marks the pair (r4, r5) in a symmetric bit matrix (rows of +604 64-bit words at +616): bit r5 of row r4 and bit r4
// of row r5.
void Sub_8284DBD8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, a = ctx.r4.u32, b = ctx.r5.u32;
  const auto mark = [&](uint32_t row, uint32_t column) {
    const uint32_t stride = Load32(base, obj + 604), rows = Load32(base, obj + 616);
    const uint32_t address = ((uint32_t(int64_t(int32_t(row)) * int64_t(int32_t(stride))) << 3) & 0xFFFFFFF8u) + rows +
                             ((column >> 3) & 0x1FFFFFF8u);
    Store64(base, address, Load64(base, address) | (1ull << (column & 0x3F)));
  };
  mark(a, b);
  mark(b, a);
}

// Unlinks the tagged node r4 and splices r3 in its place, for nodes embedded 32 bytes into their objects (the links
// point at the objects, as batch 9's Sub_82845E00 with the offset).
void Sub_8285B180(PPCContext& ctx, uint8_t* base) {
  const uint32_t a = ctx.r3.u32, b = ctx.r4.u32, prev_slot = b + 4;
  const uint32_t prev = Load32(base, prev_slot);
  if (prev & 1) return;
  const uint32_t prev_node = prev & 0xFFFFFFFEu, next_node = Load32(base, b) & 0xFFFFFFFEu;
  Store32(base, prev_node + 32, Load32(base, a));
  Store32(base, Load32(base, a) & 0xFFFFFFFEu, prev_node);
  Store32(base, next_node, (a - 32) | 1);
  Store32(base, a, next_node);
  Store32(base, prev_slot, (b - 32) | 1);
  Store32(base, b, prev_slot | 1);
}

// Constructor (null-tolerant) with an embedded object at +20: two reference counts of 1, three vtables, +32 cleared.
void Sub_829EC830(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  if (obj == 0) return;
  Store32(base, obj + 0, 0x8204FEE8);
  Store16(base, obj + 6, 1);
  Store16(base, obj + 26, 1);
  Store32(base, obj + 28, 0x82098C28);
  Store32(base, obj + 20, 0x8204FEE0);
  Store32(base, obj + 28, 0x8204FED4);
  Store32(base, obj + 32, 0);
}

// Constructor: halfword +6 = 1, vtable 0x820501C8, five words of 1.0f, two 0x80000000 sentinels, zeros.
void Sub_829F1438(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, one = Load32(base, kOne);
  Store16(base, obj + 6, 1);
  Store32(base, obj + 0, 0x820501C8);
  for (uint32_t offset : {28u, 32u, 40u, 44u}) Store32(base, obj + offset, 0);
  Store32(base, obj + 36, 0x80000000u);
  Store32(base, obj + 48, 0x80000000u);
  for (uint32_t offset = 8; offset <= 24; offset += 4) Store32(base, obj + offset, one);
}

// Constructor of a 32-byte node with two embedded sub-objects (vtables, reference counts of 1) linked to itself.
void Sub_82AAAB88(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 0, 0x82062A20);
  Store16(base, obj + 6, 1);
  Store16(base, obj + 14, 1);
  Store32(base, obj + 8, 0x820629D4);
  Store16(base, obj + 26, 1);
  Store32(base, obj + 20, 0x82062A00);
  Store32(base, obj + 16, obj);
  Store32(base, obj + 28, obj);
}

// Resets a 88-byte stream state: words, halfwords and bytes cleared, the byte at +49 set.
void Sub_82AED2C0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  for (uint32_t offset : {28u, 32u, 80u, 60u, 52u, 56u, 72u, 20u, 40u, 84u, 64u}) Store32(base, obj + offset, 0);
  Store8(base, obj + 50, 0);
  Store8(base, obj + 48, 0);
  Store8(base, obj + 49, 1);
  Store16(base, obj + 44, 0);
  Store16(base, obj + 46, 0);
}

// Eight global float triples (48-byte stride at 0x82E4A350) set to the constant 0x820722DC.
void Sub_82B0FB40(PPCContext&, uint8_t* base) {
  const uint32_t k = Load32(base, kDefault);
  for (uint32_t group = 0; group < 8; ++group) {
    for (uint32_t i = 0; i < 12; i += 4) Store32(base, 0x82E4A350 + 48 * group + i, k);
  }
}

// 24 global floats at 0x83077E80 set to the constant 0x820722DC.
void Sub_82B22708(PPCContext&, uint8_t* base) {
  const uint32_t k = Load32(base, kDefault);
  for (uint32_t i = 0; i < 96; i += 4) Store32(base, 0x83077E80 + i, k);
}

}  // namespace crackdown::hooks::decomp
