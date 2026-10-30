// Native translations, batch 6: 25 loop-free leaf functions of 15-54 instructions. Same rules as the earlier batches.
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

double LoadDouble(uint8_t* base, uint32_t addr) {
  const uint64_t bits = Load64(base, addr);
  double value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

void StoreDouble(uint8_t* base, uint32_t addr, double value) {
  uint64_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  bits = std::byteswap(bits);
  std::memcpy(rex::memory::GuestPtr<uint8_t*>(base, addr), &bits, sizeof(bits));
}

// fctidz as the recompiled code performs it (see batch 4).
int64_t TruncateToInt64(double value) {
  if (std::isnan(value)) return std::numeric_limits<int64_t>::min();
  if (value > double(std::numeric_limits<int64_t>::max())) return std::numeric_limits<int64_t>::max();
  if (value < -9223372036854775808.0) return std::numeric_limits<int64_t>::min();
  return int64_t(value);
}

constexpr uint32_t kOne = 0x82000A94;

}  // namespace

// Twelve words of the object set to the constant at 0x820ED650 (a 3x4 matrix filled with one value).
void Sub_8213E3C8(PPCContext& ctx, uint8_t* base) {
  const uint32_t k = Load32(base, 0x820ED650);
  for (uint32_t i = 0; i < 48; i += 4) Store32(base, ctx.r3.u32 + i, k);
}

// *r7 = distance between the points at r5+4 and r6+4 (three floats); returns 1.
void Sub_82185E40(PPCContext& ctx, uint8_t* base) {
  const uint32_t a = ctx.r5.u32, b = ctx.r6.u32;
  const float dx = float(double(LoadFloat(base, a + 4)) - double(LoadFloat(base, b + 4)));
  const float dy = float(double(LoadFloat(base, a + 8)) - double(LoadFloat(base, b + 8)));
  const float dz = float(double(LoadFloat(base, a + 12)) - double(LoadFloat(base, b + 12)));
  float sum = float(double(dx) * double(dx));
  sum = float(std::fma(double(dy), double(dy), double(sum)));
  sum = float(std::fma(double(dz), double(dz), double(sum)));
  StoreFloat(base, ctx.r7.u32, float(std::sqrt(double(sum))));
  ctx.r3.u64 = 1;
}

// Accumulates a byte (+112) and two floats (+108, +104) into the double at +56; the sample counter at +74 advances by
// three.
void Sub_8218DC98(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store16(base, obj + 74, uint16_t(Load16(base, obj + 74) + 3));
  double sum = double(Load8(base, obj + 112)) + double(LoadFloat(base, obj + 108));
  sum += double(LoadFloat(base, obj + 104));
  sum += LoadDouble(base, obj + 56);
  StoreDouble(base, obj + 56, sum);
}

// Resets an object: +28 cleared, +32 from the global 0x82BADF60, +36 / +40 from the constant 0x82071FE0, seven bytes
// at +44 cleared.
void Sub_82213798(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, k = Load32(base, 0x82071FE0);
  Store32(base, obj + 28, 0);
  Store32(base, obj + 32, Load32(base, 0x82BADF60));
  Store32(base, obj + 36, k);
  Store32(base, obj + 40, k);
  for (uint32_t offset = 44; offset <= 50; ++offset) Store8(base, obj + offset, 0);
}

// Copies three flag bits of the source's byte at +36 into the destination's byte at +1: bit 7 as is, bits 6 and 5
// shifted down one (to 0x20 and 0x10).
void Sub_822E9A98(PPCContext& ctx, uint8_t* base) {
  const uint32_t src = ctx.r3.u32, dst = ctx.r4.u32 + 1;
  const uint32_t s = Load8(base, src + 36);
  Store8(base, dst, (s & 0x80) | (Load8(base, dst) & 0x7F));
  Store8(base, dst, ((s >> 1) & 0x20) | (Load8(base, dst) & 0xDF));
  Store8(base, dst, ((s >> 1) & 0x10) | (Load8(base, dst) & 0xEF));
}

