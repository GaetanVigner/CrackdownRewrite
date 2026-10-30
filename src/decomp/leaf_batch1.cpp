// Native translations, batch 1: 13 leaf functions of the game code (see decomp_hooks.h). Each function is written
// from Ghidra's decompilation of the image (Any360 pipeline) checked against the recompiled code's exact semantics:
// 32-bit guest addresses, big-endian memory, float operations rounded like the PowerPC's single-precision forms
// (float(double op double) is the same rounding as the recompiled code), and the 64-bit register values the original
// leaves in r3. tests/decomp_io_tests runs each against the recompiled original on random inputs.
#include "decomp_hooks.h"

#include <bit>
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

uint64_t Load64(uint8_t* base, uint32_t addr) {
  uint64_t value;
  std::memcpy(&value, rex::memory::GuestPtr<const uint8_t*>(base, addr), sizeof(value));
  return std::byteswap(value);
}

void Store64(uint8_t* base, uint32_t addr, uint64_t value) {
  value = std::byteswap(value);
  std::memcpy(rex::memory::GuestPtr<uint8_t*>(base, addr), &value, sizeof(value));
}

// Single-precision results the way the recompiled code computes them: in double, rounded to float once.
float Mul(float a, float b) { return float(double(a) * double(b)); }
float Add(float a, float b) { return float(double(a) + double(b)); }
float Sub(float a, float b) { return float(double(a) - double(b)); }
float Fma(float a, float b, float c) { return float(std::fma(double(a), double(b), double(c))); }

// Image constants the functions read (the title's rdata): 0.0f and 1.0f at 0x82000A90 / 0x82000A94.
constexpr uint32_t kFloatA = 0x82000A90;
constexpr uint32_t kFloatB = 0x82000A94;

}  // namespace

// Zeroes the four output words; then walks the chunk list at `chunks` ({size, tag, payload...} with size the offset
// of the next chunk, 0 ending the list) and copies the payload of the first chunk tagged 0x40000000, swapping the
// halfwords and the bytes within them of each of its four words.
void Sub_8213BC38(PPCContext& ctx, uint8_t* base) {
  const uint32_t out = ctx.r3.u32;
  uint32_t chunk = ctx.r4.u32;
  for (uint32_t i = 0; i < 4; ++i) Store32(base, out + 4 * i, 0);
  if (Load32(base, chunk) == 0) return;
  for (;;) {
    if (Load32(base, chunk + 4) == 0x40000000) {
      for (uint32_t i = 0; i < 4; ++i) Store32(base, out + 4 * i, Load32(base, chunk + 8 + 4 * i));
      for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t w = Load32(base, out + 4 * i);
        const uint32_t hi_lo = (w >> 16) | (w & 0xFFFF0000u);   // rlwimi r8,r11,16,16,31
        const uint32_t lo_hi = (w << 16) | (w & 0x0000FFFFu);   // rlwimi r7,r11,16,0,15
        Store32(base, out + 4 * i, (std::rotl(hi_lo, 24) & 0xFFFFu) | (std::rotl(lo_hi, 8) & 0xFFFF0000u));
      }
      return;
    }
    chunk += Load32(base, chunk);
    if (Load32(base, chunk) == 0) return;
  }
}

// Whether the entity at r4 is visible to the local player: its owner is the local player (global at 0x82DEE658), or
// the flag bit (field +80: byte index >> 3, bit index & 7) of the player's mask at +45 differs from the expected word.
void Sub_82144D80(PPCContext& ctx, uint8_t* base) {
  const uint32_t entity = ctx.r4.u32;
  const uint32_t player = Load32(base, 0x82DEE658);
  uint32_t result = 1;
  if (Load32(base, entity) != Load32(base, player)) {
    const uint32_t flag = Load32(base, entity + 80);
    const uint32_t bit = flag & 7;
    const uint32_t byte_index = uint32_t(int64_t(int32_t(flag) >> 3) + player);
    const uint32_t mask = Load8(base, byte_index + 45);
    const uint32_t one = Load32(base, 0x82BAAC20);
    const uint32_t expected = Load32(base, 0x82BAAC1C);
    result = ((one << bit) & mask) != expected ? 1 : 0;
  }
  ctx.r3.u64 = result;
}

// For every object pointer from obj+80 up to the end pointer at obj+64, clears the object's words at +44 and +48.
void Sub_8216F470(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  const uint32_t end = Load32(base, obj + 64);
  for (uint32_t p = obj + 80; p != end; p += 4) {
    Store32(base, Load32(base, p) + 44, 0);
    Store32(base, Load32(base, p) + 48, 0);
  }
}

