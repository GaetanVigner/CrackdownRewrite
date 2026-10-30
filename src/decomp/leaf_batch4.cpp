// Native translations, batch 4: 25 loop-free leaf functions of 13-39 instructions. Same rules as the earlier batches.
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

// fctidz as the recompiled code performs it: NaN and values below the 64-bit range give INT64_MIN, values above it
// INT64_MAX, the rest truncate toward zero.
int64_t TruncateToInt64(double value) {
  if (std::isnan(value)) return std::numeric_limits<int64_t>::min();
  if (value > double(std::numeric_limits<int64_t>::max())) return std::numeric_limits<int64_t>::max();
  if (value < -9223372036854775808.0) return std::numeric_limits<int64_t>::min();
  return int64_t(value);
}

constexpr uint32_t kPad = 0x820722CC;  // the constant that pads float triples to a quad

void CopyWords(uint8_t* base, uint32_t dst, uint32_t src, uint32_t bytes) {
  for (uint32_t i = 0; i < bytes; i += 4) Store32(base, dst + i, Load32(base, src + i));
}

}  // namespace

// out = the triple at +12 of element j (84 bytes) of the list at +80 of element i (92 bytes) of the object's table
// at +84.
void Sub_821C5D38(PPCContext& ctx, uint8_t* base) {
  const uint32_t row = uint32_t(ctx.r5.s64 * 92) + Load32(base, ctx.r4.u32 + 84);
  const uint32_t element = Load32(base, row + 80) + uint32_t(ctx.r6.s64 * 84);
  CopyWords(base, ctx.r3.u32, element + 12, 12);
}

// The same element's triple at +64.
void Sub_821C5D70(PPCContext& ctx, uint8_t* base) {
  const uint32_t row = uint32_t(ctx.r5.s64 * 92) + Load32(base, ctx.r4.u32 + 84);
  const uint32_t element = Load32(base, row + 80) + uint32_t(ctx.r6.s64 * 84);
  CopyWords(base, ctx.r3.u32, element + 64, 12);
}

// out = {element i of the 16-byte array at r4 (from index 4), pad}.
void Sub_821681D0(PPCContext& ctx, uint8_t* base) {
  const uint32_t out = ctx.r3.u32;
  Store32(base, out + 12, Load32(base, kPad));
  const uint32_t element = (((ctx.r5.u32 + 4) << 4) & 0xFFFFFFF0u) + ctx.r4.u32;
  CopyWords(base, out, element, 12);
}

// Whether the flags at +100 of entry r4 of the object's table at +188, masked by r5, differ from the global at
// 0x82BADDC8.
void Sub_82202068(PPCContext& ctx, uint8_t* base) {
  const uint32_t entry = Load32(base, Load32(base, ctx.r3.u32 + 188) + ((ctx.r4.u32 << 2) & 0xFFFFFFFCu));
  const uint32_t masked = Load32(base, entry + 100) & ctx.r5.u32;
  ctx.r3.u64 = Load32(base, 0x82BADDC8) != masked ? 1 : 0;
}

// Flag bytes of r4: sets 0x30 in +5, clears 0x20 in +10, 0x28 in +8, 0x30 in +9.
void Sub_822ECBC0(PPCContext& ctx, uint8_t* base) {
  const uint32_t p = ctx.r4.u32;
  Store8(base, p + 5, Load8(base, p + 5) | 0x30);
  Store8(base, p + 10, Load8(base, p + 10) & 0xDF);
  Store8(base, p + 8, Load8(base, p + 8) & 0xD7);
  Store8(base, p + 9, Load8(base, p + 9) & 0xCF);
}

// Flag bytes of r4: sets 0x20 in +3 and +4, keeps 0x3E of +9, clears 0x24 in +10.
void Sub_8230EE50(PPCContext& ctx, uint8_t* base) {
  const uint32_t p = ctx.r4.u32;
  Store8(base, p + 3, Load8(base, p + 3) | 0x20);
  Store8(base, p + 4, Load8(base, p + 4) | 0x20);
  Store8(base, p + 9, Load8(base, p + 9) & 0x3E);
  Store8(base, p + 10, Load8(base, p + 10) & 0xDB);
}

// Resets the object: three bytes at +44, +36 from the global 0x82BAE744, +40 = 3, +48 / +52 / +26 / +28 cleared.
void Sub_822FE9F0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  for (uint32_t offset : {44u, 45u, 46u, 52u}) Store8(base, obj + offset, 0);
  Store32(base, obj + 36, Load32(base, 0x82BAE744));
  Store32(base, obj + 40, 3);
  Store32(base, obj + 48, 0);
  Store16(base, obj + 26, 0);
  Store32(base, obj + 28, 0);
}

