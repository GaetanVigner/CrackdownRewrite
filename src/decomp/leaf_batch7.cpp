// Native translations, batch 7: 25 loop-free leaf functions of 16-72 instructions. Same rules as the earlier batches.
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

double LoadDouble(uint8_t* base, uint32_t addr) {
  uint64_t bits;
  std::memcpy(&bits, rex::memory::GuestPtr<const uint8_t*>(base, addr), sizeof(bits));
  bits = std::byteswap(bits);
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

// One draw of a global LCG (seed * 0x41C64E6D + 12345): the top 15 bits of the new seed; the word after the seed is
// cleared.
uint32_t DrawLcg(uint8_t* base, uint32_t seed_address) {
  const uint32_t seed = Load32(base, seed_address) * 0x41C64E6Du + 12345u;
  Store32(base, seed_address, seed);
  Store32(base, seed_address + 4, 0);
  return (seed >> 16) & 0x7FFF;
}

constexpr uint32_t kDefault = 0x820722DC, kModeGlobal = 0x8309153C;

}  // namespace

// Camera target block (+2520 -> +284): the flag at +260 from r4's low byte, the 96-bit value r5:r6 (high word of
// r6) at +340.
void Sub_821577C0(PPCContext& ctx, uint8_t* base) {
  const uint32_t block = Load32(base, Load32(base, ctx.r3.u32 + 2520) + 284);
  Store32(base, block + 260, ctx.r4.u8 != 0 ? 1 : 0);
  Store32(base, block + 340, uint32_t(ctx.r5.u64 >> 32));
  Store32(base, block + 344, uint32_t(ctx.r5.u64));
  Store32(base, block + 348, uint32_t(ctx.r6.u64 >> 32));
}

namespace {

// Two sibling setters of the camera block (+2520 -> +224): a pair of floats (8 bytes apart between the siblings),
// then a word, two constants and flag bytes (104 bytes apart).
void SetCameraPair(PPCContext& ctx, uint8_t* base, uint32_t floats_at, uint32_t fields_at) {
  const uint32_t pair = Load32(base, Load32(base, ctx.r3.u32 + 2520) + 224);
  const uint32_t block = pair + fields_at;
  const uint32_t k = Load32(base, 0x8206F394);
  StoreFloat(base, pair + floats_at + 440, float(ctx.f1.f64));
  Store32(base, block + 308, ctx.r4.u32);
  StoreFloat(base, pair + floats_at + 456, float(ctx.f2.f64));
  Store8(base, block + 324, 0);
  Store32(base, block + 316, k);
  Store8(base, block + 328, 1);
  Store32(base, block + 320, k);
  Store8(base, block + 327, 0);
  Store8(base, block + 330, 1);
  Store8(base, block + 326, 0);
}

}  // namespace

void Sub_821584F8(PPCContext& ctx, uint8_t* base) { SetCameraPair(ctx, base, 0, 0); }
void Sub_82158588(PPCContext& ctx, uint8_t* base) { SetCameraPair(ctx, base, 8, 104); }

// *r4 = a random float in the object's range [+288, +292]: the global LCG at 0x82E34428 scaled by the constant at
// 0x820ED814.
void Sub_82271AD8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  const float range = float(double(LoadFloat(base, obj + 292)) - double(LoadFloat(base, obj + 288)));
  const float scale = LoadFloat(base, 0x820ED814);
  const float draw = float(double(int64_t(DrawLcg(base, 0x82E34428))));
  const float scaled = float(double(draw) * double(range));
  StoreFloat(base, ctx.r4.u32, float(std::fma(double(scaled), double(scale), double(LoadFloat(base, obj + 288)))));
}

// Flag bytes of r4: clears bit 7 of +6 and +10, bit 0 of +12, 0x60 of +8, 0x14 of +9, bit 6 of +11.
void Sub_822F9AD8(PPCContext& ctx, uint8_t* base) {
  const uint32_t p = ctx.r4.u32;
  Store8(base, p + 6, Load8(base, p + 6) & 0x7F);
  Store8(base, p + 10, Load8(base, p + 10) & 0x7F);
  Store8(base, p + 12, Load8(base, p + 12) & 0xFE);
  Store8(base, p + 8, Load8(base, p + 8) & 0x9F);
  Store8(base, p + 9, Load8(base, p + 9) & 0xEB);
  Store8(base, p + 11, Load8(base, p + 11) & 0xBF);
}

