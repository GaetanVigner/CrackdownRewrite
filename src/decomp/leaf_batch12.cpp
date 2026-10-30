// Native translations, batch 12: 25 loop-free leaf functions of 21-94 instructions. Same rules as the earlier batches.
#include "decomp_hooks.h"

#include <cmath>
#include <cstring>

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

float Add(float a, float b) { return float(double(a) + double(b)); }
float Mul(float a, float b) { return float(double(a) * double(b)); }

void Accumulate(uint8_t* base, uint32_t obj, double value) {
  Store16(base, obj + 74, uint16_t(Load16(base, obj + 74) + 1));
  StoreDouble(base, obj + 56, value + LoadDouble(base, obj + 56));
}

// One draw of the global object's LCG (*0x82DE2368: seed at +572, +576 cleared), as a single.
float GlobalDraw(uint8_t* base) {
  const uint32_t obj = Load32(base, 0x82DE2368);
  Store32(base, obj + 576, 0);
  const uint32_t seed = Load32(base, obj + 572) * 0x41C64E6Du + 12345u;
  Store32(base, obj + 572, seed);
  return float(double(int64_t((seed >> 16) & 0x7FFF)));
}

constexpr uint32_t kZero = 0x82000A90, kOne = 0x82000A94, kRandomScale = 0x820ED814;

}  // namespace

// f1 = a random value in [f1, f2] from the global object's LCG (the draw times (f2 - f1) times the constant at
// 0x820ED814, plus f1).
void Sub_82150C98(PPCContext& ctx, uint8_t* base) {
  const float range = float(ctx.f2.f64 - ctx.f1.f64);
  const float scale = LoadFloat(base, kRandomScale);
  const float scaled = Mul(GlobalDraw(base), range);
  ctx.f1.f64 = double(float(std::fma(double(scaled), double(scale), ctx.f1.f64)));
}

// Accumulates a byte (+112), a word (+128) and four floats (+124, +120, +108, +104) into the double at +56; the
// counter advances by six.
void Sub_8218DF60(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store16(base, obj + 74, uint16_t(Load16(base, obj + 74) + 6));
  double sum = double(Load8(base, obj + 112)) + double(Load32(base, obj + 128));
  for (uint32_t offset : {124u, 120u, 108u, 104u}) sum += double(LoadFloat(base, obj + offset));
  sum += LoadDouble(base, obj + 56);
  StoreDouble(base, obj + 56, sum);
}

// Accumulates two bytes (+120, +112) and three floats (+124, +108, +104) into the double at +56; the counter
// advances by five.
void Sub_8219A2C0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store16(base, obj + 74, uint16_t(Load16(base, obj + 74) + 5));
  double sum = double(Load8(base, obj + 120)) + double(Load8(base, obj + 112));
  for (uint32_t offset : {124u, 108u, 104u}) sum += double(LoadFloat(base, obj + offset));
  sum += LoadDouble(base, obj + 56);
  StoreDouble(base, obj + 56, sum);
}

// Clears the flag byte at +120 and draws a random delay into +124 (the global LCG times the constant 0x820EDE04).
void Sub_8219A318(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store8(base, obj + 120, 0);
  const float draw = GlobalDraw(base);
  StoreFloat(base, obj + 124, Mul(draw, LoadFloat(base, 0x820EDE04)));
}

// Unless the owner's target has bit 7 at +9037: sets the flag at +44 and a timer at +36 = random (0..1499, from the
// global LCG at 0x82E508A4) + the global 0x8307107C + 500.
void Sub_82308DE8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  if (Load8(base, Load32(base, Load32(base, obj + 16) + 260) + 9037) & 0x80) return;
  Store8(base, obj + 44, 1);
  const uint32_t seed = Load32(base, 0x82E508A4) * 0x41C64E6Du + 12345u;
  Store32(base, 0x82E508A4, seed);
  Store32(base, 0x82E508A8, 0);
  const uint32_t draw = (seed >> 16) & 0x7FFF;
  Store32(base, obj + 36, draw - (draw / 1500) * 1500 + Load32(base, 0x8307107C) + 500);
}

// Reports three bytes of r3 (+10, the top bit of +11, +8) into the statistics block at r4+64.
void Sub_82315780(PPCContext& ctx, uint8_t* base) {
  const uint32_t src = ctx.r3.u32, stats = ctx.r4.u32 + 64;
  Accumulate(base, stats, double(Load8(base, src + 10)));
  Accumulate(base, stats, double(Load8(base, src + 11) >> 7));
  Accumulate(base, stats, double(Load8(base, src + 8)));
}

