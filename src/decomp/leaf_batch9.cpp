// Native translations, batch 9: 25 loop-free leaf functions of 18-76 instructions. Same rules as the earlier batches.
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

// The statistics block of the sample objects: a double sum at +56 and a 16-bit counter at +74.
void Accumulate(uint8_t* base, uint32_t obj, double value) {
  Store16(base, obj + 74, uint16_t(Load16(base, obj + 74) + 1));
  StoreDouble(base, obj + 56, value + LoadDouble(base, obj + 56));
}

// slw / srw as the recompiled code performs them: a shift amount with bit 5 set gives 0.
uint32_t ShiftLeft(uint32_t value, uint64_t amount) { return (uint8_t(amount) & 0x20) ? 0 : value << (uint8_t(amount) & 0x3F); }
uint32_t ShiftRight(uint32_t value, uint64_t amount) { return (uint8_t(amount) & 0x20) ? 0 : value >> (uint8_t(amount) & 0x3F); }

constexpr uint32_t kModeGlobal = 0x8309153C;

}  // namespace

// f1 = a random float: the object's LCG at +572 (its +576 cleared), the draw times f1 times the constant at
// 0x820ED814.
void Sub_82140CE0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 576, 0);
  const uint32_t seed = Load32(base, obj + 572) * 0x41C64E6Du + 12345u;
  Store32(base, obj + 572, seed);
  const float draw = float(double(int64_t((seed >> 16) & 0x7FFF)));
  const float scaled = float(double(draw) * ctx.f1.f64);
  ctx.f1.f64 = double(Mul(scaled, LoadFloat(base, 0x820ED814)));
}

// Accumulates the 32-bit sum of five words (+92..+108) into the double at +56; the counter advances by one.
void Sub_8215A4A0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store16(base, obj + 74, uint16_t(Load16(base, obj + 74) + 1));
  const uint32_t sum = Load32(base, obj + 108) + Load32(base, obj + 104) + Load32(base, obj + 100) +
                       Load32(base, obj + 96) + Load32(base, obj + 92);
  StoreDouble(base, obj + 56, double(sum) + LoadDouble(base, obj + 56));
}

// Accumulates two bytes (+105, +104) and a float (+108) into the double at +56; the counter advances by three.
void Sub_821AA050(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store16(base, obj + 74, uint16_t(Load16(base, obj + 74) + 3));
  double sum = double(Load8(base, obj + 105)) + double(Load8(base, obj + 104));
  sum += double(LoadFloat(base, obj + 108));
  sum += LoadDouble(base, obj + 56);
  StoreDouble(base, obj + 56, sum);
}

// Constructor of the 116-byte weather settings: vtable 0x8208D6E4, registered in the global 0x82DE258C, defaults
// from a dozen image constants, two of them scaled by the global 0x82BAE768.
void Sub_822C5EC8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  for (uint32_t offset = 4; offset <= 20; offset += 4) Store32(base, obj + offset, 0);
  Store32(base, obj + 0, 0x8208D6E4);
  Store8(base, obj + 24, 0);
  const float scale = LoadFloat(base, 0x82BAE768);
  const float a = Mul(scale, LoadFloat(base, 0x820708BC)), b = Mul(scale, LoadFloat(base, 0x820EDDB0));
  Store32(base, 0x82DE258C, obj);
  const uint32_t g = Load32(base, 0x82BAE748);
  for (uint32_t offset : {28u, 32u, 36u, 40u}) Store32(base, obj + offset, g);
  Store32(base, obj + 44, 2000);
  Store32(base, obj + 48, 0);
  Store32(base, obj + 60, Load32(base, 0x82072D7C));
  Store32(base, obj + 52, 2000);
  Store32(base, obj + 64, Load32(base, 0x820704AC));
  Store32(base, obj + 56, 0);
  Store32(base, obj + 68, Load32(base, 0x82072D7C));
  Store32(base, obj + 76, 0);
  Store32(base, obj + 72, Load32(base, 0x820704AC));
  Store32(base, obj + 80, Load32(base, 0x820EE164));
  Store32(base, obj + 84, Load32(base, 0x820EE160));
  Store32(base, obj + 88, Load32(base, 0x82071F00));
  Store32(base, obj + 100, Load32(base, 0x820EE15C));
  Store32(base, obj + 92, Load32(base, 0x820ED5F0));
  Store32(base, obj + 96, Load32(base, 0x820EE158));
  Store32(base, obj + 104, Load32(base, 0x820EE154));
  StoreFloat(base, obj + 108, a);
  StoreFloat(base, obj + 112, b);
}

