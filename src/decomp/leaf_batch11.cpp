// Native translations, batch 11: 25 loop-free leaf functions of 20-88 instructions. Same rules as the earlier batches.
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

float Mul(float a, float b) { return float(double(a) * double(b)); }

void Accumulate(uint8_t* base, uint32_t obj, double value) {
  Store16(base, obj + 74, uint16_t(Load16(base, obj + 74) + 1));
  StoreDouble(base, obj + 56, value + LoadDouble(base, obj + 56));
}

// A sample converted through a single (fcfid then frsp in the original) before the double add.
double Rounded(int64_t value) { return double(float(double(value))); }

constexpr uint32_t kRandomScale = 0x820ED814, kDefault = 0x82071FE0;

}  // namespace

// f1 = a random value scaled by f2: the global object's LCG (*0x82DE2368, seed at +572, +576 cleared) times f2
// times the constant at 0x820ED814.
void Sub_82150CF0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = Load32(base, 0x82DE2368);
  Store32(base, obj + 576, 0);
  const uint32_t seed = Load32(base, obj + 572) * 0x41C64E6Du + 12345u;
  Store32(base, obj + 572, seed);
  const float draw = float(double(int64_t((seed >> 16) & 0x7FFF)));
  const float scaled = float(double(draw) * ctx.f2.f64);
  ctx.f1.f64 = double(Mul(scaled, LoadFloat(base, kRandomScale)));
}

namespace {

// Time left: (global frame count (*0x82DE2674 -> +628 -> +32) - start) times the constant 0x820722D0, no less than
// the floor at 0x82BAB4F8 (NaN falls through to the product, as the original's fcmpu / bgtlr).
void TimeLeft(PPCContext& ctx, uint8_t* base, float start) {
  const int32_t frame = int32_t(Load32(base, Load32(base, Load32(base, 0x82DE2674) + 628) + 32));
  const float elapsed = float(double(float(double(int64_t(frame)))) - double(start));
  const float scale = LoadFloat(base, 0x820722D0), floor = LoadFloat(base, 0x82BAB4F8);
  const float product = Mul(elapsed, scale);
  ctx.f1.f64 = double(floor > product ? floor : Mul(elapsed, scale));
}

}  // namespace

void Sub_821A17C8(PPCContext& ctx, uint8_t* base) { TimeLeft(ctx, base, LoadFloat(base, ctx.r3.u32 + 240)); }
void Sub_821BC260(PPCContext& ctx, uint8_t* base) { TimeLeft(ctx, base, LoadFloat(base, ctx.r3.u32)); }

namespace {

// Two resets of the same layout: bytes +88.. cleared (five or four), +64 cleared, +68 from the global 0x82BADF60,
// +72 / +76 from the constant 0x82071FE0, bytes +80..+86, the halfword +26 and the word +28 cleared.
void ResetTimed(uint8_t* base, uint32_t obj, uint32_t flag_bytes) {
  const uint32_t k = Load32(base, kDefault);
  for (uint32_t offset = 88; offset < 88 + flag_bytes; ++offset) Store8(base, obj + offset, 0);
  Store32(base, obj + 64, 0);
  Store32(base, obj + 68, Load32(base, 0x82BADF60));
  Store8(base, obj + 80, 0);
  Store32(base, obj + 72, k);
  Store8(base, obj + 81, 0);
  Store32(base, obj + 76, k);
  for (uint32_t offset = 82; offset <= 86; ++offset) Store8(base, obj + offset, 0);
  Store16(base, obj + 26, 0);
  Store32(base, obj + 28, 0);
}

}  // namespace

void Sub_82312AB8(PPCContext& ctx, uint8_t* base) { ResetTimed(base, ctx.r3.u32, 5); }
void Sub_823137F8(PPCContext& ctx, uint8_t* base) { ResetTimed(base, ctx.r3.u32, 4); }

// Flag bytes of r4: sets 0x60 in +7; when the object's byte at +36 is clear, also clears bits of +11, +8, +10, +9, +12.
void Sub_82315270(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, p = ctx.r4.u32;
  Store8(base, p + 7, Load8(base, p + 7) | 0x60);
  if (Load8(base, obj + 36) != 0) return;
  Store8(base, p + 11, Load8(base, p + 11) & 0x3F);
  Store8(base, p + 8, Load8(base, p + 8) & 0xDB);
  Store8(base, p + 10, Load8(base, p + 10) & 199);
  Store8(base, p + 9, Load8(base, p + 9) & 0xEE);
  Store8(base, p + 12, Load8(base, p + 12) & 0xBF);
}