namespace {

// A packed colour r3 (0xRRGGBB) unpacked into three floats at r4, scaled by the constant 0x8206F3D4; returns 1.
void UnpackColour(PPCContext& ctx, uint8_t* base) {
  const uint32_t colour = ctx.r3.u32, out = ctx.r4.u32;
  const float scale = LoadFloat(base, 0x8206F3D4);
  StoreFloat(base, out + 0, Mul(float((colour >> 16) & 0xFF), scale));
  StoreFloat(base, out + 4, Mul(float((colour >> 8) & 0xFF), scale));
  StoreFloat(base, out + 8, Mul(float(colour & 0xFF), scale));
  ctx.r3.u64 = 1;
}

}  // namespace

void Sub_823A41D8(PPCContext& ctx, uint8_t* base) { UnpackColour(ctx, base); }

// The same with the pad constant at +12 first.
void Sub_823A4248(PPCContext& ctx, uint8_t* base) {
  Store32(base, ctx.r4.u32 + 12, Load32(base, 0x820722CC));
  UnpackColour(ctx, base);
}

// Installs the vtables of a derived listener: the primary at +0, two secondary ones through the RTTI offsets of the
// vtable at +8 (plus the adjusted offset there), two node vtables at +16 / +24, flags.
void Sub_8241BDB8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 0, 0x820AD008);
  Store32(base, obj + Load32(base, Load32(base, obj + 8) + 4) + 8, 0x820AD068);
  Store32(base, obj + Load32(base, Load32(base, obj + 8) + 8) + 8, 0x820AD070);
  const uint32_t offset = Load32(base, Load32(base, obj + 8) + 4);
  Store32(base, obj + offset + 4, offset - 148);
  Store32(base, obj + 16, 0x820ACE24);
  Store32(base, obj + 20, 0);
  Store32(base, obj + 24, 0x820ACE24);
  Store32(base, obj + 28, 0);
  Store32(base, obj + 136, 0);
  Store32(base, obj + 140, 0);
  Store8(base, obj + 132, 1);
  Store8(base, obj + 144, 0);
}

// Installs the vtables of another derived class (secondary through the RTTI offset of the vtable at +8), three
// (count, 1) pairs, the argument at +52, flags.
void Sub_8241F868(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 0, 0x820ACFB8);
  Store32(base, obj + 4, 0);
  Store32(base, obj + Load32(base, Load32(base, obj + 8) + 4) + 8, 0x820ACFB0);
  const uint32_t offset = Load32(base, Load32(base, obj + 8) + 4);
  Store32(base, obj + offset + 4, offset - 100);
  Store32(base, obj + 32, 1);
  Store32(base, obj + 28, 3);
  Store32(base, obj + 40, 1);
  Store32(base, obj + 36, 4);
  Store32(base, obj + 48, 1);
  Store32(base, obj + 44, 5);
  Store32(base, obj + 52, ctx.r4.u32);
  Store8(base, obj + 72, 1);
  for (uint32_t offset2 : {24u, 23u, 22u, 21u, 20u}) Store8(base, obj + offset2, 0);
  Store32(base, obj + 64, 0);
  Store32(base, obj + 16, 0);
}

// 36 words of the object set to the constant 0x820ED650.
void Sub_824A05D0(PPCContext& ctx, uint8_t* base) {
  const uint32_t k = Load32(base, 0x820ED650);
  for (uint32_t offset = 0; offset < 144; offset += 4) Store32(base, ctx.r3.u32 + offset, k);
}

// f1 = the sum of the object's three floats and fourteen bytes (two signed), in the original's order, as singles.
void Sub_8250F0F0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  float sum = Add(LoadFloat(base, obj + 4), LoadFloat(base, obj));
  sum = Add(sum, LoadFloat(base, obj + 8));
  sum = Add(sum, float(double(int8_t(Load8(base, obj + 12)))));
  sum = Add(sum, float(double(int8_t(Load8(base, obj + 13)))));
  for (uint32_t offset : {14u, 15u, 16u, 17u, 20u, 21u, 22u, 23u, 25u, 26u, 18u, 19u}) {
    sum = Add(sum, float(double(Load8(base, obj + offset))));
  }
  ctx.f1.f64 = double(sum);
}

// A 4x4 identity matrix from the image's 0.0f / 1.0f constants (diagonal 0.0f, the rest 1.0f: the original's layout).
void Sub_8273C678(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, zero = Load32(base, kZero), one = Load32(base, kOne);
  for (uint32_t offset : {0u, 20u, 40u, 60u}) Store32(base, obj + offset, zero);
  for (uint32_t offset : {4u, 8u, 12u, 16u, 24u, 28u, 32u, 36u, 44u, 48u, 52u, 56u}) Store32(base, obj + offset, one);
}

