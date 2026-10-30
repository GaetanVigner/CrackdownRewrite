// Native translations, batch 5: 25 loop-free leaf functions of 14-40 instructions. Same rules as the earlier batches.
#include "decomp_hooks.h"

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

constexpr uint32_t kZero = 0x82000A90, kOne = 0x82000A94;  // the image's 0.0f / 1.0f
constexpr uint32_t kPad = 0x820722CC, kDefault = 0x820722DC;  // two float constants of the image

}  // namespace

// Stores f1..f4 as floats at +396..+408 of element r4 (412 bytes) of the object's array at +32.
void Sub_821F39B0(PPCContext& ctx, uint8_t* base) {
  const uint32_t element = Load32(base, ctx.r3.u32 + 32) + uint32_t(ctx.r4.s64 * 412);
  StoreFloat(base, element + 396, float(ctx.f1.f64));
  StoreFloat(base, element + 400, float(ctx.f2.f64));
  StoreFloat(base, element + 404, float(ctx.f3.f64));
  StoreFloat(base, element + 408, float(ctx.f4.f64));
}

// Flag bytes of r4: sets 0x21 in +2, clears bit 0 of +9, bit 7 of +6, 0x60 of +11, bit 2 of +10.
void Sub_822FB238(PPCContext& ctx, uint8_t* base) {
  const uint32_t p = ctx.r4.u32;
  Store8(base, p + 2, Load8(base, p + 2) | 0x21);
  Store8(base, p + 9, Load8(base, p + 9) & 0xFE);
  Store8(base, p + 6, Load8(base, p + 6) & 0x7F);
  Store8(base, p + 11, Load8(base, p + 11) & 0x9F);
  Store8(base, p + 10, Load8(base, p + 10) & 0xFB);
}

// +588 = the global's count (*0x82DE2674 -> +628 -> +32, signed) times the constant at 0x820722D0.
void Sub_823FA9A8(PPCContext& ctx, uint8_t* base) {
  const int32_t count = int32_t(Load32(base, Load32(base, Load32(base, 0x82DE2674) + 628) + 32));
  const float scaled = float(double(int64_t(count)));
  StoreFloat(base, ctx.r3.u32 + 588, float(double(scaled) * double(LoadFloat(base, 0x820722D0))));
}

// *r3 = 1; the object's +16 advances by 19, its target (+64) takes the word at r5+5 at +400, +792 counts one more.
void Sub_82473790(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r4.u32, target = Load32(base, obj + 64);
  Store32(base, ctx.r3.u32, 1);
  Store32(base, obj + 16, Load32(base, obj + 16) + 19);
  Store32(base, target + 400, Load32(base, ctx.r5.u32 + 5));
  Store32(base, obj + 792, Load32(base, obj + 792) + 1);
}

// Advances a parse cursor (*r4) over a 52-byte header aligned to 4 plus 4 * its count at +12, storing the end of the
// header at its +48.
void Sub_825F6CC8(PPCContext& ctx, uint8_t* base) {
  const uint32_t cursor = ctx.r4.u32;
  const uint32_t header = (Load32(base, cursor) + 3) & 0xFFFFFFFCu;
  const uint32_t after = header + 52;
  Store32(base, cursor, after);
  Store32(base, header + 48, after);
  Store32(base, cursor, after + ((Load32(base, header + 12) << 2) & 0xFFFFFFFCu));
}

namespace {

// Two constructors of the same class differ by one vtable: the secondary vtable at +4 through the offset read from
// the primary's RTTI data (*(+4 of the vtable) + 4).
void ConstructListener(uint8_t* base, uint32_t obj, uint32_t secondary) {
  Store32(base, obj + 4, 0x820E7480);
  Store32(base, obj + 40, 0x820E61E8);
  Store32(base, obj + 44, 0);
  Store32(base, 0x82DE44A8, Load32(base, 0x82DE44A8) + 1);
  Store32(base, obj + 0, 0x820E69FC);
  Store32(base, Load32(base, Load32(base, obj + 4) + 4) + obj + 4, 0x820E7488);
  Store32(base, obj + 0, 0x820E6BDC);
  Store32(base, Load32(base, Load32(base, obj + 4) + 4) + obj + 4, secondary);
  for (uint32_t offset : {12u, 16u, 20u, 28u, 32u, 36u}) Store32(base, obj + offset, 0);
}

}  // namespace

void Sub_82694200(PPCContext& ctx, uint8_t* base) { ConstructListener(base, ctx.r3.u32, 0x820E6848); }
void Sub_82626BE8(PPCContext& ctx, uint8_t* base) { ConstructListener(base, ctx.r3.u32, 0x820E6BE0); }