// Accumulates four bytes (+97, +96, +89, +88) and two floats (+104, +100) into the double at +56; the counter
// advances by six.
void Sub_8240BAC8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store16(base, obj + 74, uint16_t(Load16(base, obj + 74) + 6));
  double sum = double(Load8(base, obj + 97)) + double(Load8(base, obj + 96));
  sum += double(Load8(base, obj + 89));
  sum += double(Load8(base, obj + 88));
  sum += double(LoadFloat(base, obj + 104));
  sum += double(LoadFloat(base, obj + 100));
  sum += LoadDouble(base, obj + 56);
  StoreDouble(base, obj + 56, sum);
}

// Stores f1 into the global grid at 0x82DE2A2C: row r4 (252 floats), cell r5 (0..24, laid out as 10 * (cell / 5) +
// cell % 5).
void Sub_82445008(PPCContext& ctx, uint8_t* base) {
  const uint32_t cell = ctx.r5.u32 & 0xFF;
  const uint32_t index = 10 * (cell / 5) + cell % 5 + uint32_t(ctx.r4.s64 * 252);
  StoreFloat(base, ((index << 2) & 0xFFFFFFFCu) + 0x82DE2A2C, float(ctx.f1.f64));
}

// 28 words of the object (+16..+124) set to the constant 0x820ED650.
void Sub_824A3BC8(PPCContext& ctx, uint8_t* base) {
  const uint32_t k = Load32(base, 0x820ED650);
  for (uint32_t offset = 16; offset <= 124; offset += 4) Store32(base, ctx.r3.u32 + offset, k);
}

// Reports eight fields of r3 into the statistics block at r4+16, each rounded to a single first: ints at +12, +16,
// a word at +20, bytes at +24, +25, words at +28, +32, and the float at +64.
void Sub_82586538(PPCContext& ctx, uint8_t* base) {
  const uint32_t src = ctx.r3.u32, stats = ctx.r4.u32 + 16;
  Accumulate(base, stats, Rounded(int32_t(Load32(base, src + 12))));
  Accumulate(base, stats, Rounded(int32_t(Load32(base, src + 16))));
  Accumulate(base, stats, Rounded(Load32(base, src + 20)));
  Accumulate(base, stats, Rounded(Load8(base, src + 24)));
  Accumulate(base, stats, Rounded(Load8(base, src + 25)));
  Accumulate(base, stats, Rounded(Load32(base, src + 28)));
  Accumulate(base, stats, Rounded(Load32(base, src + 32)));
  Accumulate(base, stats, double(LoadFloat(base, src + 64)));
}

// Reports seven fields of r3 into the statistics block at r4+16 (rounded to a single first): words at +12, +16, +20,
// +28, ints at +24, +32, +36.
void Sub_82587D30(PPCContext& ctx, uint8_t* base) {
  const uint32_t src = ctx.r3.u32, stats = ctx.r4.u32 + 16;
  for (uint32_t offset : {12u, 16u, 20u, 28u}) Accumulate(base, stats, Rounded(Load32(base, src + offset)));
  for (uint32_t offset : {24u, 32u, 36u}) Accumulate(base, stats, Rounded(int32_t(Load32(base, src + offset))));
}

// Four random floats into r3 from the global LCG at 0x82CFDF48 (the draw times the constant 0x820ED814); the word
// after the seed is cleared before the fourth draw.
void Sub_825DD398(PPCContext& ctx, uint8_t* base) {
  const uint32_t out = ctx.r3.u32;
  const float scale = LoadFloat(base, kRandomScale);
  for (uint32_t i = 0; i < 4; ++i) {
    const uint32_t seed = Load32(base, 0x82CFDF48) * 0x41C64E6Du + 12345u;
    Store32(base, 0x82CFDF48, seed);
    const uint32_t draw = (seed >> 16) & 0x7FFF;
    if (i == 3) Store32(base, 0x82CFDF4C, 0);
    StoreFloat(base, out + 4 * i, Mul(float(double(int64_t(draw))), scale));
  }
}

// Sound parameters from a descriptor r4: +24 / +28 the first two floats, +32 / +36 the gain at r4+32 scaled by the
// second and first floats and two constants, +4 the gain itself.
void Sub_8272C558(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, src = ctx.r4.u32;
  Store32(base, obj + 24, Load32(base, src));
  const float second = LoadFloat(base, src + 4);
  StoreFloat(base, obj + 28, second);
  const float gain = LoadFloat(base, src + 32);
  StoreFloat(base, obj + 32, Mul(Mul(gain, second), LoadFloat(base, 0x82001C44)));
  StoreFloat(base, obj + 36, Mul(Mul(LoadFloat(base, src + 32), LoadFloat(base, obj + 24)), LoadFloat(base, 0x82001C48)));
  Store32(base, obj + 4, Load32(base, src + 32));
}

