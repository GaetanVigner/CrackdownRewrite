// Native translations, batch 8: 25 loop-free leaf functions of 17-61 instructions. Same rules as the earlier batches.
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

// fctiwz as the recompiled code performs it: NaN and values below the 32-bit range give INT32_MIN, above it INT32_MAX.
int32_t TruncateToInt32(double value) {
  if (std::isnan(value)) return std::numeric_limits<int32_t>::min();
  if (value > double(std::numeric_limits<int32_t>::max())) return std::numeric_limits<int32_t>::max();
  if (value < -2147483648.0) return std::numeric_limits<int32_t>::min();
  return int32_t(value);
}

float Add(float a, float b) { return float(double(a) + double(b)); }

constexpr uint32_t kPad = 0x820722CC, kModeGlobal = 0x8309153C;

// The camera pair setters (see batch 7): offsets of the two floats and of the field block differ per sibling.
void CameraPair(PPCContext& ctx, uint8_t* base, uint32_t floats_delta, uint32_t fields_delta) {
  const uint32_t pair = Load32(base, Load32(base, ctx.r3.u32 + 2520) + 224);
  const uint32_t block = pair + fields_delta;
  const uint32_t k = Load32(base, 0x8206F394);
  StoreFloat(base, pair + floats_delta + 440, float(ctx.f1.f64));
  Store32(base, block + 308, ctx.r4.u32);
  StoreFloat(base, pair + floats_delta + 456, float(ctx.f2.f64));
  Store8(base, block + 324, 0);
  Store32(base, block + 316, k);
  Store8(base, block + 328, 1);
  Store32(base, block + 320, k);
  Store8(base, block + 327, 0);
  Store8(base, block + 330, 1);
  Store8(base, block + 326, 0);
}

}  // namespace

// Camera target block (+2520 -> +384): the flag at +452 from r4's low byte, the 96-bit value r5:r6 at +416.
void Sub_82157A18(PPCContext& ctx, uint8_t* base) {
  const uint32_t block = Load32(base, Load32(base, ctx.r3.u32 + 2520) + 384);
  Store32(base, block + 452, ctx.r4.u8 != 0 ? 1 : 0);
  Store32(base, block + 416, uint32_t(ctx.r5.u64 >> 32));
  Store32(base, block + 420, uint32_t(ctx.r5.u64));
  Store32(base, block + 424, uint32_t(ctx.r6.u64 >> 32));
}

void Sub_821584B0(PPCContext& ctx, uint8_t* base) { CameraPair(ctx, base, uint32_t(-4), uint32_t(-52)); }
void Sub_82158540(PPCContext& ctx, uint8_t* base) { CameraPair(ctx, base, 4, 52); }

// Constructor of a 52-byte listener: an embedded node at +4 (vtable, self-linked list), two vtables, the global
// 0x82BADF60 and the constant 0x82071FE0, flag bytes cleared.
void Sub_82213708(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, node = obj + 4, k = Load32(base, 0x82071FE0);
  Store32(base, node, 0x820E62D4);
  Store32(base, obj + 16, 0);
  Store32(base, obj + 8, node);
  Store32(base, obj + 12, node);
  Store32(base, obj + 20, 0x82083EB0);
  Store8(base, obj + 24, 0);
  Store32(base, obj + 0, 0x8207BE64);
  Store32(base, obj + 20, 0x8207BE7C);
  Store32(base, obj + 28, 0);
  Store32(base, obj + 32, Load32(base, 0x82BADF60));
  Store32(base, obj + 36, k);
  Store32(base, obj + 40, k);
  for (uint32_t offset = 44; offset <= 50; ++offset) Store8(base, obj + offset, 0);
}

// f1 = the sum of the listener's fields as floats: the int at +28, the constant at 0x82BADF5C, three floats at
// +32..+40 and seven bytes at +44..+50, added in that order with single-precision rounding.
void Sub_822137E0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  float sum = Add(float(double(int64_t(int32_t(Load32(base, obj + 28))))), LoadFloat(base, 0x82BADF5C));
  for (uint32_t offset : {32u, 36u, 40u}) sum = Add(sum, LoadFloat(base, obj + offset));
  for (uint32_t offset = 44; offset <= 50; ++offset) sum = Add(sum, float(double(Load8(base, obj + offset))));
  ctx.f1.f64 = double(sum);
}