// Flag bytes of r4 (set 0x40 in +3, clear bit 0 of +9, bit 7 of +6, 0x60 of +11, bit 2 of +10), then the owner's
// (+16) byte at +424 takes the object's byte at +36.
void Sub_822F9FA0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, p = ctx.r4.u32;
  Store8(base, p + 3, Load8(base, p + 3) | 0x40);
  Store8(base, p + 9, Load8(base, p + 9) & 0xFE);
  Store8(base, p + 6, Load8(base, p + 6) & 0x7F);
  Store8(base, p + 11, Load8(base, p + 11) & 0x9F);
  Store8(base, p + 10, Load8(base, p + 10) & 0xFB);
  Store8(base, Load32(base, obj + 16) + 424, Load8(base, obj + 36));
}

// Fills r4 from the owner (+16): bytes +293 / +292 at +10 / +11, +292 again as a word at +12, the halfword at +300
// at +20, and the object's word at +112 at +16.
void Sub_82303670(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, out = ctx.r4.u32, owner = Load32(base, obj + 16);
  Store8(base, out + 10, Load8(base, owner + 293));
  Store8(base, out + 11, Load8(base, owner + 292));
  Store32(base, out + 12, Load8(base, owner + 292));
  Store16(base, out + 20, Load16(base, owner + 300));
  Store32(base, out + 16, Load32(base, obj + 112));
}

namespace {

// Appends slot j (32-byte entries at obj + slots) to the list of bucket i (104-byte buckets at obj + buckets, the
// count byte at +100 of the bucket, pointers after it).
void AppendSlot(uint8_t* base, uint32_t obj, uint32_t i, uint32_t j, uint32_t buckets, uint32_t slots) {
  const uint32_t bucket = (i & 0xFF) * 104 + obj + buckets;
  const uint32_t count = Load8(base, bucket + 100);
  Store32(base, bucket + (((count + 1) << 2) & 0xFFFFFFFCu), ((j << 5) & 0x1FE0) + obj + slots);
  Store8(base, bucket + 100, Load8(base, bucket + 100) + 1);
}

}  // namespace

void Sub_823CA8F0(PPCContext& ctx, uint8_t* base) { AppendSlot(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, 588, 328); }
void Sub_823CAA10(PPCContext& ctx, uint8_t* base) { AppendSlot(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, 4692, 3920); }

// Accumulates two bytes (+89, +88) into the double at +56; the sample counter advances by two.
void Sub_8240B648(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store16(base, obj + 74, uint16_t(Load16(base, obj + 74) + 2));
  const double pair = double(Load8(base, obj + 89)) + double(Load8(base, obj + 88));
  StoreDouble(base, obj + 56, pair + LoadDouble(base, obj + 56));
}

// Starts two modes on r4 (the pair of mode blocks at +88 and +104: 1 and 2, counters cleared, the counter byte at
// +37 advanced by two); *r3 = the global 0x8309153C.
void Sub_82476C50(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r4.u32;
  Store16(base, obj + 92, 0);
  Store32(base, obj + 88, 0);
  Store8(base, obj + 101, 0);
  Store16(base, obj + 108, 0);
  Store32(base, obj + 104, 0);
  Store8(base, obj + 117, 0);
  Store32(base, obj + 96, 1);
  Store32(base, obj + 112, 2);
  Store8(base, obj + 37, Load8(base, obj + 37) + 2);
  Store32(base, ctx.r3.u32, Load32(base, 0x8309153C));
}

// In state 3, counts a frame (+8, 16-bit) and, when it reaches the definition's (+24) count at +8, wraps to 0 and
// raises the done flag at +20.
void Sub_82581ED0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  if (Load32(base, obj + 4) != 3) return;
  const uint16_t count = uint16_t(Load16(base, obj + 8) + 1);
  Store16(base, obj + 8, count);
  if (count != Load16(base, Load32(base, obj + 24) + 8)) return;
  Store16(base, obj + 8, 0);
  Store8(base, obj + 20, 1);
}

