// Native translations, batch 13: 25 loop-free leaf functions of 22-53 instructions. Same rules as the earlier batches.
#include "decomp_hooks.h"

#include <climits>
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

float Sub(float a, float b) { return float(double(a) - double(b)); }
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

// fctidz: toward zero, saturating, NaN to the minimum (the recompiler's rule).
int64_t ToInt64(double value) {
  if (std::isnan(value)) return INT64_MIN;
  if (value > double(LLONG_MAX)) return LLONG_MAX;
  if (value <= -9223372036854775808.0) return INT64_MIN;
  return int64_t(value);
}

// (-7, -6, -6) in ten consecutive triples at the given global.
void SentinelTriples(uint8_t* base, uint32_t at) {
  for (uint32_t i = 0; i < 10; ++i) {
    Store32(base, at + 12 * i, 0xFFFFFFF9u);
    Store32(base, at + 12 * i + 4, 0xFFFFFFFAu);
    Store32(base, at + 12 * i + 8, 0xFFFFFFFAu);
  }
}

// Constructors of the 8298xxxx family: halfword +6 = 1, three sentinel triples at +56 / +80 / +92, a fourth at
// `last`, the vtable.
void SentinelConstructor(uint8_t* base, uint32_t obj, uint32_t vtable, uint32_t last) {
  Store16(base, obj + 6, 1);
  Store32(base, obj + 40, 0);
  for (uint32_t offset : {56u, 80u, 92u, last}) {
    Store32(base, obj + offset, 0);
    Store32(base, obj + offset + 4, 0);
    Store32(base, obj + offset + 8, 0x80000000u);
  }
  Store32(base, obj + 0, vtable);
}

// Six 64-bit arguments r5..r8 spilled and re-read as words: the receiver's hi/lo words at `high` (r5 hi, r5 lo, r6
// hi) and `low` (r7 hi, r7 lo, r8 hi), f1 / f2 stored as singles, the flag "r4's low byte is non-zero".
void StoreSpilledArguments(PPCContext& ctx, uint8_t* base, uint32_t obj, uint32_t floats, uint32_t flag,
                           uint32_t high, uint32_t low) {
  StoreFloat(base, obj + floats, float(ctx.f1.f64));
  StoreFloat(base, obj + floats + 4, float(ctx.f2.f64));
  Store32(base, obj + flag, (ctx.r4.u32 & 0xFF) != 0 ? 1 : 0);
  Store32(base, obj + high + 0, uint32_t(ctx.r5.u64 >> 32));
  Store32(base, obj + high + 4, uint32_t(ctx.r5.u64));
  Store32(base, obj + high + 8, uint32_t(ctx.r6.u64 >> 32));
  Store32(base, obj + low + 0, uint32_t(ctx.r7.u64 >> 32));
  Store32(base, obj + low + 4, uint32_t(ctx.r7.u64));
  Store32(base, obj + low + 8, uint32_t(ctx.r8.u64 >> 32));
}

constexpr uint32_t kOne = 0x82000A94, kRandomScale = 0x820ED814;

}  // namespace

// Stores the spilled arguments into the sub-object *(*(r3+2520)+284): floats at +276, flag at +264, words at +364
// and +352.
void Sub_82157808(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = Load32(base, Load32(base, ctx.r3.u32 + 2520) + 284);
  StoreSpilledArguments(ctx, base, obj, 276, 264, 364, 352);
}

// The same into *(*(r3+2520)+384): floats at +460, flag at +456, words at +440 and +428.
void Sub_82157A60(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = Load32(base, Load32(base, ctx.r3.u32 + 2520) + 384);
  StoreSpilledArguments(ctx, base, obj, 460, 456, 440, 428);
}