// Constructor: vtable 0x8200E014, kind 13, zeros, 1 at +40, four zero doubles.
void Sub_82794C58(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 4, 13);
  Store32(base, obj + 0, 0x8200E014);
  for (uint32_t offset = 8; offset <= 36; offset += 4) Store32(base, obj + offset, 0);
  Store32(base, obj + 40, 1);
  for (uint32_t offset = 48; offset <= 72; offset += 8) Store64(base, obj + offset, 0);
}

// Constructor: vtable 0x8200E020, kind 14, r4 at +16, r5 at +24, 32 bytes copied from r6 to +32.
void Sub_82794FB8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, src = ctx.r6.u32;
  Store32(base, obj + 16, ctx.r4.u32);
  Store32(base, obj + 24, ctx.r5.u32);
  Store32(base, obj + 0, 0x8200E020);
  Store32(base, obj + 8, 0);
  Store32(base, obj + 4, 14);
  Store32(base, obj + 12, 0);
  for (uint32_t i = 0; i < 32; i += 8) Store64(base, obj + 32 + i, Load64(base, src + i));
}

// Encodes a texture fetch constant header at r8 from the format word *r4, the swizzle word *r5, the optional extra
// word *r6 (when bit 23 of the format is set) and the halfword r7; returns the address after it.
void Sub_828C6468(PPCContext& ctx, uint8_t* base) {
  const uint32_t pa = ctx.r4.u32, pb = ctx.r5.u32, pc = ctx.r6.u32, out = ctx.r8.u32;
  Store16(base, out + 2, 3);
  Store32(base, out, Load32(base, out) & 0xE000FFFFu);
  Store16(base, out + 6, uint16_t(ctx.r7.u32));
  Store32(base, out + 4, (Load32(base, out + 4) & 0xFF00FFFFu) | 0x00040000u);
  Store16(base, out + 10, 0);
  Store32(base, out + 8, (Load32(base, out + 8) & 0xFF00FFFFu) | 0x00410000u);
  Store8(base, out + 8, 1);
  Store32(base, out + 12, (Load32(base, out + 12) & 0xFFFF9999u) | 0x1111u);
  const uint32_t p = out + 16;
  Store16(base, p + 2, uint16_t(Load32(base, pa)));
  const uint32_t w = Load32(base, p), a = Load32(base, pa);
  const uint32_t merged = (w & 0xFFC0FFFFu) | (a & 0x003F0000u);
  Store32(base, p, merged);
  Store32(base, p, ((merged & 0xFF7FFFFFu) | (Load32(base, pa) & 0x00800000u)) | 0x00400000u);
  const uint32_t q = out + 20;
  for (uint32_t mask : {7u, 0x70u, 0x700u, 0x7000u}) {
    Store32(base, q, (Load32(base, q) & ~mask) | (Load32(base, pb) & mask));
  }
  for (uint32_t mask : {8u, 0x80u, 0x800u, 0x8000u}) {
    Store32(base, q, (Load32(base, q) & ~mask) | (~Load32(base, pb) & mask));
  }
  if ((Load32(base, pa) & 0x00800000u) == 0) {
    ctx.r3.u64 = out + 24;
    return;
  }
  Store32(base, out + 24, Load32(base, pc));
  ctx.r3.u64 = out + 28;
}

// Constructor (null-tolerant): halfword +6 = 1, vtable 0x8204A678, four (0, 0, 0x80000000) triples.
void Sub_82987A58(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  if (obj == 0) return;
  Store16(base, obj + 6, 1);
  Store32(base, obj + 40, 0);
  for (uint32_t offset : {56u, 80u, 92u, 288u}) {
    Store32(base, obj + offset, 0);
    Store32(base, obj + offset + 4, 0);
    Store32(base, obj + offset + 8, 0x80000000u);
  }
  Store32(base, obj + 0, 0x8204A678);
}

// Constructor: halfword +6 = 1, vtable 0x8204A934, four (0, 0, 0x80000000) triples and two zero words.
void Sub_8298A330(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store16(base, obj + 6, 1);
  Store32(base, obj + 0, 0x8204A934);
  for (uint32_t offset : {8u, 20u, 32u, 44u}) {
    Store32(base, obj + offset, 0);
    Store32(base, obj + offset + 4, 0);
    Store32(base, obj + offset + 8, 0x80000000u);
  }
  Store32(base, obj + 56, 0);
  Store32(base, obj + 60, 0);
}