// Sets state 4 when the owner (+16) has bit 2 of its byte at +289 and its target (+260) lacks bit 7 at +9037.
void Sub_82305158(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, owner = Load32(base, obj + 16);
  if (Load8(base, Load32(base, owner + 260) + 9037) & 0x80) return;
  if ((Load8(base, owner + 289) & 2) == 0) return;
  Store16(base, obj + 26, 4);
}

// Resets the object: +12 from the constant 0x820722DC, +10 = 1, +16 / +9 / +4 cleared, the top three bits of +11.
void Sub_82315E00(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 12, Load32(base, 0x820722DC));
  Store8(base, obj + 10, 1);
  Store8(base, obj + 16, 0);
  Store8(base, obj + 9, 0);
  Store32(base, obj + 4, 0);
  Store8(base, obj + 11, Load8(base, obj + 11) & 0x1F);
}

// Starts a mode on r4 (+96 = 1, +92 = 0, +88 = 2, the counter byte at +37 incremented); *r3 = the global 0x8309153C.
void Sub_824748B0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r4.u32;
  Store32(base, obj + 96, 1);
  Store16(base, obj + 92, 0);
  Store32(base, obj + 88, 2);
  Store8(base, obj + 37, Load8(base, obj + 37) + 1);
  Store32(base, ctx.r3.u32, Load32(base, 0x8309153C));
}

// Starts another mode on r4 (+96 = 0, +92 = 1000, +88 = 1, the counter byte incremented); *r3 = the same global.
void Sub_82474CA8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r4.u32;
  Store32(base, obj + 96, 0);
  Store16(base, obj + 92, 1000);
  Store32(base, obj + 88, 1);
  Store8(base, obj + 37, Load8(base, obj + 37) + 1);
  Store32(base, ctx.r3.u32, Load32(base, 0x8309153C));
}

// Resets a timer from its definition at +28: halfwords +8 / +10 cleared, +24 = definition's +52, +26 = the
// definition's +68 times 60; the mode at +20 becomes 3 when the object has no owner at +4.
void Sub_8258AEA8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, definition = Load32(base, obj + 28);
  Store16(base, obj + 8, 0);
  Store16(base, obj + 10, 0);
  Store16(base, obj + 24, Load16(base, definition + 52));
  Store16(base, obj + 26, uint16_t(Load16(base, definition + 68) * 60));
  if (Load32(base, obj + 4) != 0) return;
  Store32(base, obj + 20, 3);
}

// Caches two words of the object indexed by the global counter (*0x82D82F5C): +60 = word (counter + 73),
// +64 = word (counter + 77).
void Sub_825BFA18(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, counter = Load32(base, 0x82D82F5C);
  Store32(base, obj + 60, Load32(base, (((Load32(base, counter) + 73) << 2) & 0xFFFFFFFCu) + obj));
  Store32(base, obj + 64, Load32(base, (((Load32(base, counter) + 77) << 2) & 0xFFFFFFFCu) + obj));
}

// Reads a viewport from the device (+4): four floats at +12368.. truncated to integers into r4[0..3], then two
// floats copied; returns 0.
void Sub_8261BCF8(PPCContext& ctx, uint8_t* base) {
  const uint32_t device = Load32(base, ctx.r3.u32 + 4), out = ctx.r4.u32;
  for (uint32_t i = 0; i < 4; ++i) {
    Store32(base, out + 4 * i, uint32_t(TruncateToInt64(double(LoadFloat(base, device + 12368 + 4 * i)))));
  }
  Store32(base, out + 16, Load32(base, device + 12384));
  Store32(base, out + 20, Load32(base, device + 12388));
  ctx.r3.u64 = 0;
}

// Accumulates four fields into the double at +56 (three bytes at +88, +89, +90 and the int at +92, in that order)
// and advances the 16-bit sample counter at +74 by four.
void Sub_8264FAA0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  double sum = LoadDouble(base, obj + 56);
  sum += double(Load8(base, obj + 88));
  sum += double(Load8(base, obj + 89));
  sum += double(int64_t(int32_t(Load32(base, obj + 92))));
  sum += double(Load8(base, obj + 90));
  StoreDouble(base, obj + 56, sum);
  Store16(base, obj + 74, uint16_t(Load16(base, obj + 74) + 4));
}

// Constructor: vtable 0x8200DFA8, kind 4, two zero words, the four arguments at +16..+28.
void Sub_82793ED8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 16, ctx.r4.u32);
  Store32(base, obj + 20, ctx.r5.u32);
  Store32(base, obj + 24, ctx.r6.u32);
  Store32(base, obj + 28, ctx.r7.u32);
  Store32(base, obj + 8, 0);
  Store32(base, obj + 12, 0);
  Store32(base, obj + 4, 4);
  Store32(base, obj + 0, 0x8200DFA8);
}