// Resets the target state of r4 (words and bytes around +2268, the constant 0x82C63D5C at +2284, -1 at +1932);
// *r3 = the global 0x830D218C.
void Sub_82597840(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r4.u32;
  Store32(base, obj + 2268, 0);
  Store32(base, obj + 2272, 0);
  Store8(base, obj + 2282, 0);
  Store32(base, obj + 2284, Load32(base, 0x82C63D5C));
  Store8(base, obj + 2281, 0);
  Store8(base, obj + 1960, 0);
  Store32(base, obj + 1932, 0xFFFFFFFFu);
  Store32(base, obj + 2240, 0);
  Store32(base, ctx.r3.u32, Load32(base, 0x830D218C));
}

// Three random floats into r4: the global LCG at 0x82CFDF48 (seed * 0x41C64E6D + 12345, the top 15 bits of the new
// seed as the value), scaled by the constant at 0x820EDB48 minus the one at 0x82BAB364; the word after the seed is
// cleared before the third draw.
void Sub_826011F8(PPCContext& ctx, uint8_t* base) {
  const uint32_t out = ctx.r4.u32;
  const float scale = LoadFloat(base, 0x820EDB48);
  for (uint32_t i = 0; i < 3; ++i) {
    const uint32_t seed = Load32(base, 0x82CFDF48) * 0x41C64E6Du + 12345u;
    Store32(base, 0x82CFDF48, seed);
    const uint32_t draw = (seed >> 16) & 0x7FFF;
    if (i == 2) Store32(base, 0x82CFDF4C, 0);
    const float value = float(double(int64_t(draw)));
    StoreFloat(base, out + 4 * i,
               float(std::fma(double(value), double(scale), -double(LoadFloat(base, 0x82BAB364)))));
  }
}

// Accumulates a word (+88, unsigned), two floats (+92, +96) and two bytes (+100, +101) into the double at +56; the
// sample counter at +74 advances by five.
void Sub_82652B30(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  double sum = LoadDouble(base, obj + 56);
  sum = double(Load32(base, obj + 88)) + sum;
  sum = double(LoadFloat(base, obj + 92)) + sum;
  sum = double(LoadFloat(base, obj + 96)) + sum;
  sum = double(Load8(base, obj + 100)) + sum;
  sum = double(Load8(base, obj + 101)) + sum;
  StoreDouble(base, obj + 56, sum);
  Store16(base, obj + 74, uint16_t(Load16(base, obj + 74) + 5));
}

// Reads a little-endian float from a byte stream {offset, _, buffer} at r3 and advances the offset by 4; f1 = it.
void Sub_82726F50(PPCContext& ctx, uint8_t* base) {
  const uint32_t stream = ctx.r3.u32, offset = Load32(base, stream);
  Store32(base, stream, offset + 4);
  const uint32_t bits = std::byteswap(Load32(base, Load32(base, stream + 8) + offset));
  float value;
  std::memcpy(&value, &bits, sizeof(value));
  ctx.f1.f64 = double(value);
}

// Reads a little-endian word from the same kind of stream and advances the offset by 8; r3 = it.
void Sub_82726FC8(PPCContext& ctx, uint8_t* base) {
  const uint32_t stream = ctx.r3.u32, offset = Load32(base, stream), buffer = Load32(base, stream + 8);
  Store32(base, stream, offset + 4);
  const uint32_t word = Load32(base, buffer + offset);
  Store32(base, stream, offset + 8);
  ctx.r3.u64 = std::byteswap(word);
}