// Copies a 20-byte descriptor from r4 to r3, then links the node at r3+20 (vtable 0x8207B734) into the ring whose
// head is *(r4+32) when there is one.
void Sub_82172648(PPCContext& ctx, uint8_t* base) {
  const uint32_t dst = ctx.r3.u32, src = ctx.r4.u32, node = dst + 20;
  Store32(base, dst + 0, Load32(base, src + 0));
  Store32(base, dst + 4, Load32(base, src + 4));
  for (uint32_t offset : {8u, 9u, 10u}) Store8(base, dst + offset, Load8(base, src + offset));
  Store32(base, dst + 12, Load32(base, src + 12));
  Store32(base, dst + 16, Load32(base, src + 16));
  const uint32_t head = Load32(base, src + 32);
  Store32(base, node + 0, 0x8207B734);
  Store32(base, node + 12, head);
  if (head == 0) return;
  Store32(base, node + 8, head + 4);
  Store32(base, node + 4, Load32(base, head + 8));
  Store32(base, head + 8, node);
  Store32(base, Load32(base, node + 4) + 8, node);
}

// In state 1: moves to state 4 and draws a random value in [+276, +280] into +284 (the global LCG times the constant
// at 0x820ED814).
void Sub_8219D658(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  if (Load32(base, obj + 264) != 1) return;
  const float low = LoadFloat(base, obj + 276);
  const float range = Sub(LoadFloat(base, obj + 280), low);
  const float scale = LoadFloat(base, kRandomScale);
  Store32(base, obj + 264, 4);
  const float scaled = Mul(GlobalDraw(base), range);
  StoreFloat(base, obj + 284, float(std::fma(double(scaled), double(scale), double(low))));
}

// Constructor of a message handler: -1 at +24, two function pointers, a name pointer, the vtable, zeros, the top
// three bits of +32 cleared then the top one set, nine floats from the constant 0x820722DC.
void Sub_82302828(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, k = Load32(base, 0x820722DC);
  Store16(base, obj + 24, 0xFFFF);
  Store32(base, obj + 8, 0x82299000);
  Store32(base, obj + 12, 0x822DFD78);
  Store32(base, obj + 4, 0x82E52A21);
  Store32(base, obj + 0, 0x8208C2A8);
  Store32(base, obj + 16, 0);
  Store32(base, obj + 20, 0);
  Store16(base, obj + 26, 0);
  Store32(base, obj + 28, 0);
  Store32(base, obj + 32, Load32(base, obj + 32) & 0x1FFFFFFFu);
  for (uint32_t offset : {64u, 68u, 72u, 80u, 84u, 88u, 96u, 100u, 104u}) Store32(base, obj + offset, k);
  Store32(base, obj + 32, Load32(base, obj + 32) | 0x80000000u);
}

// Reports the float at +12 and three bytes of r3 (+10, the top bit of +11, +8) into the statistics block at r4+64.
void Sub_82315B78(PPCContext& ctx, uint8_t* base) {
  const uint32_t src = ctx.r3.u32, stats = ctx.r4.u32 + 64;
  Accumulate(base, stats, double(LoadFloat(base, src + 12)));
  Accumulate(base, stats, double(Load8(base, src + 10)));
  Accumulate(base, stats, double(Load8(base, src + 11) >> 7));
  Accumulate(base, stats, double(Load8(base, src + 8)));
}

// Records a frame step r4: the global 0x82D99128 block (step, total, the object's +28), the dirty byte 0x82DE3FB0,
// the scaled step into *r5 and +56, the step at +52, two accumulators at +32 / +36, and the smoothed value at +60.
void Sub_82326348(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, step = ctx.r4.u32, out = ctx.r5.u32;
  const float k_scale = LoadFloat(base, 0x820722D0), k_decay = LoadFloat(base, 0x82071980);
  const float k_norm = LoadFloat(base, 0x8206F394);
  Store8(base, 0x82DE3FB0, 1);
  Store32(base, 0x82D99128, step);
  Store32(base, 0x82D9912C, Load32(base, 0x82D9912C) + step);
  const float step_f = float(double(int64_t(step)));
  const float scaled = Mul(step_f, k_scale);
  StoreFloat(base, out, scaled);
  const float smoothed = float(std::fma(double(LoadFloat(base, obj + 60)), double(k_decay), double(step_f)));
  Store32(base, 0x82D99130, Load32(base, obj + 28));
  StoreFloat(base, obj + 56, scaled);
  Store32(base, obj + 52, step);
  Store32(base, obj + 36, Load32(base, obj + 36) + step);
  Store32(base, obj + 32, Load32(base, obj + 32) + step);
  StoreFloat(base, obj + 60, Mul(smoothed, k_norm));
}

