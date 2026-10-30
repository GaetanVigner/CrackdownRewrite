// Native translations, batch 3: 25 small loop-free leaf functions (10-12 instructions): copies of float triples and
// quads, constructors, flag tests, a few global writes. Same rules as the earlier batches.
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

void Store16(uint8_t* base, uint32_t addr, uint16_t value) {
  value = std::byteswap(value);
  std::memcpy(rex::memory::GuestPtr<uint8_t*>(base, addr), &value, sizeof(value));
}

// The image's 1.0f at 0x82000A94 and the constant at 0x820722CC that pads float triples to a quad.
constexpr uint32_t kOne = 0x82000A94;
constexpr uint32_t kPad = 0x820722CC;

// out = {src[at], src[at + 4], src[at + 8], pad}: a float triple of an object copied as a quad.
void CopyTriple(uint8_t* base, uint32_t out, uint32_t src, uint32_t at) {
  Store32(base, out + 4, Load32(base, src + at + 4));
  Store32(base, out + 8, Load32(base, src + at + 8));
  Store32(base, out + 12, Load32(base, kPad));
  Store32(base, out + 0, Load32(base, src + at));
}

}  // namespace

void Sub_8213F180(PPCContext& ctx, uint8_t* base) { CopyTriple(base, ctx.r3.u32, ctx.r4.u32, 304); }
void Sub_821F58D0(PPCContext& ctx, uint8_t* base) { CopyTriple(base, ctx.r3.u32, ctx.r4.u32, 288); }
void Sub_824888C8(PPCContext& ctx, uint8_t* base) { CopyTriple(base, ctx.r3.u32, ctx.r4.u32, 800); }

// out = {*src, src[1], src[2], pad} from the triple at r5.
void Sub_824879B8(PPCContext& ctx, uint8_t* base) {
  const uint32_t out = ctx.r3.u32, src = ctx.r5.u32;
  for (uint32_t i = 0; i < 12; i += 4) Store32(base, out + i, Load32(base, src + i));
  Store32(base, out + 12, Load32(base, kPad));
}

// out = the object's quad at +400.
void Sub_825065E0(PPCContext& ctx, uint8_t* base) {
  for (uint32_t i = 0; i < 16; i += 4) Store32(base, ctx.r3.u32 + i, Load32(base, ctx.r4.u32 + 400 + i));
}

// out = the triple at +12 of element r5 (28 bytes each) of the object's array at +84.
void Sub_821BFCC0(PPCContext& ctx, uint8_t* base) {
  const uint32_t element = Load32(base, ctx.r4.u32 + 84) + uint32_t(ctx.r5.s64 * 28);
  for (uint32_t i = 0; i < 12; i += 4) Store32(base, ctx.r3.u32 + i, Load32(base, element + 12 + i));
}

// Links r4 to the object: when the object's flag at +556 is clear and *r4 is its id at +64, r4[1] = object - 24.
void Sub_82270B80(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, link = ctx.r4.u32;
  if (Load8(base, obj + 556) != 0) return;
  if (Load32(base, link) != Load32(base, obj + 64)) return;
  Store32(base, link + 4, obj - 24);
}

// Sets the state halfword at +26 to 4 when the owner's value (+16 -> +260 -> float at +1328) is above the
// threshold at 0x82071E28 (or either is NaN, as the original's fcmpu / blelr).
void Sub_82311448(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  const float value = LoadFloat(base, Load32(base, Load32(base, obj + 16) + 260) + 1328);
  if (value <= LoadFloat(base, 0x82071E28)) return;
  Store16(base, obj + 26, 4);
}

// Resets a counter at *r4 when it reaches the object's limit at +40; *r5 says whether it did.
void Sub_82326300(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, counter = ctx.r4.u32, flag = ctx.r5.u32;
  Store8(base, flag, 0);
  if (Load32(base, counter) >= Load32(base, obj + 40)) return;
  Store32(base, counter, 0);
  Store8(base, flag, 1);
}

// f1 = float(count at *(+152) + 576) * the constant at 0x82070E6C (the count converted through a 64-bit integer).
void Sub_82369348(PPCContext& ctx, uint8_t* base) {
  const uint32_t count = Load32(base, Load32(base, ctx.r3.u32 + 152) + 576);
  const float scaled = float(double(int64_t(count)));
  ctx.f1.f64 = double(float(double(scaled) * double(LoadFloat(base, 0x82070E6C))));
}

// r3 = 0xFFFFFFFF when the object's key at +68 is below the other's, else 1 (an ordering result, 32-bit masked).
void Sub_8253C118(PPCContext& ctx, uint8_t* base) {
  ctx.r3.u64 = Load32(base, ctx.r3.u32 + 68) < Load32(base, ctx.r4.u32 + 68) ? 0xFFFFFFFFu : 1;
}

// Timer tick: when the object is armed (+93), decrements the count at +96 and disarms it at zero.
void Sub_8265CDC8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  if (Load8(base, obj + 93) == 0) return;
  const uint32_t count = Load32(base, obj + 96) - 1;
  Store32(base, obj + 96, count);
  if (int32_t(count) != 0) return;
  Store8(base, obj + 93, 0);
}

// Constructor: five zero words, then kind 20 and the argument.
void Sub_8269B3C8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  for (uint32_t i = 8; i < 20; i += 4) Store32(base, obj + i, 0);
  Store32(base, obj + 0, 20);
  Store32(base, obj + 4, ctx.r4.u32);
}