// Reports five fields of the object into the owner's statistics block (+16 -> +260, +64): an int at +20, two
// shorts at +24 / +26 and bits 30 and 29 of the word at +32, each counted as one sample.
void Sub_822DEFD0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  const auto stats = [&] { return Load32(base, Load32(base, obj + 16) + 260) + 64; };
  Accumulate(base, stats(), double(int64_t(int32_t(Load32(base, obj + 20)))));
  Accumulate(base, stats(), double(int64_t(int16_t(Load16(base, obj + 24)))));
  Accumulate(base, stats(), double(int64_t(int16_t(Load16(base, obj + 26)))));
  Accumulate(base, stats(), double((Load32(base, obj + 32) >> 30) & 1));
  Accumulate(base, stats(), double((Load32(base, obj + 32) >> 29) & 1));
}

// Resets a 5232-byte state object: nineteen floats to the constant 0x820722DC, many flag bytes, halfwords and words
// cleared, three flags set, a count of 1000.
void Sub_822F0F08(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, k = Load32(base, 0x820722DC);
  for (uint32_t offset : {208u, 304u, 5008u, 5088u, 5168u, 400u, 576u, 816u, 864u, 912u, 1040u, 1488u, 4232u, 4216u, 4220u,
                          4212u, 4224u, 4228u, 4240u}) {
    Store32(base, obj + offset, k);
  }
  for (uint32_t offset : {5206u, 5204u, 5218u, 5216u, 26u}) Store16(base, obj + offset, 0);
  for (uint32_t offset : {5200u, 5212u, 1008u, 4452u, 28u}) Store32(base, obj + offset, 0);
  for (uint32_t offset : {5208u, 5209u, 5210u, 5211u, 5220u, 5221u, 5222u, 5223u, 5228u, 64u, 36u, 465u, 657u, 658u, 736u,
                          737u, 1493u, 1494u, 1495u, 1496u, 1497u, 1498u, 1584u, 2400u, 4236u, 2401u, 4245u, 4460u, 4461u,
                          4462u, 4752u, 4848u, 4849u, 4850u, 464u, 4448u, 4449u}) {
    Store8(base, obj + offset, 0);
  }
  Store8(base, obj + 1492, 1);
  Store8(base, obj + 4851, 1);
  Store32(base, obj + 4456, 1000);
  Store8(base, obj + 4450, 1);
}

// Fills r4 from the object and its owner (+16): the pending byte at +36 (then cleared), the owner's bytes +293 /
// +292, its short at +294 as a word, its halfword at +300, and the object's byte at +81.
void Sub_82307B70(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, out = ctx.r4.u32, owner = Load32(base, obj + 16);
  Store8(base, out + 8, Load8(base, obj + 36));
  Store8(base, obj + 36, 0);
  Store8(base, out + 10, Load8(base, owner + 293));
  Store8(base, out + 11, Load8(base, owner + 292));
  Store32(base, out + 12, uint32_t(int32_t(int16_t(Load16(base, owner + 294)))));
  Store16(base, out + 20, Load16(base, owner + 300));
  Store8(base, out + 57, Load8(base, obj + 81));
}

// Flag bytes of r4: sets bit 1 of +0, clears bit 7 of +10 and bit 2 of +9; when neither of the object's bytes at
// +48 / +49 is set, also bit 6 of +8.
void Sub_82309F98(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, p = ctx.r4.u32;
  Store8(base, p, Load8(base, p) | 2);
  Store8(base, p + 10, Load8(base, p + 10) & 0x7F);
  Store8(base, p + 9, Load8(base, p + 9) & 0xFB);
  if (Load8(base, obj + 48) != 0 || Load8(base, obj + 49) != 0) return;
  Store8(base, p + 8, Load8(base, p + 8) & 0xBF);
}