// Registers r4 in the global table *0x82DE2678 for slot r5: pointer at [(r5+674)*4]; when the byte at
// (r4+1356)*2 + r5 is clear, sets it, bumps the count at [(r5+698)*4] and stores the frame time / 1000 (from
// *(*(*0x82DE2674+628)+32)) at [(r5+676)*4].
void Sub_82391118(PPCContext& ctx, uint8_t* base) {
  const uint32_t table = Load32(base, 0x82DE2678), r4 = ctx.r4.u32, r5 = ctx.r5.u32;
  const uint32_t mark = (r4 + 1356) * 2 + r5;
  Store32(base, table + (r5 + 674) * 4, r4);
  if (Load8(base, table + mark) != 0) return;
  Store8(base, table + mark, 1);
  const uint32_t count = table + (r5 + 698) * 4;
  Store32(base, count, Load32(base, count) + 1);
  const int32_t time = int32_t(Load32(base, Load32(base, Load32(base, 0x82DE2674) + 628) + 32));
  Store32(base, table + (r5 + 676) * 4, uint32_t(time / 1000));
}

// Twelve words of a 3x4 (padded rows of four) gathered into a packed 3x3 + 3.
void Sub_82462D98(PPCContext& ctx, uint8_t* base) {
  const uint32_t src = ctx.r3.u32, dst = ctx.r4.u32;
  const uint32_t from[] = {0, 4, 8, 16, 20, 24, 32, 36, 40, 48, 52, 56};
  for (uint32_t i = 0; i < 12; ++i) Store32(base, dst + 4 * i, Load32(base, src + from[i]));
}

// The inverse scatter: twelve packed words into padded rows of four.
void Sub_82462E60(PPCContext& ctx, uint8_t* base) {
  const uint32_t src = ctx.r3.u32, dst = ctx.r4.u32;
  const uint32_t to[] = {0, 4, 8, 16, 20, 24, 32, 36, 40, 48, 52, 56};
  for (uint32_t i = 0; i < 12; ++i) Store32(base, dst + to[i], Load32(base, src + 4 * i));
}

// Resets the nine counters of a profile block r4 (count 1, limits 200 / 500 / 1000, zeros), adds nine to the byte at
// +37, and returns the global 0x8309153C through *r3.
void Sub_82471460(PPCContext& ctx, uint8_t* base) {
  const uint32_t out = ctx.r3.u32, obj = ctx.r4.u32;
  for (uint32_t offset : {96u, 112u, 120u, 136u, 152u, 168u, 184u, 208u, 216u}) Store32(base, obj + offset, 0);
  for (uint32_t offset : {101u, 149u, 165u, 181u, 197u}) Store8(base, obj + offset, 0);
  for (uint32_t offset : {124u, 140u, 156u, 172u, 188u, 220u}) Store16(base, obj + offset, 0);
  Store16(base, obj + 92, 200);
  Store8(base, obj + 37, uint8_t(Load8(base, obj + 37) + 9));
  for (uint32_t offset : {88u, 104u, 128u, 144u, 160u, 176u, 192u, 200u, 224u}) Store32(base, obj + offset, 1);
  Store8(base, obj + 100, 1);
  Store16(base, obj + 108, 500);
  Store16(base, obj + 204, 1000);
  Store32(base, out, Load32(base, 0x8309153C));
}