// Constructor: vtable 0x8200DFE4, kind 9, zero fields, 9 at +20.
void Sub_827944D8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 4, 9);
  Store32(base, obj + 0, 0x8200DFE4);
  for (uint32_t offset : {8u, 12u, 16u, 24u, 28u, 32u}) Store32(base, obj + offset, 0);
  Store32(base, obj + 20, 9);
}

// Constructor: the same class with five arguments at +16..+32.
void Sub_82794510(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 16, ctx.r4.u32);
  Store32(base, obj + 20, ctx.r5.u32);
  Store32(base, obj + 24, ctx.r6.u32);
  Store32(base, obj + 28, ctx.r7.u32);
  Store32(base, obj + 32, ctx.r8.u32);
  Store32(base, obj + 8, 0);
  Store32(base, obj + 12, 0);
  Store32(base, obj + 4, 9);
  Store32(base, obj + 0, 0x8200DFE4);
}

// Writes one byte of a lookup table: table at r3+328 (+128), indexed by (scale (*(r4+80), s16) * *r5 (s16) + 4) >> 3
// masked to 10 bits; the byte goes to *r6 + r7.
void Sub_8281B120(PPCContext& ctx, uint8_t* base) {
  const int32_t scale = int16_t(Load16(base, Load32(base, ctx.r4.u32 + 80)));
  const int32_t value = int16_t(Load16(base, ctx.r5.u32));
  const int32_t product = int32_t(int64_t(scale) * int64_t(value)) + 4;
  const uint32_t index = uint32_t(product >> 3) & 0x3FF;
  const uint8_t byte = Load8(base, index + Load32(base, ctx.r3.u32 + 328) + 128);
  Store8(base, Load32(base, ctx.r6.u32) + ctx.r7.u32, byte);
}

// Constructor of a handle on r4: +8 = r4, byte +12 = r5, halfword +6 = 1, vtable 0x820529F4; the target's reference
// count at +6 grows when its +4 is non-zero.
void Sub_82953330(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, target = ctx.r4.u32;
  Store32(base, obj + 8, target);
  Store8(base, obj + 12, ctx.r5.u8);
  Store16(base, obj + 6, 1);
  Store32(base, obj + 0, 0x820529F4);
  if (Load16(base, target + 4) == 0) return;
  Store16(base, target + 6, Load16(base, target + 6) + 1);
}

// Constructor: vtable 0x82058870, 1.0f at +8, the rest cleared.
void Sub_82A792E0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 0, 0x82058870);
  Store32(base, obj + 8, Load32(base, kOne));
  Store8(base, obj + 4, 0);
  for (uint32_t offset : {20u, 24u, 28u, 12u, 16u}) Store32(base, obj + offset, 0);
}

// Clears ten global words at 0x830BCC28 / 0x830BCCC8.
void Sub_82B37038(PPCContext&, uint8_t* base) {
  for (uint32_t i = 0; i < 40; i += 4) Store32(base, 0x830BCC28 + i, 0);
}

void Sub_82B37118(PPCContext&, uint8_t* base) {
  for (uint32_t i = 0; i < 40; i += 4) Store32(base, 0x830BCCC8 + i, 0);
}

// Sets the flag byte at +189; when set and the object's list at +64 is empty (its end pointer 16 bytes past the
// head), also sets +188.
void Sub_8216F4E8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store8(base, obj + 189, ctx.r4.u8);
  if (ctx.r4.u8 == 0) return;
  const uint32_t head = obj + 64;
  if (((Load32(base, head) - head - 16) & 0xFFFFFFFCu) != 0) return;
  Store8(base, obj + 188, 1);
}

// Resets an object from the globals at 0x82BAE748 / 0x82BAE744 (three words), four bytes, a halfword and a word.
void Sub_822EB240(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 36, Load32(base, 0x82BAE748));
  Store32(base, obj + 40, Load32(base, 0x82BAE744));
  Store32(base, obj + 44, Load32(base, 0x82BAE744));
  for (uint32_t offset : {48u, 49u, 50u, 51u}) Store8(base, obj + offset, 0);
  Store16(base, obj + 26, 0);
  Store32(base, obj + 28, 0);
}

// Resets an object from the constant 0x820722DC at +20 (the sibling of batch 4's Sub_82315E00 with +12 cleared).
void Sub_82315CE0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 20, Load32(base, kDefault));
  Store8(base, obj + 10, 1);
  Store32(base, obj + 12, 0);
  Store8(base, obj + 16, 0);
  Store8(base, obj + 9, 0);
  Store32(base, obj + 4, 0);
  Store8(base, obj + 11, Load8(base, obj + 11) & 0x1F);
}