// Stamps the remaining count into the current slot (as batch 4's Sub_827F4660) once the halfword at +202 reaches
// 512; returns 0.
void Sub_82807698(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  ctx.r3.u64 = 0;
  if (Load16(base, obj + 202) < 512) return;
  const uint32_t index = Load32(base, obj + 280);
  const uint32_t slot = ((index << 2) & 0xFFFFFFFCu) + Load32(base, obj + 268);
  const uint32_t remaining = Load32(base, obj + 272) - index - 1;
  Store32(base, slot, Load32(base, slot) | ((remaining & 0xFF) << 24));
}

// Two-bit field r4 of the bit array at +624 (64-bit big-endian words): r3 = (word[bit / 32] >> (2 * bit % 64)) & 3.
void Sub_8284D670(PPCContext& ctx, uint8_t* base) {
  const uint32_t bit = (ctx.r4.u32 << 1) & 0xFFFFFFFEu;
  const uint32_t word_bit = bit & 0xFFFFFFC0u;
  const uint32_t shift = bit - word_bit;
  const uint64_t word = Load64(base, ((word_bit >> 3) & 0x1FFFFFF8u) + Load32(base, ctx.r3.u32 + 624));
  ctx.r3.u64 = uint32_t(word >> shift) & 3;
}

// out = the object's quad at +12.
void Sub_8298FA58(PPCContext& ctx, uint8_t* base) {
  for (uint32_t i = 0; i < 16; i += 4) Store32(base, ctx.r4.u32 + i, Load32(base, ctx.r3.u32 + 12 + i));
}

// Constructor: halfword +6 = 1, vtable 0x82050178, 1.0f at +8 / +12, zeros, 0x80000000 at +24.
void Sub_829F0F40(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, one = Load32(base, kOne);
  Store16(base, obj + 6, 1);
  Store32(base, obj + 0, 0x82050178);
  Store32(base, obj + 16, 0);
  Store32(base, obj + 20, 0);
  Store32(base, obj + 24, 0x80000000u);
  Store32(base, obj + 8, one);
  Store32(base, obj + 12, one);
}

// Lower-cases an ASCII letter code (r3, 16 bits) unless it is 0xFFFF, the flag at *r4 is set, or it is not A-Z.
void Sub_82AB52F0(PPCContext& ctx, uint8_t* base) {
  const uint32_t code = ctx.r3.u32 & 0xFFFF;
  if (code == 0xFFFF || Load32(base, ctx.r4.u32) != 0 || code >= 256 || code < 65 || code > 90) return;
  ctx.r3.u64 = (code + 32) & 0xFFFF;
}

// floor(f1) through fsel: the truncation, minus one (constant 0x820ED510) when it lies above the value, kept only
// while |f1| is below the large constant at 0x82063D20; f1 itself when it is NaN or zero (the original's fsel chain).
void Sub_82AB5FD0(PPCContext& ctx, uint8_t* base) {
  const double x = ctx.f1.f64;
  const double truncated = double(TruncateToInt64(x));
  double magnitude;
  {
    uint64_t bits;
    std::memcpy(&bits, &x, 8);
    bits &= ~0x8000000000000000ull;
    std::memcpy(&magnitude, &bits, 8);
  }
  const double room = LoadDouble(base, 0x82063D20) - magnitude;
  const double negated = -magnitude;
  const double adjusted = truncated + LoadDouble(base, 0x820ED510);
  double result = (truncated - x) >= 0.0 ? truncated : adjusted;
  result = room >= 0.0 ? result : x;
  ctx.f1.f64 = negated >= 0.0 ? x : result;
}

// Installs a vtable pointer into a global object (through a byte-wise copy in the original).
void Sub_82B48FE8(PPCContext&, uint8_t* base) { Store32(base, 0x82BA1DC0, 0x82047E44); }
void Sub_82B4A590(PPCContext&, uint8_t* base) { Store32(base, 0x82BA1F50, 0x8204AA08); }

}  // namespace crackdown::hooks::decomp