// Copies a camera block of *(r3+4) (+132..+220) into r3-40..r3+48.
void Sub_8249E760(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, src = Load32(base, obj + 4);
  const int32_t pairs[][2] = {{144, -28}, {132, -40}, {136, -36}, {140, -32}, {148, -24}, {152, -20}, {156, -16},
                              {160, -12}, {164, -8},  {180, 8},   {188, 16},  {192, 20},  {196, 24},  {200, 28},
                              {204, 32},  {208, 36},  {212, 40},  {216, 44}};
  for (const auto& p : pairs) Store32(base, obj + uint32_t(p[1]), Load32(base, src + uint32_t(p[0])));
  Store8(base, obj + 12, Load8(base, src + 184));
  Store8(base, obj + 48, Load8(base, src + 220));
}

// Six settings of *(r3+12) + 12368: four floats truncated to integers, two words; returns 0.
void Sub_8270D108(PPCContext& ctx, uint8_t* base) {
  const uint32_t src = Load32(base, ctx.r3.u32 + 12) + 12368, out = ctx.r4.u32;
  for (uint32_t i = 0; i < 4; ++i) {
    Store32(base, out + 4 * i, uint32_t(ToInt64(double(LoadFloat(base, src + 4 * i)))));
  }
  Store32(base, out + 16, Load32(base, src + 16));
  Store32(base, out + 20, Load32(base, src + 20));
  ctx.r3.u64 = 0;
}

// Constructor of a 132-byte message dispatcher: five function pointers, a format string pointer, 123, zeros.
void Sub_8278EA38(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 12, 0x8278E910);
  Store32(base, obj + 16, 0x8278EA20);
  Store32(base, obj + 104, 0);
  Store32(base, obj + 108, 0);
  Store32(base, obj + 0, 0x8278E800);
  Store32(base, obj + 4, 0x8278E880);
  Store32(base, obj + 8, 0x8278E850);
  Store32(base, obj + 20, 0);
  Store32(base, obj + 112, 0x8200BF00);
  Store32(base, obj + 116, 123);
  for (uint32_t offset : {120u, 124u, 128u}) Store32(base, obj + offset, 0);
}

// Constructor: vtable 0x8200DFF0, kind 10, fifteen zero words.
void Sub_827945C0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 4, 10);
  Store32(base, obj + 0, 0x8200DFF0);
  for (uint32_t offset = 8; offset <= 64; offset += 4) Store32(base, obj + offset, 0);
}

// Constructor: vtable 0x8200E020, kind 14, r4 at +16, r5 at +24, r6 at +28, 32 bytes copied from r7 to +32.
void Sub_82795010(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, src = ctx.r7.u32;
  Store32(base, obj + 16, ctx.r4.u32);
  Store32(base, obj + 24, ctx.r5.u32);
  Store32(base, obj + 28, ctx.r6.u32);
  Store32(base, obj + 0, 0x8200E020);
  Store32(base, obj + 8, 0);
  Store32(base, obj + 4, 14);
  Store32(base, obj + 12, 0);
  for (uint32_t i = 0; i < 32; i += 8) Store64(base, obj + 32 + i, Load64(base, src + i));
}

// Constructor (null-tolerant): vtable 0x8204A708, the fourth triple at +144.
void Sub_82988798(PPCContext& ctx, uint8_t* base) {
  if (ctx.r3.u32 == 0) return;
  SentinelConstructor(base, ctx.r3.u32, 0x8204A708, 144);
}

// Constructor (null-tolerant): vtable 0x8204ABF4, the fourth triple at +288.
void Sub_8298F838(PPCContext& ctx, uint8_t* base) {
  if (ctx.r3.u32 == 0) return;
  SentinelConstructor(base, ctx.r3.u32, 0x8204ABF4, 288);
}

// Transposes a 3x4 of r4 into r3 with the constant 1.0f in the fourth column.
void Sub_829A3680(PPCContext& ctx, uint8_t* base) {
  const uint32_t dst = ctx.r3.u32, src = ctx.r4.u32, one = Load32(base, kOne);
  for (uint32_t offset : {0u, 20u, 40u}) Store32(base, dst + offset, Load32(base, src + offset));
  for (uint32_t offset : {12u, 28u, 44u}) Store32(base, dst + offset, one);
  const uint32_t pairs[][2] = {{4, 16}, {16, 4}, {8, 32}, {32, 8}, {24, 36}, {36, 24}};
  for (const auto& p : pairs) Store32(base, dst + p[0], Load32(base, src + p[1]));
}