// Flag bytes of r4 (set 0x20 in +2 and 0x80 in +3, clear bit 0 of +9, bit 7 of +6, bit 2 of +10, 0x60 of +11); when
// the object's byte at +37 is clear, also bit 4 of +10, bit 5 of +9 and bit 6 of +8.
void Sub_822FC1A8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, p = ctx.r4.u32;
  const uint32_t b10 = Load8(base, p + 10), b9 = Load8(base, p + 9);
  Store8(base, p + 9, b9 & 0xFE);
  Store8(base, p + 2, Load8(base, p + 2) | 0x20);
  Store8(base, p + 3, Load8(base, p + 3) | 0x80);
  Store8(base, p + 6, Load8(base, p + 6) & 0x7F);
  Store8(base, p + 10, b10 & 0xFB);
  Store8(base, p + 11, Load8(base, p + 11) & 0x9F);
  if (Load8(base, obj + 37) != 0) return;
  Store8(base, p + 10, b10 & 0xEB);
  Store8(base, p + 9, b9 & 0xDE);
  Store8(base, p + 8, Load8(base, p + 8) & 0xBF);
}

// Flag bytes of r4: clears bit 0 of +12, sets 0x30 in +5, clears 0x38 of +10 and +8, 0x34 of +9, bit 6 of +11.
void Sub_82314E50(PPCContext& ctx, uint8_t* base) {
  const uint32_t p = ctx.r4.u32;
  Store8(base, p + 12, Load8(base, p + 12) & 0xFE);
  Store8(base, p + 5, Load8(base, p + 5) | 0x30);
  Store8(base, p + 10, Load8(base, p + 10) & 199);
  Store8(base, p + 9, Load8(base, p + 9) & 0xCB);
  Store8(base, p + 8, Load8(base, p + 8) & 199);
  Store8(base, p + 11, Load8(base, p + 11) & 0xBF);
}

// Accumulates a signed byte (+113) and a byte (+112) into the double at +56; the counter advances by two.
void Sub_82316328(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store16(base, obj + 74, uint16_t(Load16(base, obj + 74) + 2));
  const double pair = double(int8_t(Load8(base, obj + 113))) + double(Load8(base, obj + 112));
  StoreDouble(base, obj + 56, pair + LoadDouble(base, obj + 56));
}

// Profiler block (*0x82D07F9C): the object's quad at +104 copied to +5888, bit 63 of the mask at +8 set.
void Sub_823D19A8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, block = Load32(base, 0x82D07F9C);
  for (uint32_t i = 0; i < 16; i += 4) Store32(base, block + 5888 + i, Load32(base, obj + 104 + i));
  Store64(base, block + 8, Load64(base, block + 8) | 0x8000000000000000ull);
}

// Accumulates three bytes (+104, +89, +88) and two floats (+100, +96) into the double at +56; the counter advances
// by five.
void Sub_8240B8D0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store16(base, obj + 74, uint16_t(Load16(base, obj + 74) + 5));
  double sum = double(Load8(base, obj + 104)) + double(Load8(base, obj + 89));
  sum += double(Load8(base, obj + 88));
  sum += double(LoadFloat(base, obj + 100));
  sum += double(LoadFloat(base, obj + 96));
  sum += LoadDouble(base, obj + 56);
  StoreDouble(base, obj + 56, sum);
}

// Starts three modes on r4 (1 with 200, 1, 2 with 1), the counter byte at +37 advanced by three; *r3 = the mode
// global.
void Sub_82474118(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r4.u32;
  Store32(base, obj + 96, 0);
  Store8(base, obj + 101, 0);
  Store16(base, obj + 108, 0);
  Store32(base, obj + 104, 0);
  Store16(base, obj + 124, 0);
  Store16(base, obj + 92, 200);
  Store32(base, obj + 88, 1);
  Store8(base, obj + 100, 1);
  Store32(base, obj + 112, 1);
  Store32(base, obj + 128, 1);
  Store32(base, obj + 120, 2);
  Store8(base, obj + 37, Load8(base, obj + 37) + 3);
  Store32(base, ctx.r3.u32, Load32(base, kModeGlobal));
}