// Starts a mode on r4 (+88 = 1, +100 = 1, +96 = 0, +92 = 100, +101 = 0, the counter byte incremented); *r3 = the
// global 0x8309153C.
void Sub_82479668(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r4.u32;
  Store32(base, obj + 88, 1);
  Store8(base, obj + 100, 1);
  Store32(base, obj + 96, 0);
  Store16(base, obj + 92, 100);
  Store8(base, obj + 101, 0);
  Store8(base, obj + 37, Load8(base, obj + 37) + 1);
  Store32(base, ctx.r3.u32, Load32(base, 0x8309153C));
}

// Profiler block (*0x82D07F9C): clears the word at +10292, the low six bits of +10436, and sets bits 7 and 44 of
// the 64-bit mask at +16.
void Sub_825D3808(PPCContext&, uint8_t* base) {
  const uint32_t block = Load32(base, 0x82D07F9C);
  Store32(base, block + 10292, 0);
  Store32(base, block + 10436, Load32(base, block + 10436) & 0xFFFFFFC0u);
  Store64(base, block + 16, Load64(base, block + 16) | 0x80 | (1ull << 44));
}

// Latches pending values of r4: +16 = +28 and +60 = +40 always; when the pending flag at +48 is set, also
// +56 = +44, +24 = +32, +12 = +36, and the flag clears.
void Sub_828FEE28(PPCContext& ctx, uint8_t* base) {
  const uint32_t p = ctx.r4.u32;
  Store32(base, p + 16, Load32(base, p + 28));
  Store32(base, p + 60, Load32(base, p + 40));
  if (Load8(base, p + 48) == 0) return;
  Store32(base, p + 56, Load32(base, p + 44));
  Store32(base, p + 24, Load32(base, p + 32));
  Store32(base, p + 12, Load32(base, p + 36));
  Store8(base, p + 48, 0);
}

// Constructor (null-tolerant): halfword +6 = 1, vtable 0x8204FEE0, secondary vtable 0x8204FED4 at +8.
void Sub_829EBFF8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  if (obj == 0) return;
  Store16(base, obj + 6, 1);
  Store32(base, obj + 8, 0x82098C28);
  Store32(base, obj + 0, 0x8204FEE0);
  Store32(base, obj + 8, 0x8204FED4);
}

// Sets a rate f1 in [1.0, 0.0]-bounded form on the object: +68 = f1 and +256 = 0.0 / (rate at +60 * f1), only when
// f1 is not below 1.0 nor above 0.0 as the image's constants read (NaN passes both tests, as the original).
void Sub_82A3F0A0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  const double value = ctx.f1.f64;
  if (value < double(LoadFloat(base, kOne))) return;
  const float zero = LoadFloat(base, kZero);
  if (value > double(zero)) return;
  const float product = float(double(LoadFloat(base, obj + 60)) * value);
  StoreFloat(base, obj + 68, float(value));
  StoreFloat(base, obj + 256, float(double(zero) / double(product)));
}

// Self-link at +432, flag +436 cleared, the constant 0x820722DC at +416, +420 and +424 (and the uninitialised stack
// word the original copies to +428, taken as 0).
void Sub_821F3320(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 432, obj);
  const uint32_t k = Load32(base, kDefault);
  Store8(base, obj + 436, 0);
  Store32(base, obj + 416, k);
  Store32(base, obj + 420, k);
  Store32(base, obj + 424, k);
  Store32(base, obj + 428, 0);
}

// A 4x4 float matrix set to the identity pattern of two image constants (0x820722CC on the diagonal of rows 0-2,
// 0x820722DC elsewhere; the last column untouched).
void Sub_82284F58(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, a = Load32(base, kDefault), b = Load32(base, kPad);
  for (uint32_t offset : {4u, 8u, 16u, 24u, 32u, 36u, 48u, 52u, 56u}) Store32(base, obj + offset, a);
  for (uint32_t offset : {0u, 20u, 40u}) Store32(base, obj + offset, b);
}

// Resets an object from the globals 0x82BAE744 (twice) and 0x82BAE734, two bytes, a halfword and a word.
void Sub_8230A578(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 36, Load32(base, 0x82BAE744));
  Store32(base, obj + 40, Load32(base, 0x82BAE744));
  Store32(base, obj + 44, Load32(base, 0x82BAE734));
  Store8(base, obj + 48, 0);
  Store8(base, obj + 49, 0);
  Store16(base, obj + 26, 0);
  Store32(base, obj + 28, 0);
}

}  // namespace crackdown::hooks::decomp