// Session (*0x82DE3EA8) with a live owner (*0x82DE3EA0 byte +8): when its flag at +143 is set and its count at
// +138 is above one, decrements the count and clears the flag.
void Sub_8232AD98(PPCContext&, uint8_t* base) {
  const uint32_t session = Load32(base, 0x82DE3EA8);
  if (session == 0) return;
  if (Load8(base, Load32(base, 0x82DE3EA0) + 8) == 0) return;
  if (Load8(base, session + 143) == 0) return;
  const uint32_t count = Load8(base, session + 138);
  if (count <= 1) return;
  Store8(base, session + 138, uint8_t(count - 1));
  Store8(base, session + 143, 0);
}

// Accumulates a byte (+98), a short (+96) and two bytes (+89, +88) into the double at +56; the counter advances by
// four.
void Sub_8240BC48(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store16(base, obj + 74, uint16_t(Load16(base, obj + 74) + 4));
  double sum = double(Load8(base, obj + 98)) + double(int64_t(int16_t(Load16(base, obj + 96))));
  sum += double(Load8(base, obj + 89));
  sum += double(Load8(base, obj + 88));
  sum += LoadDouble(base, obj + 56);
  StoreDouble(base, obj + 56, sum);
}

// Starts six modes on r4 (the mode blocks at +88, +104, +120, +136, +152; counts of 200), the counter byte at +37
// advanced by six; *r3 = the mode global.
void Sub_82475DD0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r4.u32;
  for (uint32_t offset : {88u, 112u, 128u, 160u, 152u, 144u}) Store32(base, obj + offset, 1);
  Store8(base, obj + 100, 1);
  Store32(base, obj + 96, 0);
  Store16(base, obj + 92, 200);
  Store8(base, obj + 101, 0);
  Store16(base, obj + 108, 0);
  Store32(base, obj + 104, 0);
  Store16(base, obj + 124, 0);
  Store8(base, obj + 37, Load8(base, obj + 37) + 6);
  Store32(base, obj + 120, 2);
  Store16(base, obj + 156, 200);
  Store16(base, obj + 140, 0);
  Store32(base, obj + 136, 2);
  Store32(base, ctx.r3.u32, Load32(base, kModeGlobal));
}

// Resets the target block of r4 (+556..+636): zeros, constants 0x8207092C (two), 0x820ED56C and 0x82071FE0 (three);
// *r3 = the global 0x83098208.
void Sub_824DE458(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r4.u32;
  Store32(base, obj + 556, 0);
  Store8(base, obj + 592, 0);
  Store32(base, obj + 600, 0);
  Store16(base, obj + 616, 0);
  Store8(base, obj + 619, 0);
  Store32(base, obj + 620, 0);
  const uint32_t pair = Load32(base, 0x8207092C), k = Load32(base, 0x82071FE0);
  Store32(base, obj + 576, pair);
  Store8(base, obj + 624, 0);
  Store32(base, obj + 580, pair);
  Store32(base, obj + 628, 0);
  Store32(base, obj + 560, Load32(base, 0x820ED56C));
  Store32(base, obj + 632, 0);
  Store32(base, obj + 564, k);
  Store32(base, obj + 636, 0);
  Store32(base, obj + 568, k);
  Store32(base, obj + 572, k);
  Store32(base, ctx.r3.u32, Load32(base, 0x83098208));
}

// Profiler block (*0x82D07F9C): the four nibbles of the global 0x82DE3FE8 as floats at +8384.., mask bit 24 set.
void Sub_82555B38(PPCContext&, uint8_t* base) {
  const uint32_t value = Load32(base, 0x82DE3FE8), block = Load32(base, 0x82D07F9C);
  StoreFloat(base, block + 8384, float(value & 0xF));
  StoreFloat(base, block + 8396, float((value >> 12) & 0xF));
  StoreFloat(base, block + 8388, float((value >> 4) & 0xF));
  StoreFloat(base, block + 8392, float((value >> 8) & 0xF));
  Store64(base, block + 8, Load64(base, block + 8) | 0x1000000);
}

// f1 = the sum of six counters as floats: (+20 + +24) + +26 and (+4 + +8) + +10 (words and halfwords, unsigned).
void Sub_82581E18(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  float a = Add(float(double(Load32(base, obj + 20))), float(double(Load16(base, obj + 24))));
  float b = Add(float(double(Load32(base, obj + 4))), float(double(Load16(base, obj + 8))));
  a = Add(a, float(double(Load16(base, obj + 26))));
  b = Add(b, float(double(Load16(base, obj + 10))));
  ctx.f1.f64 = double(Add(a, b));
}