// Constructor: five words of 1.0f and the count 3.
void Sub_8273B2F8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, one = Load32(base, kOne);
  for (uint32_t i = 0; i < 20; i += 4) Store32(base, obj + i, one);
  Store32(base, obj + 20, 3);
}

// Constructor: vtable 0x8200DF84, kind 2, the four arguments at +8, +12, +16, +20.
void Sub_82793BB8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 16, ctx.r4.u32);
  Store32(base, obj + 8, ctx.r5.u32);
  Store32(base, obj + 12, ctx.r6.u32);
  Store32(base, obj + 20, ctx.r7.u32);
  Store32(base, obj + 4, 2);
  Store32(base, obj + 0, 0x8200DF84);
}

// Reports a format: *r3 = 9, *r4 = 5, *r5 and *r6 two tables of the image; returns 0.
void Sub_8281BDD0(PPCContext& ctx, uint8_t* base) {
  Store32(base, ctx.r3.u32, 9);
  Store32(base, ctx.r4.u32, 5);
  Store32(base, ctx.r5.u32, 0x82026700);
  Store32(base, ctx.r6.u32, 0x82027700);
  ctx.r3.u64 = 0;
}

// Node constructor: zero words, the owner at +16, the top bit of the first word set, and the owner's +4 pointing
// back at the node.
void Sub_828D2700(PPCContext& ctx, uint8_t* base) {
  const uint32_t node = ctx.r3.u32, owner = ctx.r4.u32;
  for (uint32_t i = 4; i < 16; i += 4) Store32(base, node + i, 0);
  Store32(base, node + 16, owner);
  Store32(base, node + 0, 0x80000000u);
  Store32(base, owner + 4, node);
}

// Sets the byte at +144 and, when the other object's target (+36 -> +24) exists, its pair at +40 / +44 to 1.0f.
void Sub_82993038(PPCContext& ctx, uint8_t* base) {
  Store8(base, ctx.r3.u32 + 144, ctx.r5.u8);
  const uint32_t target = Load32(base, Load32(base, ctx.r4.u32 + 36) + 24);
  if (target == 0) return;
  const uint32_t one = Load32(base, kOne);
  Store32(base, target + 40, one);
  Store32(base, target + 44, one);
}

// Packs four bytes: (r6 << 24) | (r3 << 16) | (r4 << 8) | r5.
void Sub_829F1AD8(PPCContext& ctx, uint8_t* base) {
  (void)base;
  const uint32_t a = ctx.r3.u32 & 0xFF, b = ctx.r4.u32 & 0xFF, c = ctx.r5.u32 & 0xFF, d = ctx.r6.u32 & 0xFF;
  ctx.r3.u64 = (d << 24) | (a << 16) | (b << 8) | c;
}

// Stores f1 as a float at +40 of r4 and sets bit 0 of its flags at +8, when both pointers are set.
void Sub_82A85C38(PPCContext& ctx, uint8_t* base) {
  const uint32_t owner = ctx.r3.u32, target = ctx.r4.u32;
  if (owner == 0 || target == 0) return;
  const uint32_t flags = Load32(base, target + 8);
  StoreFloat(base, target + 40, float(ctx.f1.f64));
  Store32(base, target + 8, flags | 1);
}

// Clears the count at +24 and the slot (index at +12, plus 4) of the array at +8; returns 0.
void Sub_82A89BE0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 24, 0);
  Store32(base, (((Load32(base, obj + 12) + 4) << 2) & 0xFFFFFFFCu) + Load32(base, obj + 8), 0);
  ctx.r3.u64 = 0;
}

// Resets the object: byte +44 and halfword +26 and word +28 cleared, +36 / +40 from the globals at 0x82BAE734 / +16.
void Sub_822EBBB0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store8(base, obj + 44, 0);
  Store32(base, obj + 36, Load32(base, 0x82BAE734));
  Store32(base, obj + 40, Load32(base, 0x82BAE744));
  Store16(base, obj + 26, 0);
  Store32(base, obj + 28, 0);
}

// Toggles the global flag byte at 0x82BAA609; when it turns off, clears the word at 0x82DE247C.
void Sub_82258550(PPCContext&, uint8_t* base) {
  const uint32_t on = Load8(base, 0x82BAA609) == 0 ? 1 : 0;
  Store8(base, 0x82BAA609, uint8_t(on));
  if (on) return;
  Store32(base, 0x82DE247C, 0);
}

// Two default colour triples from the constants at 0x82C3E400 / 0x82C3E404.
void Sub_82B1F4A8(PPCContext&, uint8_t* base) {
  const uint32_t a = Load32(base, 0x82C3E400), b = Load32(base, 0x82C3E404);
  Store32(base, 0x8306F4A0, a);
  Store32(base, 0x8306F4A4, b);
  Store32(base, 0x8306F4A8, a);
}

void Sub_82B1F4D0(PPCContext&, uint8_t* base) {
  const uint32_t a = Load32(base, 0x82C3E400), b = Load32(base, 0x82C3E404);
  Store32(base, 0x8306F4B0, a);
  Store32(base, 0x8306F4B4, a);
  Store32(base, 0x8306F4B8, b);
}

}  // namespace crackdown::hooks::decomp