// The distance between two 20-byte vertices of the mesh *(r4+4) (index *r4 through the tables at +60 and +56,
// vertices at +24) into *r5; returns 1.
void Sub_82A27D10(PPCContext& ctx, uint8_t* base) {
  const uint32_t ref = ctx.r4.u32, mesh = Load32(base, ref + 4), index = Load32(base, ref) << 2;
  const uint32_t vertices = Load32(base, mesh + 24);
  const uint32_t a = Load32(base, Load32(base, mesh + 60) + index) * 20 + vertices;
  const uint32_t b = Load32(base, Load32(base, mesh + 56) + index) * 20 + vertices;
  const float dx = Sub(LoadFloat(base, a + 4), LoadFloat(base, b + 4));
  const float dy = Sub(LoadFloat(base, a + 8), LoadFloat(base, b + 8));
  const float dz = Sub(LoadFloat(base, a + 12), LoadFloat(base, b + 12));
  float sum = Mul(dx, dx);
  sum = float(std::fma(double(dy), double(dy), double(sum)));
  sum = float(std::fma(double(dz), double(dz), double(sum)));
  StoreFloat(base, ctx.r5.u32, float(std::sqrt(double(sum))));
  ctx.r3.u64 = 1;
}

// Writes the fields of a 96-byte slot r4 of the table *(r3+8) from the descriptor r5: byte +10 into bits 12-19 of
// word 0, bytes +8 / +9 into bits 12-13 / 17-19 of word 1, the low 26 bits of words +0 / +4 into words 3 / 4; returns 0.
void Sub_82A52168(PPCContext& ctx, uint8_t* base) {
  const uint32_t desc = ctx.r5.u32, slot = ctx.r4.u32 * 96 + Load32(base, ctx.r3.u32 + 8);
  Store32(base, slot, (Load32(base, slot) & 0xFFF00FFFu) | (uint32_t(Load8(base, desc + 10)) << 12));
  const uint32_t w1 = (Load32(base, slot + 4) & ~0x3000u) | ((uint32_t(Load8(base, desc + 8)) << 12) & 0x3000u);
  Store32(base, slot + 4, w1);
  Store32(base, slot + 4, (w1 & ~0xE0000u) | ((uint32_t(Load8(base, desc + 9)) << 17) & 0xE0000u));
  Store32(base, slot + 12, (Load32(base, slot + 12) & 0xFC000000u) | (Load32(base, desc) & 0x03FFFFFFu));
  Store32(base, slot + 16, (Load32(base, slot + 16) & 0xFC000000u) | (Load32(base, desc + 4) & 0x03FFFFFFu));
  ctx.r3.u64 = 0;
}

// Constructor of a colour pair: vtable 0x8206382C, four bytes from r4, four from r5, the flag "the second alpha
// bytes differ".
void Sub_82AB2FF0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store16(base, obj + 6, 1);
  Store32(base, obj + 0, 0x8206382C);
  for (uint32_t i = 0; i < 4; ++i) {
    Store8(base, obj + 8 + i, Load8(base, ctx.r4.u32 + i));
    Store8(base, obj + 12 + i, Load8(base, ctx.r5.u32 + i));
  }
  Store8(base, obj + 16, Load8(base, obj + 13) != Load8(base, obj + 9) ? 1 : 0);
}

// Ten global triples at 0x82E4EA10 / 0x82E4EA88 / 0x82E4EB00 set to (-7, -6, -6).
void Sub_82B12560(PPCContext&, uint8_t* base) { SentinelTriples(base, 0x82E4EA10); }
void Sub_82B12638(PPCContext&, uint8_t* base) { SentinelTriples(base, 0x82E4EA88); }
void Sub_82B12710(PPCContext&, uint8_t* base) { SentinelTriples(base, 0x82E4EB00); }

}  // namespace crackdown::hooks::decomp