// Element address: obj+4 is an index into the owner's table (owner = **(obj+8), count at +40, pairs {slot, owner} at
// +52); a slot of -1 or an index past the count falls back to the pair at address 0. The result is
// owner2+24 + 4 * (5 * owner2.indices[slot]) (the recompiled code's 32-bit wraps kept where they matter).
void Sub_821CAA40(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  const uint32_t table = Load32(base, Load32(base, obj + 8));
  const uint32_t index = Load16(base, obj + 4);
  uint32_t pair = 0;
  if (index < Load32(base, table + 40)) {
    const uint32_t candidate = Load32(base, table + 52) + index * 8;
    if (Load32(base, candidate) != 0xFFFFFFFFu) pair = candidate;
  }
  const uint32_t owner = Load32(base, pair + 4);
  const uint32_t slot = Load32(base, pair);
  const uint32_t entry = Load32(base, (slot << 2) + Load32(base, owner + 56));
  const uint64_t scaled = uint64_t(entry) + (entry << 2);  // add r11,r11,r9: 64-bit sum of two 32-bit values
  ctx.r3.u64 = uint64_t((uint32_t(scaled) << 2) & 0xFFFFFFFCu) + Load32(base, owner + 24);
}

// Whether r4 equals the object's word at +56 or at +80 (32-bit comparison); 0 or 1 in r3.
void Sub_821E72D0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  const uint32_t value = ctx.r4.u32;
  ctx.r3.u64 = (value == Load32(base, obj + 56) || value == Load32(base, obj + 80)) ? 1 : 0;
}

// Whether the string at r4 equals the fixed name at 0x82080E6C: 1 when the first difference is at the terminator of
// the argument (so a longer constant counts as different), else 0.
void Sub_821E8E90(PPCContext& ctx, uint8_t* base) {
  uint32_t s = ctx.r4.u32;
  uint32_t name = 0x82080E6C;
  uint32_t difference;
  for (;;) {
    const uint32_t c = Load8(base, s);
    difference = c - Load8(base, name);
    if (c == 0) break;
    ++s;
    ++name;
    if (difference != 0) break;
  }
  ctx.r3.u64 = difference == 0 ? 1 : 0;
}

// Character state test: the character's state block (pointer at +292, minus 32, or 0 when null) has a non-zero word
// at +6304, and one of: flag 0x20 of the byte at +6340, or bits 0x10 / 0x80 of the byte at +5456.
void Sub_822B7408(PPCContext& ctx, uint8_t* base) {
  const uint32_t character = ctx.r3.u32;
  const uint32_t link = Load32(base, character + 292);
  const uint32_t state = link ? link - 32 : 0;
  uint32_t result = 0;
  if (Load32(base, state + 6304) != 0) {
    const uint32_t a = Load8(base, state + 6340) & 0x20;
    const uint32_t b = Load8(base, state + 5456);
    const uint32_t bits = ((((a | (b >> 2)) >> 1) & 0x7FFFFFFFu) | (b & 0x10)) & 0xFFFFFFF0u;
    result = bits != 0 ? 1 : 0;
  }
  ctx.r3.u64 = result;
}

// Orb map view (r3) parameters from the scale in f1: marks the view dirty (+540), sets the pan pair (+544, +548) to
// (rate * scale + offset) * factor and the zoom pair (+504, +508) to factor * k1 * (rate * scale) + factor * k2, with
// the constants read from the image.
void Sub_823E9BD0(PPCContext& ctx, uint8_t* base) {
  const uint32_t view = ctx.r3.u32;
  const float rate = LoadFloat(base, view + 860);
  const float factor = LoadFloat(base, view + 864);
  const float scaled = float(double(rate) * ctx.f1.f64);  // fmuls with the double argument as given
  Store8(base, view + 540, 1);
  const float pan = Mul(Add(scaled, LoadFloat(base, 0x8207047C)), factor);
  StoreFloat(base, view + 544, pan);
  StoreFloat(base, view + 548, pan);
  const float zoom = Fma(Mul(factor, LoadFloat(base, 0x820ED468)), scaled, Mul(factor, LoadFloat(base, 0x82071FE0)));
  StoreFloat(base, view + 504, zoom);
  StoreFloat(base, view + 508, zoom);
}