// Flag bytes of r4: sets 0x20 in +2 and 0x80 in +3, clears bit 0 of +9, bit 7 of +6, 0x60 of +11, bit 2 of +10.
void Sub_822FD2E8(PPCContext& ctx, uint8_t* base) {
  const uint32_t p = ctx.r4.u32;
  Store8(base, p + 2, Load8(base, p + 2) | 0x20);
  Store8(base, p + 3, Load8(base, p + 3) | 0x80);
  Store8(base, p + 9, Load8(base, p + 9) & 0xFE);
  Store8(base, p + 6, Load8(base, p + 6) & 0x7F);
  Store8(base, p + 11, Load8(base, p + 11) & 0x9F);
  Store8(base, p + 10, Load8(base, p + 10) & 0xFB);
}

// Flag bytes of r4: sets 0x20 in +3 and 0x40 in +4, keeps 0x3E of +9, clears 0x20 of +11 and 0x24 of +10.
void Sub_8230E280(PPCContext& ctx, uint8_t* base) {
  const uint32_t p = ctx.r4.u32;
  Store8(base, p + 3, Load8(base, p + 3) | 0x20);
  Store8(base, p + 4, Load8(base, p + 4) | 0x40);
  Store8(base, p + 9, Load8(base, p + 9) & 0x3E);
  Store8(base, p + 11, Load8(base, p + 11) & 0xDF);
  Store8(base, p + 10, Load8(base, p + 10) & 0xDB);
}

// Render state write: the value r4 into the global register shadow at 0x82D08780 (index r3 / 4) and the index's bit
// into the dirty mask words at 0x82D08910.
void Sub_8232EFB0(PPCContext& ctx, uint8_t* base) {
  const int32_t index = ctx.r3.s32 >> 2;
  Store32(base, 0x82D08780 + ((uint32_t(index) << 2) & 0xFFFFFFFCu), ctx.r4.u32);
  const uint32_t word = 0x82D08910 + ((uint32_t(index >> 5) << 2) & 0xFFFFFFFCu);
  Store32(base, word, Load32(base, word) | (1u << (uint32_t(index) & 0x1F)));
}

// Accumulates the length of the object's list at +96 (elements of 4 bytes after the 16-byte head) into the double at
// +56; the sample counter at +74 advances by one.
void Sub_823CD950(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, head = obj + 96;
  Store16(base, obj + 74, uint16_t(Load16(base, obj + 74) + 1));
  const uint32_t length = uint32_t(int32_t(Load32(base, head) - head - 16) >> 2);
  StoreDouble(base, obj + 56, double(length) + LoadDouble(base, obj + 56));
}

// Accumulates two bytes (+89, +88) and two floats (+100, +96) into the double at +56; the counter advances by four.
void Sub_8240B7B8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store16(base, obj + 74, uint16_t(Load16(base, obj + 74) + 4));
  double sum = double(Load8(base, obj + 89)) + double(Load8(base, obj + 88));
  sum += double(LoadFloat(base, obj + 100));
  sum += double(LoadFloat(base, obj + 96));
  sum += LoadDouble(base, obj + 56);
  StoreDouble(base, obj + 56, sum);
}

// Copies the mode of r4 (+96, +88, +92) into the record r6 with the default constant and a flag; *r5 = 1; *r3 = the
// mode global.
void Sub_82474CE0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r4.u32, record = ctx.r6.u32;
  Store32(base, record + 8, Load32(base, obj + 96));
  Store32(base, record + 4, Load32(base, obj + 88));
  const uint16_t half = Load16(base, obj + 92);
  Store32(base, record + 16, Load32(base, kDefault));
  Store8(base, record + 3, 1);
  Store32(base, record + 20, 0);
  Store16(base, record + 12, half);
  Store8(base, ctx.r5.u32, 1);
  Store32(base, ctx.r3.u32, Load32(base, kModeGlobal));
}