// f1 = the sum of nine floats of the object (+4..+32, +40) and the constant at 0x82C626C0, in the original's order.
void Sub_824C55E8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  float sum = Add(LoadFloat(base, obj + 4), LoadFloat(base, 0x82C626C0));
  for (uint32_t offset : {8u, 12u, 16u, 20u, 24u, 28u, 32u, 40u}) sum = Add(sum, LoadFloat(base, obj + offset));
  ctx.f1.f64 = double(sum);
}

// Constructor of a 72-byte settings block: counts of 272 and 271, flags, vtable 0x82083EB0.
void Sub_82581CF8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 4, 4);
  for (uint32_t offset : {20u, 24u, 28u, 32u, 36u}) Store32(base, obj + offset, 272);
  Store16(base, obj + 12, 1);
  Store16(base, obj + 14, 1);
  Store8(base, obj + 16, 1);
  Store32(base, obj + 40, 271);
  Store32(base, obj + 44, 271);
  Store32(base, obj + 48, 2);
  Store16(base, obj + 52, 0);
  Store16(base, obj + 54, 1);
  Store8(base, obj + 57, 1);
  Store8(base, obj + 58, 1);
  for (uint32_t offset = 60; offset <= 66; ++offset) Store8(base, obj + offset, 0);
  Store16(base, obj + 68, 0);
  Store32(base, obj + 8, 0);
  Store32(base, obj + 0, 0x82083EB0);
}

// Stores four 64-bit values (r4..r7) into the object: r4 at +208, r5 at +216, r6 at +192, r7 at +200.
void Sub_8259FD50(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store64(base, obj + 208, ctx.r4.u64);
  Store64(base, obj + 216, ctx.r5.u64);
  Store64(base, obj + 192, ctx.r6.u64);
  Store64(base, obj + 200, ctx.r7.u64);
}

// Copies 18 words of a matrix with words 3 and 7 replaced by the pad constant (as batch 2's element copy).
void Sub_825FBDB8(PPCContext& ctx, uint8_t* base) {
  const uint32_t dst = ctx.r3.u32, src = ctx.r4.u32, pad = Load32(base, kPad);
  for (uint32_t word = 0; word < 18; ++word) {
    Store32(base, dst + 4 * word, (word == 3 || word == 7) ? pad : Load32(base, src + 4 * word));
  }
}

// Default stream parameters: four image constants and the counts 1200 and 444.
void Sub_82740710(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 100, Load32(base, 0x820023C8));
  Store32(base, obj + 108, 1200);
  Store32(base, obj + 120, 444);
  Store32(base, obj + 104, Load32(base, 0x82000A88));
  Store32(base, obj + 112, Load32(base, 0x82000B84));
  Store32(base, obj + 116, Load32(base, 0x82000A94));
}

// Constructor: vtable 0x8200E020, kind 14, twelve zero words, 3 at +16.
void Sub_82794E68(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 4, 14);
  Store32(base, obj + 0, 0x8200E020);
  for (uint32_t offset = 8; offset < 64; offset += 4) Store32(base, obj + offset, 0);
  Store32(base, obj + 16, 3);
}

// Reads an n-bit field (r5 bits) ending at bit r4 of a little-endian bit stream at r3 (the shifts as the original's
// slw / srw, which give 0 for amounts of 32 or more).
void Sub_82832BF8(PPCContext& ctx, uint8_t* base) {
  const uint32_t buffer = ctx.r3.u32, bit = ctx.r4.u32, count = ctx.r5.u32;
  const uint32_t byte_index = (bit - 1) >> 3;
  const uint32_t p = byte_index + buffer;
  const uint32_t word = Load8(base, p) | (uint32_t(Load8(base, p + 1)) << 8) | (uint32_t(Load8(base, p + 2)) << 16) |
                        (uint32_t(Load8(base, p + 3)) << 24);
  const uint64_t left = uint64_t(byte_index * 8) - ctx.r4.u64 - ctx.r5.u64 + 33;
  const uint64_t right = 32 - ctx.r5.u64;
  const uint32_t shifted = (uint8_t(left) & 0x20) ? 0 : (word << (uint8_t(left) & 0x3F));
  ctx.r3.u64 = (uint8_t(right) & 0x20) ? 0 : (shifted >> (uint8_t(right) & 0x3F));
}