// Constructor: vtable 0x8200E020, kind 14, f1 at +24, r4 at +16, 32 bytes copied from r6 to +32.
void Sub_82794F60(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, src = ctx.r6.u32;
  Store64(base, obj + 24, ctx.f1.u64);
  Store32(base, obj + 16, ctx.r4.u32);
  Store32(base, obj + 0, 0x8200E020);
  Store32(base, obj + 8, 0);
  Store32(base, obj + 4, 14);
  Store32(base, obj + 12, 0);
  for (uint32_t i = 0; i < 32; i += 8) Store64(base, obj + 32 + i, Load64(base, src + i));
}

// Constructor: vtable 0x8200E038, kind 15, 32 bytes copied from r4 to +16, r5 and r6 at +48 / +52.
void Sub_82795160(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, src = ctx.r4.u32;
  Store32(base, obj + 0, 0x8200E038);
  Store32(base, obj + 8, 0);
  Store32(base, obj + 4, 15);
  Store32(base, obj + 12, 0);
  for (uint32_t i = 0; i < 32; i += 8) Store64(base, obj + 16 + i, Load64(base, src + i));
  Store32(base, obj + 48, ctx.r5.u32);
  Store32(base, obj + 52, ctx.r6.u32);
}

// Constructor of a format table: vtable 0x82B8CB44 and eighteen counts; returns 0.
void Sub_82839CF0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  static const uint32_t kValues[19] = {0x82B8CB44, 256, 16, 64, 224, 16, 64, 8, 8, 32, 8, 8, 32, 32, 2, 2, 28, 2, 2};
  for (uint32_t i = 0; i < 19; ++i) Store32(base, obj + 4 * i, kValues[i]);
  ctx.r3.u64 = 0;
}

// Releases a 4-bit count in a 64-bit bit array at r3: the nibble at bit position r4 (>> 2, 16 nibbles per word
// after the first word) masked by r4's low nibble; when non-zero, subtracted from the word of (r4 & ~0xF).
void Sub_82844E88(PPCContext& ctx, uint8_t* base) {
  const uint32_t table = ctx.r3.u32, v = ctx.r4.u32;
  const uint32_t position = (v >> 2) & 0x3FFFFFFCu;
  const uint64_t high_mask = (2ull << ((position + 3) & 0x3F)) - 1;
  const uint32_t shift = position & 0x3F;
  const uint32_t word_at = (((position >> 6) & 0x03FFFFFFu) + 1) << 3;
  uint64_t field = Load64(base, (word_at & 0xFFFFFFF8u) + table) & high_mask;
  field &= ~0ull << shift;
  field >>= shift;
  const uint32_t count = uint32_t(field) & v & 0xF;
  if (count == 0) return;
  const uint32_t key = count | (v & 0xFFFFFFF0u);
  const uint32_t key_position = (key >> 2) & 0x3FFFFFFCu;
  const uint32_t key_word = ((((key_position >> 6) & 0x03FFFFFFu) + 1) << 3) & 0xFFFFFFF8u;
  const uint64_t amount = uint64_t(key & 0xF) << (key_position & 0x3F);
  Store64(base, key_word + table, Load64(base, key_word + table) - amount);
}

// Resets a stream block of the object (+352..+504, +468, +1684): flags set at +371 / +388, -1 at +500, 3 at +504.
void Sub_828CDC28(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store8(base, obj + 371, 1);
  Store32(base, obj + 388, 1);
  for (uint32_t offset : {352u, 364u, 376u, 380u, 384u, 392u, 396u, 400u, 404u, 408u, 484u, 488u, 492u, 496u, 468u, 1684u}) {
    Store32(base, obj + offset, 0);
  }
  for (uint32_t offset : {356u, 369u, 368u, 370u, 372u}) Store8(base, obj + offset, 0);
  Store32(base, obj + 500, 0xFFFFFFFFu);
  Store32(base, obj + 504, 3);
}

// Constructor: five vtable pointers of the image (two of them overwritten by the base ones), +0 last.
void Sub_82957EE8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 8, 0x8204779C);
  Store32(base, obj + 12, 0x82047794);
  Store32(base, obj + 16, 0x8204778C);
  Store32(base, obj + 20, 0x82047788);
  Store32(base, obj + 16, 0x82047770);
  Store32(base, obj + 12, 0x82047770);
  Store32(base, obj + 0, 0x820E5958);
}