// Tiled texture address of texel (r3, r4) in a surface r5 texels wide with r6 bytes per texel (log2 from the
// leading zeros): the Xbox 360 tiling formula, kept as the original computes it.
void Sub_825A7068(PPCContext& ctx, uint8_t*) {
  const uint32_t x = ctx.r3.u32, y = ctx.r4.u32, width = ctx.r5.u32;
  const int64_t log2 = 31 - int64_t(ctx.r6.u32 == 0 ? 32 : __builtin_clz(ctx.r6.u32));
  const uint32_t r7 = ((y << 2) & 0x18) + (x & 7);
  const uint64_t r6 = uint64_t(int64_t(int32_t((width + 31) >> 5)) * int64_t(int32_t(y >> 5))) + (x >> 5);
  const uint64_t r10a = uint64_t((y << 1) & 0xFFFFFFF0u) + x;
  const uint32_t r9 = (y << 4) & 0x100, r8 = (uint32_t(r10a) << 3) & 0xC0, r30 = y & 8, r31 = (y << 3) & 8;
  const uint32_t shifted = ShiftLeft(r7, log2);
  const uint32_t r5 = ShiftLeft(uint32_t(r6), log2 + 7);
  const uint32_t r6b = ShiftLeft(r30, log2 + 3);
  const uint32_t high = shifted & 0xFFFFFFF0u, low = shifted & 0xF;
  uint64_t r10 = ((uint32_t(uint64_t(r31) + high) << 1) & 0xFFFFFFFEu);
  r10 = uint64_t(r5) + r10 + r6b + low;
  const uint32_t a = uint32_t(r10) & 0xFFFFFE00u, b = uint32_t(r10) & 0x1C0, c = uint32_t(r10) & 0x3F;
  const uint64_t sum = uint64_t(r9) + a;
  uint64_t out = (uint32_t(sum) << 1) & 0xFFFFFFFEu;
  out += b;
  out = (uint32_t(out) << 2) & 0xFFFFFFFCu;
  out += r8;
  out += c;
  ctx.r3.u64 = ShiftRight(uint32_t(out), log2);
}

// Constructor of a 76-byte colour set: nine words of 1.0f, 15 at +72, +68 and the byte +52 cleared.
void Sub_8273CC98(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, one = Load32(base, 0x82000A94);
  for (uint32_t offset : {0u, 4u, 8u, 16u, 20u, 24u, 32u, 36u, 40u}) Store32(base, obj + offset, one);
  Store32(base, obj + 72, 15);
  Store32(base, obj + 68, 0);
  Store8(base, obj + 52, 0);
}

// Constructor: vtable 0x8200DFC0, kind 6, four arguments at +16..+28, -1 at +36, zeros.
void Sub_82794140(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 16, ctx.r4.u32);
  Store32(base, obj + 20, ctx.r5.u32);
  Store32(base, obj + 24, ctx.r6.u32);
  Store32(base, obj + 28, ctx.r7.u32);
  Store32(base, obj + 4, 6);
  Store32(base, obj + 0, 0x8200DFC0);
  for (uint32_t offset : {8u, 12u, 32u, 40u, 44u}) Store32(base, obj + offset, 0);
  Store32(base, obj + 36, 0xFFFFFFFFu);
}

// Unlinks the tagged doubly linked node r4 ({next, prev} with bit 0 marking the sentinel) and splices r3 in its
// place as a one-element list; nothing when r4 is already unlinked.
void Sub_82845E00(PPCContext& ctx, uint8_t* base) {
  const uint32_t a = ctx.r3.u32, b = ctx.r4.u32, prev_slot = b + 4;
  const uint32_t prev = Load32(base, prev_slot);
  if (prev & 1) return;
  const uint32_t prev_node = prev & 0xFFFFFFFEu, next_node = Load32(base, b) & 0xFFFFFFFEu;
  Store32(base, prev_node, Load32(base, a));
  Store32(base, Load32(base, a) & 0xFFFFFFFEu, prev_node);
  Store32(base, next_node, a | 1);
  Store32(base, a, next_node);
  Store32(base, prev_slot, b | 1);
  Store32(base, b, prev_slot | 1);
}