// Permutes the four 2-bit fields at bits 5-12 of *r3 by the selector r4 (two bits of selector per field).
void Sub_8285E988(PPCContext& ctx, uint8_t* base) {
  const uint32_t p = ctx.r3.u32, selector = ctx.r4.u32, word = Load32(base, p);
  const uint32_t fields = (word >> 5) & 0xFF;
  const uint32_t s9 = fields >> ((selector >> 5) & 6), s8 = fields >> ((selector >> 3) & 6),
                 s7 = fields >> ((selector >> 1) & 6), s11 = fields >> ((selector << 1) & 6);
  const uint32_t low = ((s9 & 3) << 2) | (s8 & 3);
  const uint32_t mid = (low << 2) | (s7 & 3);
  const uint32_t packed = (mid << 2) | (s11 & 3);
  Store32(base, p, (packed << 5) | (word & 0xFFFFE01Fu));
}

// Constructor: vtable 0x820B8650, 1 at +12, zeros, 0x7FFFFFFF at +8.
void Sub_8294DDD0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 12, 1);
  Store32(base, obj + 0, 0x820B8650);
  for (uint32_t offset = 16; offset <= 40; offset += 4) Store32(base, obj + offset, 0);
  Store32(base, obj + 4, 0);
  Store32(base, obj + 8, 0x7FFFFFFFu);
}

// Constructor (null-tolerant): halfword +6 = 1, vtable 0x8204ED9C, eight words from the constant 0x820ED650.
void Sub_829DC450(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, k = Load32(base, 0x820ED650);
  if (obj == 0) return;
  Store16(base, obj + 6, 1);
  Store32(base, obj + 0, 0x8204ED9C);
  for (uint32_t offset = 32; offset <= 60; offset += 4) Store32(base, obj + offset, k);
}

// Constructor of a 24-byte proxy on r4: 1 at +8, r4's word at +4 copied to +12, two vtables, two zero words.
void Sub_82A4BF98(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 8, 1);
  Store32(base, obj + 4, 0x8205581C);
  Store32(base, obj + 12, Load32(base, ctx.r4.u32 + 4));
  Store32(base, obj + 0, 0x8205593C);
  Store32(base, obj + 4, 0x82055920);
  Store32(base, obj + 16, 0);
  Store32(base, obj + 20, 0);
}

// r3 = min(r4, (k1 / f1) * k2 + half, truncated to int) with the image's constants; the comparison is signed 32-bit,
// the result the unsigned truncation or r4 itself.
void Sub_82A97790(PPCContext& ctx, uint8_t* base) {
  const float quotient = float(double(LoadFloat(base, 0x8205AFB0)) / ctx.f1.f64);
  const float scaled = float(std::fma(double(quotient), double(LoadFloat(base, 0x8205EA10)), double(LoadFloat(base, 0x82000BAC))));
  const uint32_t value = uint32_t(TruncateToInt32(double(scaled)));
  if (int32_t(value) <= ctx.r4.s32) {
    ctx.r3.u64 = value;
    return;
  }
  ctx.r3.u64 = ctx.r4.u64;
}

// Default colour set at 0x82CFEA28 from the two constants at 0x82BAE054 / 0x82BAE058.
void Sub_82B0FEC0(PPCContext&, uint8_t* base) {
  const uint32_t a = Load32(base, 0x82BAE054), b = Load32(base, 0x82BAE058);
  for (uint32_t offset : {12u, 20u, 36u, 44u, 52u, 60u, 76u, 84u}) Store32(base, 0x82CFEA28 + offset, a);
  Store32(base, 0x82CFEA28 + 4, b);
  Store32(base, 0x82CFEA28 + 28, b);
}

// Registers the global object at 0x82D300B4: its pointer at 0x82D300F8, the value at 0x82D300F4 stored through the
// vtable's RTTI offset into the object, and bit 1 set in the word at +16 of that offset.
void Sub_82B52010(PPCContext&, uint8_t* base) {
  const uint32_t global = 0x82D300B4;
  Store32(base, 0x82D300F8, global);
  Store32(base, global + 44 + Load32(base, Load32(base, global) + 4), Load32(base, 0x82D300F4));
  const uint32_t at = global + Load32(base, Load32(base, global) + 4);
  Store32(base, at + 16, Load32(base, at + 16) | 2);
}

}  // namespace crackdown::hooks::decomp