// Starts two modes on r4 (1 with 200 and 1), the counter byte at +37 advanced by two; *r3 = the mode global.
void Sub_82477E88(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r4.u32;
  Store32(base, obj + 96, 0);
  Store8(base, obj + 101, 0);
  Store16(base, obj + 108, 0);
  Store32(base, obj + 104, 0);
  Store16(base, obj + 92, 200);
  Store32(base, obj + 88, 1);
  Store8(base, obj + 100, 1);
  Store32(base, obj + 112, 1);
  Store8(base, obj + 37, Load8(base, obj + 37) + 2);
  Store32(base, ctx.r3.u32, Load32(base, kModeGlobal));
}

// Invalidates the seven cached handles of the global at 0x82DE4160 (pointers at +256 and +268..+288, word +68 = -1).
void Sub_8250ADE0(PPCContext&, uint8_t* base) {
  const uint32_t global = Load32(base, 0x82DE4160);
  for (uint32_t offset : {268u, 272u, 276u, 280u, 284u, 288u, 256u}) {
    Store32(base, Load32(base, global + offset) + 68, 0xFFFFFFFFu);
  }
}

// Accumulates a draw of the global LCG at 0x830A5B70 into the double at +56; the counter advances by one.
void Sub_825111A8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  const uint32_t draw = DrawLcg(base, 0x830A5B70);
  const double sum = LoadDouble(base, obj + 56);
  Store16(base, obj + 74, uint16_t(Load16(base, obj + 74) + 1));
  StoreDouble(base, obj + 56, double(draw) + sum);
}

// Constructor of a 208-byte render state: flags set to 1 at +16, +32, +48, +64, floats from four image constants
// (0x820722DC, 0x8207094C, 0x82071E88, 0x820709A8), 0xFFFE at +144, the rest cleared.
void Sub_826D5C28(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  const uint32_t a = Load32(base, 0x820722DC), b = Load32(base, 0x8207094C), c = Load32(base, 0x82071E88),
                 d = Load32(base, 0x820709A8);
  for (uint32_t offset : {4u, 8u, 12u, 20u, 24u, 28u, 36u, 40u, 44u, 52u, 56u, 60u, 152u, 184u, 92u, 104u, 100u, 84u, 88u,
                          108u, 112u, 128u, 136u, 116u, 140u, 148u, 120u, 124u, 96u, 68u, 72u, 76u, 80u}) {
    Store32(base, obj + offset, 0);
  }
  for (uint32_t offset : {16u, 32u, 48u, 64u}) Store8(base, obj + offset, 1);
  for (uint32_t offset : {156u, 160u, 164u, 168u, 192u, 196u, 200u, 204u}) Store32(base, obj + offset, a);
  Store32(base, obj + 172, b);
  Store32(base, obj + 176, c);
  Store32(base, obj + 180, d);
  Store32(base, obj + 144, 0xFFFE);
  Store8(base, obj + 188, 0);
}

// Constructor of a 48-byte node: the argument at +4, state 3 at +8, the rest cleared.
void Sub_82730D90(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 4, ctx.r4.u32);
  Store8(base, obj + 0, 0);
  Store8(base, obj + 1, 0);
  Store16(base, obj + 8, 3);
  for (uint32_t offset : {12u, 16u, 20u, 24u, 32u, 36u, 44u}) Store32(base, obj + offset, 0);
  Store16(base, obj + 28, 0);
  Store16(base, obj + 40, 0);
}

// Resets a stream's limits (64, 64, 0, 8192) and two flag words; returns 0.
void Sub_827C7F08(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 40, 64);
  Store32(base, obj + 44, 64);
  Store32(base, obj + 48, 0);
  Store32(base, obj + 56, 8192);
  Store32(base, obj + 108, Load32(base, obj + 108) & 0x9FFFFFFFu);
  Store32(base, obj + 112, Load32(base, obj + 112) | 0x200000u);
  ctx.r3.u64 = 0;
}