// Stamps the remaining count (capacity at +272 minus index at +280, minus one) into the top byte of the current
// slot of the array at +268; returns 0.
void Sub_827F4660(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  const uint32_t index = Load32(base, obj + 280);
  const uint32_t slot = ((index << 2) & 0xFFFFFFFCu) + Load32(base, obj + 268);
  const uint32_t remaining = Load32(base, obj + 272) - index - 1;
  Store32(base, slot, Load32(base, slot) | ((remaining & 0xFF) << 24));
  ctx.r3.u64 = 0;
}

// Stores the eight arguments into the global block at 0x82D03BD0 (in the order r5, r6, r7, r3, r4, r8, r9, r10).
void Sub_82832F88(PPCContext& ctx, uint8_t* base) {
  Store32(base, 0x82D03BD0, ctx.r5.u32);
  Store32(base, 0x82D03BD4, ctx.r6.u32);
  Store32(base, 0x82D03BD8, ctx.r7.u32);
  Store32(base, 0x82D03BDC, ctx.r3.u32);
  Store32(base, 0x82D03BE0, ctx.r4.u32);
  Store32(base, 0x82D03BE4, ctx.r8.u32);
  Store32(base, 0x82D03BE8, ctx.r9.u32);
  Store32(base, 0x82D03BEC, ctx.r10.u32);
}

// Sort key difference of two entries (*r4 minus *r3): weight (+4 -> +64, low 28 bits) times count (+40), 64-bit.
void Sub_8287A968(PPCContext& ctx, uint8_t* base) {
  const auto product = [&](uint32_t entry) {
    const int64_t weight = int32_t(Load32(base, Load32(base, entry + 4) + 64) & 0x0FFFFFFFu);
    return weight * int64_t(int32_t(Load32(base, entry + 40)));
  };
  ctx.r3.s64 = product(Load32(base, ctx.r4.u32)) - product(Load32(base, ctx.r3.u32));
}

// Clears the text builder: the write cursor at 0x82D100C0 and two buffers' first byte and length.
void Sub_8293D600(PPCContext&, uint8_t* base) {
  Store32(base, 0x82D100C0, 0);
  Store8(base, 0x82D0D070, 0);
  Store32(base, 0x82D0D0B0, 0);
  Store8(base, 0x82D0D020, 0);
  Store32(base, 0x82D0D060, 0);
}

// Constructor of a heading: the "Unknown Heading" string of the image, 0, 1, 0, then 0.0f twice.
void Sub_829518F0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, zero = Load32(base, 0x82000A90);
  Store32(base, obj + 16, zero);
  Store32(base, obj + 20, zero);
  Store32(base, obj + 8, 1);
  Store32(base, obj + 0, 0x82046EF4);
  Store32(base, obj + 4, 0);
  Store32(base, obj + 12, 0);
}

// Scales a double by a power of two: adds r4 to the exponent field of f1's top halfword (bits only, no checks).
void Sub_82AC4470(PPCContext& ctx, uint8_t*) {
  const uint64_t bits = ctx.f1.u64;
  const uint32_t top = uint32_t(bits >> 48);
  const uint32_t exponent = uint32_t(((top >> 4) & 0x7FF) + ctx.r4.u64);
  const uint32_t adjusted = ((exponent << 4) & 0xFFFFFFF0u) | (top & 0x800F);
  ctx.f1.u64 = (bits & 0x0000FFFFFFFFFFFFull) | (uint64_t(adjusted & 0xFFFF) << 48);
}

// Range of the current entry: *r4 = global block (*0x82DE4F40) + its count * 272 + the object's offset at +264 + 4,
// r4[1] = that plus the length at +268.
void Sub_82AEDC80(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, out = ctx.r4.u32;
  const uint32_t block = Load32(base, 0x82DE4F40);
  const uint32_t start = Load32(base, obj + 264) + uint32_t(int64_t(Load32(base, block)) * 272) + block + 4;
  Store32(base, out, start);
  Store32(base, out + 4, Load32(base, obj + 268) + start);
}

// Default colour sets from the two constants at 0x82BAE054 / 0x82BAE058.
void Sub_82B0FDA8(PPCContext&, uint8_t* base) {
  const uint32_t a = Load32(base, 0x82BAE054), b = Load32(base, 0x82BAE058);
  for (uint32_t offset : {4u, 12u, 28u, 36u, 44u}) Store32(base, 0x82CFE934 + offset, a);
  Store32(base, 0x82CFE934 + 20, b);
}

void Sub_82B0FE18(PPCContext&, uint8_t* base) {
  const uint32_t a = Load32(base, 0x82BAE054), b = Load32(base, 0x82BAE058);
  for (uint32_t offset : {4u, 20u, 28u, 36u, 44u}) Store32(base, 0x82CFE994 + offset, a);
  Store32(base, 0x82CFE994 + 12, b);
}

}  // namespace crackdown::hooks::decomp