// Constructor of a 68-byte object at r3: vtable 0x8200DFFC, kind 11, fifteen zero words.
void Sub_82794868(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store32(base, obj + 4, 11);
  Store32(base, obj + 0, 0x8200DFFC);
  for (uint32_t offset = 8; offset <= 64; offset += 4) Store32(base, obj + offset, 0);
}

// Profiler slot r3: writes the elapsed time (pair at *0x82D08E44) into the slots block (*0x82D07F9C) at
// 16 * (slot + 112), the start time, 1.0 and 0.0 at 16 * slot + 1796.., and sets bit (slot >> 2) from the top of the
// block's leading 64-bit mask (a shift by 64 or more sets nothing).
void Sub_8294AA18(PPCContext& ctx, uint8_t* base) {
  const uint32_t slot = ctx.r3.u32;
  const uint32_t times = Load32(base, 0x82D08E44);
  const uint32_t block = Load32(base, 0x82D07F9C);
  const float start = LoadFloat(base, times);
  const float elapsed = Sub(LoadFloat(base, times + 4), start);
  StoreFloat(base, ((slot + 112) << 4) + block, elapsed);
  const uint32_t entry = (slot << 4) + block;
  StoreFloat(base, entry + 1796, start);
  StoreFloat(base, entry + 1800, LoadFloat(base, kFloatB));
  StoreFloat(base, entry + 1804, LoadFloat(base, kFloatA));
  const uint32_t shift = (slot >> 2) & 0x3FFFFFFFu;
  const uint64_t bit = (shift & 0x40) ? 0 : (0x8000000000000000ull >> (shift & 0x3F));
  Store64(base, block, Load64(base, block) | bit);
}

// Largest value of (entry[24] + 39) * 4 + entry[20] + entry[16] + entry[12] over the array of entry pointers at
// obj+56 (count at obj+60), as a signed comparison; 0 for an empty array. The sums are 64-bit, as the original's.
void Sub_82981388(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  uint64_t best = 0;
  int64_t i = int64_t(Load32(base, obj + 60)) - 1;
  if (int32_t(i) >= 0) {
    uint64_t p = uint64_t(Load32(base, obj + 56)) + ((uint32_t(i) << 2) & 0xFFFFFFFCu);
    do {
      const uint32_t entry = Load32(base, uint32_t(p));
      const uint64_t size = uint64_t(((uint32_t(Load32(base, entry + 24) + 39)) << 2) & 0xFFFFFFFCu) +
                            Load32(base, entry + 20) + Load32(base, entry + 16) + Load32(base, entry + 12);
      if (int32_t(best) <= int32_t(size)) best = size;
      --i;
      p -= 4;
    } while (int32_t(i) >= 0);
  }
  ctx.r3.u64 = best;
}

// Over the float array at r3 of r4 elements, summed from the end: the number of elements (plus one) left when the
// running sum from the end reaches fraction (image constant at 0x82001978) of the total, at least 76.
void Sub_82A94078(PPCContext& ctx, uint8_t* base) {
  const uint32_t array = ctx.r3.u32;
  const int64_t last = ctx.r4.s64 - 1;
  float total = LoadFloat(base, kFloatB);
  float sum = total;
  if (int32_t(last) >= 0) {
    uint32_t p = ((uint32_t(last) << 2) & 0xFFFFFFFCu) + array;
    for (int64_t i = last; int32_t(i) >= 0; --i, p -= 4) total = Add(LoadFloat(base, p), total);
  }
  const float threshold = Mul(total, LoadFloat(base, 0x82001978));
  int64_t i = last;
  if (int32_t(last) >= 0) {
    uint32_t p = ((uint32_t(last) << 2) & 0xFFFFFFFCu) + array;
    for (;;) {
      sum = Add(LoadFloat(base, p), sum);
      if (!(sum < threshold)) break;  // fcmpu: a NaN also leaves the loop
      --i;
      p -= 4;
      if (int32_t(i) < 0) break;
    }
  }
  const int64_t result = i + 2;
  ctx.r3.s64 = int32_t(result) >= 76 ? result : 76;
}

// Three 64-bit words at 0x82D19DC4 from the image's two float constants: (A, B), (B, A), (B, B).
void Sub_82B47D50(PPCContext&, uint8_t* base) {
  const uint64_t a = Load32(base, kFloatA);
  const uint64_t b = Load32(base, kFloatB);
  Store64(base, 0x82D19DD4, (b << 32) | b);
  Store64(base, 0x82D19DCC, (b << 32) | a);
  Store64(base, 0x82D19DC4, (a << 32) | b);
}

}  // namespace crackdown::hooks::decomp