// Moves the tagged node r4 out of its list into the list whose head is at r3 (after the head); nothing when r4 is
// already unlinked.
void Sub_8287CCF0(PPCContext& ctx, uint8_t* base) {
  const uint32_t head = ctx.r3.u32, b = ctx.r4.u32, prev_slot = b + 4;
  const uint32_t prev = Load32(base, prev_slot);
  if (prev & 1) return;
  const uint32_t after_head = (head & 0xFFFFFFFEu) + 4;
  const uint32_t next_node = Load32(base, b) & 0xFFFFFFFEu, prev_node = prev & 0xFFFFFFFEu;
  const uint32_t old = Load32(base, after_head);
  Store32(base, prev_node, after_head);
  Store32(base, after_head, prev_node);
  Store32(base, next_node, old);
  Store32(base, old & 0xFFFFFFFEu, next_node);
  Store32(base, prev_slot, b | 1);
  Store32(base, b, prev_slot | 1);
}

// FNV-1 of the four bytes of r3 (low byte first) folded into the hash r4; 64-bit arithmetic as the original's.
void Sub_8292D4B0(PPCContext& ctx, uint8_t*) {
  const uint32_t w = ctx.r3.u32;
  const int64_t prime = 0x01000193;
  int64_t h = int64_t(ctx.r4.s32) * prime;
  h ^= w & 0xFF;
  h = int64_t(int32_t(h)) * prime;
  h ^= (w >> 8) & 0xFF;
  h = int64_t(int32_t(h)) * prime;
  h ^= (w >> 16) & 0xFF;
  h = int64_t(int32_t(h)) * prime;
  ctx.r3.u64 = uint64_t(h) ^ ((w >> 24) & 0xFF);
}

// Constructor: halfword +6 = 1, vtable 0x8204A94C, four image constants and 0.0f twice.
void Sub_8298A828(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 8, Load32(base, 0x820EDB14));
  Store16(base, obj + 6, 1);
  Store32(base, obj + 0, 0x8204A94C);
  Store32(base, obj + 12, Load32(base, 0x82001C8C));
  Store32(base, obj + 16, Load32(base, 0x82000A90));
  Store32(base, obj + 24, Load32(base, 0x82000A90));
  Store32(base, obj + 20, Load32(base, 0x82000A8C));
}

// *r6 = distance between the positions at r4+48 and r5+48; *r7 = 1; returns 1.
void Sub_82A45648(PPCContext& ctx, uint8_t* base) {
  const uint32_t a = ctx.r4.u32, b = ctx.r5.u32;
  const float dx = float(double(LoadFloat(base, b + 48)) - double(LoadFloat(base, a + 48)));
  const float dy = float(double(LoadFloat(base, b + 52)) - double(LoadFloat(base, a + 52)));
  const float dz = float(double(LoadFloat(base, b + 56)) - double(LoadFloat(base, a + 56)));
  float sum = Mul(dx, dx);
  sum = float(std::fma(double(dy), double(dy), double(sum)));
  sum = float(std::fma(double(dz), double(dz), double(sum)));
  StoreFloat(base, ctx.r6.u32, float(std::sqrt(double(sum))));
  Store8(base, ctx.r7.u32, 1);
  ctx.r3.u64 = 1;
}

// Constructor of a callback table: four function pointers of the image, the argument at +16, +28 cleared; returns 0.
void Sub_82A85FD0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 16, ctx.r4.u32);
  Store32(base, obj + 0, 0x82A85E18);
  Store32(base, obj + 8, 0x82299000);
  Store32(base, obj + 12, 0x82299000);
  Store32(base, obj + 4, 0x82A85E68);
  Store32(base, obj + 28, 0);
  ctx.r3.u64 = 0;
}

// Default colour sets at 0x82CFEA88 / 0x82CFEAE8 from the constants at 0x82BAE054 / 0x82BAE058.
void Sub_82B0FF08(PPCContext&, uint8_t* base) {
  const uint32_t a = Load32(base, 0x82BAE054), b = Load32(base, 0x82BAE058);
  for (uint32_t offset = 4; offset <= 92; offset += 8) Store32(base, 0x82CFEA88 + offset, a);
  Store32(base, 0x82CFEA88 + 4, b);
  Store32(base, 0x82CFEA88 + 28, b);
}

void Sub_82B0FF58(PPCContext&, uint8_t* base) {
  const uint32_t a = Load32(base, 0x82BAE054);
  for (uint32_t offset = 4; offset <= 92; offset += 8) Store32(base, 0x82CFEAE8 + offset, a);
}

}  // namespace crackdown::hooks::decomp