// Constructor (null-tolerant): halfword +6 = 1, vtable 0x8204EE50, twelve words from the constant 0x820ED650.
void Sub_829DCDC8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, k = Load32(base, 0x820ED650);
  if (obj == 0) return;
  Store16(base, obj + 6, 1);
  Store32(base, obj + 0, 0x8204EE50);
  for (uint32_t offset = 32; offset <= 76; offset += 4) Store32(base, obj + offset, k);
}

// Constructor with an embedded object at +20 (the sibling of batch 10's Sub_829EC830 without the null check,
// clearing +8, +12 and +16 too).
void Sub_829EC030(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 0, 0x8204FEE8);
  Store16(base, obj + 6, 1);
  Store16(base, obj + 26, 1);
  Store32(base, obj + 28, 0x82098C28);
  Store32(base, obj + 32, 0);
  Store32(base, obj + 20, 0x8204FEE0);
  Store32(base, obj + 28, 0x8204FED4);
  Store32(base, obj + 12, 0);
  Store8(base, obj + 8, 0);
  Store32(base, obj + 16, 0);
}

// Installs the vtables of a derived class: +0 and +4, the secondary vtable at +16 through the RTTI offset of the
// vtable found there, and the one at +208.
void Sub_82A4F8A8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 0, 0x820559AC);
  Store32(base, obj + 4, 0x82055990);
  Store32(base, obj + 16 + Load32(base, Load32(base, obj + 16) + 4), 0x82055968);
  Store32(base, obj + 208, 0x82055980);
  Store32(base, obj + 4, 0x82055808);
}

// Positions a glyph: +20 = row (r4[1]) times the pitch (*(+16)[0]) plus the base (*(+16)[1]), +28 = row mod 8
// (toward zero), +24 = r4[0] plus the 16-bit r5; returns 0.
void Sub_82A85E18(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, p = ctx.r4.u32;
  const int32_t row = int32_t(Load32(base, p + 4));
  const uint32_t layout = Load32(base, obj + 16);
  Store32(base, obj + 20, uint32_t(int64_t(row) * int64_t(int32_t(Load32(base, layout))) + Load32(base, layout + 4)));
  Store32(base, obj + 28, uint32_t(row - (row / 8) * 8));
  Store32(base, obj + 24, (ctx.r5.u32 & 0xFFFF) + Load32(base, p));
  ctx.r3.u64 = 0;
}

// Six global doubles at 0x82D19E40 from two constants of the image (a, b, b, a, b, b).
void Sub_82B47F28(PPCContext&, uint8_t* base) {
  const uint64_t a = Load64(base, 0x820ED510), b = Load64(base, 0x820ED4C0);
  Store64(base, 0x82D19E40, a);
  Store64(base, 0x82D19E48, b);
  Store64(base, 0x82D19E50, b);
  Store64(base, 0x82D19E58, a);
  Store64(base, 0x82D19E60, b);
  Store64(base, 0x82D19E68, b);
}

// Nine aligned buffer sizes derived from r4 (headers of 515 / 1027 bytes rounded to 4), as the original computes
// them bit by bit.
void Sub_82B6F290(PPCContext& ctx, uint8_t* base) {
  const uint32_t out = ctx.r3.u32, v = ctx.r4.u32;
  const uint32_t sixteenth = (v >> 4) & 0x0FFFFFFFu, eighth = (v >> 3) & 0x1FFFFFFFu;
  Store32(base, out + 4, (sixteenth + 515) & 0xFFFFFFFCu);
  const uint32_t r11 = (eighth << 3) & 0xFFFFFFF8u;
  const uint32_t doubled = (((eighth << 1) & 0x1FFFFFFEu) + 1027) & 0xFFFFFFFCu;
  Store32(base, out + 24, doubled);
  Store32(base, out + 28, doubled);
  const uint32_t single = (((r11 >> 3) & 0x1FFFFFFFu) + 515) & 0xFFFFFFFCu;
  Store32(base, out + 0, single);
  Store32(base, out + 16, single);
  Store32(base, out + 20, single);
  const uint64_t r6 = uint64_t(r11) + ((r11 << 1) & 0xFFFFFFFEu);
  Store32(base, out + 8, (((r11 << 3) & 0x1FFFFFF8u) + 515) & 0xFFFFFFFCu);
  Store32(base, out + 12, ((r11 & 0x1FFFFFFFu) + 515) & 0xFFFFFFFCu);
  Store32(base, out + 32, (((uint32_t(r6) << 1) & 0x1FFFFFFEu) + 515) & 0xFFFFFFFCu);
}

}  // namespace crackdown::hooks::decomp