// Constructor of a handle on r4 with r5: the constant 0x82000B88 at +20, vtable 0x8204AA68, three flag bytes; the
// target's reference count at +6 grows when its +4 is non-zero.
void Sub_8298C970(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 12, ctx.r4.u32);
  Store32(base, obj + 16, ctx.r5.u32);
  Store32(base, obj + 20, Load32(base, 0x82000B88));
  Store16(base, obj + 6, 1);
  Store32(base, obj + 0, 0x8204AA68);
  for (uint32_t offset : {24u, 25u, 26u}) Store8(base, obj + offset, 0);
  const uint32_t target = Load32(base, obj + 12);
  Store32(base, obj + 28, 0);
  if (Load16(base, target + 4) == 0) return;
  Store16(base, target + 6, Load16(base, target + 6) + 1);
}

// Constructor of an 80-byte emitter: the argument at +4, vtable 0x82053C10, 0.0f at +12, the rest cleared.
void Sub_82A151E8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 4, ctx.r4.u32);
  Store32(base, obj + 0, 0x82053C10);
  Store32(base, obj + 12, Load32(base, kZero));
  for (uint32_t offset : {16u, 20u, 28u, 36u, 40u, 48u, 52u, 56u, 60u, 64u, 68u, 72u, 76u}) Store32(base, obj + offset, 0);
}

// f1 = log2 of f1 (as a single), approximated: the mantissa as [1, 2) times a constant minus zero, times another,
// plus the unbiased exponent plus one.
void Sub_82A95278(PPCContext& ctx, uint8_t* base) {
  const float value = float(ctx.f1.f64);
  uint32_t bits;
  std::memcpy(&bits, &value, 4);
  const uint32_t mantissa_bits = (bits & 0x007FFFFFu) | 0x3F800000u;
  const int32_t exponent = int32_t((uint32_t(int32_t(bits) >> 23) & 0xFF) - 127 + 1);
  float mantissa;
  std::memcpy(&mantissa, &mantissa_bits, 4);
  const float k1 = LoadFloat(base, 0x82000BAC), k2 = LoadFloat(base, 0x82000A8C), zero = LoadFloat(base, kZero);
  const float scaled = float(std::fma(double(mantissa), double(k1), -double(zero)));
  const float e = float(double(int64_t(exponent)));
  ctx.f1.f64 = double(float(std::fma(double(scaled), double(k2), double(e))));
}

// Request r4 in state 12: sets the slot's 9-bit field (bits 6-14) of the table (*r3 -> +12, index r4[1]) to r5 + 1;
// when r5 is 1, the request moves to state 11 with the slot's low byte as its value.
void Sub_82AD8038(PPCContext& ctx, uint8_t* base) {
  const uint32_t owner = ctx.r3.u32, request = ctx.r4.u32, k = ctx.r5.u32;
  if (Load32(base, request) != 12) return;
  const uint32_t table = Load32(base, Load32(base, owner) + 12);
  const uint32_t slot = table + ((Load32(base, request + 4) << 2) & 0xFFFFFFFCu);
  Store32(base, slot, (Load32(base, slot) & ~0x7FC0u) | (((k + 1) << 6) & 0x7FC0u));
  if (ctx.r5.s32 != 1) return;
  const uint32_t index = (Load32(base, request + 4) << 2) & 0xFFFFFFFCu;
  Store32(base, request, 11);
  Store32(base, request + 4, Load8(base, index + Load32(base, Load32(base, owner) + 12)));
}

// Ten global triples at 0x82E4E320 set to (-7, -6, -6).
void Sub_82B11740(PPCContext&, uint8_t* base) {
  for (uint32_t i = 0; i < 10; ++i) {
    Store32(base, 0x82E4E320 + 12 * i, 0xFFFFFFF9u);
    Store32(base, 0x82E4E320 + 12 * i + 4, 0xFFFFFFFAu);
    Store32(base, 0x82E4E320 + 12 * i + 8, 0xFFFFFFFAu);
  }
}

// Three global triples at 0x82D19E70 set to (0.0f, 1.0f, 1.0f, 1.0f) rows from the image constants.
void Sub_82B47DA0(PPCContext&, uint8_t* base) {
  const uint32_t zero = Load32(base, kZero), one = Load32(base, kOne);
  for (uint32_t row = 0; row < 3; ++row) {
    Store32(base, 0x82D19E70 + 16 * row, zero);
    Store32(base, 0x82D19E70 + 16 * row + 4, one);
    Store32(base, 0x82D19E70 + 16 * row + 8, one);
    Store32(base, 0x82D19E70 + 16 * row + 12, one);
  }
}

// A global 4x4 of the image constants at 0x82D19D80: 0.0f on the diagonal, 1.0f elsewhere.
void Sub_82B47E60(PPCContext&, uint8_t* base) {
  const uint32_t zero = Load32(base, kZero), one = Load32(base, kOne);
  for (uint32_t i = 0; i < 16; ++i) Store32(base, 0x82D19D80 + 4 * i, (i % 5 == 0) ? zero : one);
}

}  // namespace crackdown::hooks::decomp