// Priority of an object: its explicit priority at +728 when set, else 2047 when the top halfword of +44 is 0xFFFF,
// else 2046.
void Sub_8283EEC8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, explicit_priority = Load32(base, obj + 728);
  if (explicit_priority != 0) {
    ctx.r3.u64 = explicit_priority;
    return;
  }
  ctx.r3.u64 = ((Load32(base, obj + 44) & 0xFFFF0000u) == 0xFFFF0000u ? 1 : 0) + 2046;
}

// Packs four 2-bit fields of r4, selected by the 8-bit selector at bits 5-12 of *r3 (two bits of selector per field),
// back into bits 5-12 of *r3.
void Sub_82846518(PPCContext& ctx, uint8_t* base) {
  const uint32_t p = ctx.r3.u32, v = ctx.r4.u32, word = Load32(base, p);
  const uint32_t selector = (word >> 5) & 0xFF;
  const uint32_t s9 = v >> ((selector >> 5) & 6), s8 = v >> ((selector >> 3) & 6), s7 = v >> ((selector >> 1) & 6),
                 s11 = v >> ((selector << 1) & 6);
  const uint32_t low = ((s9 & 3) << 2) | (s8 & 3);
  const uint32_t mid = (low << 2) | (s7 & 3);
  const uint32_t packed = (mid << 2) | (s11 & 3);
  Store32(base, p, (packed << 5) | (word & 0xFFFFE01Fu));
}

// Pops the last halfword pair of a small stack (count byte at +33, pairs at obj + 4 * n) into slot r4; the byte at
// +34 is cleared.
void Sub_8297DEB0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, dst = obj + ((ctx.r4.u32 << 2) & 0xFFFFFFFCu);
  Store8(base, obj + 34, 0);
  const uint32_t src = obj + Load8(base, obj + 33) * 4 - 4;
  Store16(base, dst, Load16(base, src));
  Store16(base, dst + 2, Load16(base, src + 2));
  Store8(base, obj + 33, Load8(base, obj + 33) - 1);
}

// Constructor (null-tolerant): halfword +6 = 1, vtable 0x820B8688; the float at +36 becomes the constant at
// 0x8204AAC8 when it is exactly 1.0f.
void Sub_8298D7B0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  if (obj == 0) return;
  const float value = LoadFloat(base, obj + 36);
  Store16(base, obj + 6, 1);
  Store32(base, obj + 0, 0x820B8688);
  if (!(value == LoadFloat(base, 0x82000A94))) return;
  Store32(base, obj + 36, Load32(base, 0x8204AAC8));
}

// *r7 = distance between the points at r5 and r6 (three floats); returns 1.
void Sub_82A24A70(PPCContext& ctx, uint8_t* base) {
  const uint32_t a = ctx.r5.u32, b = ctx.r6.u32;
  const float dx = float(double(LoadFloat(base, b)) - double(LoadFloat(base, a)));
  const float dy = float(double(LoadFloat(base, b + 4)) - double(LoadFloat(base, a + 4)));
  const float dz = float(double(LoadFloat(base, b + 8)) - double(LoadFloat(base, a + 8)));
  float sum = float(double(dx) * double(dx));
  sum = float(std::fma(double(dy), double(dy), double(sum)));
  sum = float(std::fma(double(dz), double(dz), double(sum)));
  StoreFloat(base, ctx.r7.u32, float(std::sqrt(double(sum))));
  ctx.r3.u64 = 1;
}

// Upper-cases an ASCII letter code (r3, 16 bits) unless it is 0xFFFF, the flag at *r4 is set, or it is not a-z.
void Sub_82AB52B0(PPCContext& ctx, uint8_t* base) {
  const uint32_t code = ctx.r3.u32 & 0xFFFF;
  if (code == 0xFFFF || Load32(base, ctx.r4.u32) != 0 || code >= 256 || code < 97 || code > 122) return;
  ctx.r3.u64 = (code - 32) & 0xFFFF;
}

// Installs a vtable pointer into a global object.
void Sub_82B4A610(PPCContext&, uint8_t* base) { Store32(base, 0x82BA1F68, 0x820B8688); }
void Sub_82B4A7A0(PPCContext&, uint8_t* base) { Store32(base, 0x82BA1FCC, 0x8204ADCC); }

}  // namespace crackdown::hooks::decomp
