// Native translations, batch 2: 11 larger leaf functions of the game code (40-130 instructions; see decomp_hooks.h).
// Same rules as batch 1: 32-bit guest addresses, big-endian memory, single-precision results rounded once from the
// double computation the recompiled code performs, 64-bit r3 where the original returns one.
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

void Store16(uint8_t* base, uint32_t addr, uint16_t value) {
  value = std::byteswap(value);
  std::memcpy(rex::memory::GuestPtr<uint8_t*>(base, addr), &value, sizeof(value));
}

float Fma(float a, float b, float c) { return float(std::fma(double(a), double(b), double(c))); }

}  // namespace

// Blend of two curves (a at r4+136, b at r5+136, 36-byte stride, the count in the signed byte at obj+528) into the
// object's curve at obj+632 with weight f1: out = a * (k - t) + b * t for the floats at +0, +4, +8, +12 and +20 of each
// entry, k the constant at 0x82C626C4. Computed in the original's order (fmuls, then fmadds).
void Sub_824ABED0(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32, a = ctx.r4.u32, b = ctx.r5.u32;
  const double t = ctx.f1.f64;
  const float weight_a = float(double(LoadFloat(base, 0x82C626C4)) - t);
  if (int8_t(Load8(base, obj + 528)) <= 0) return;
  uint32_t offset = 0;
  for (int32_t i = 1;; ++i, offset += 36) {
    for (uint32_t field : {8u, 4u, 0u, 12u, 20u}) {
      const uint32_t pb = Load32(base, b + 136) + offset, pa = Load32(base, a + 136) + offset,
                     out = Load32(base, obj + 632) + offset;
      const float scaled_b = float(double(LoadFloat(base, pb + field)) * t);
      StoreFloat(base, out + field, Fma(LoadFloat(base, pa + field), weight_a, scaled_b));
    }
    if (i >= int32_t(int8_t(Load8(base, obj + 528)))) break;
  }
}

// Picks between two values a (f1) and b (f2) with weights c (f3), d (f4), returning in f1: when b lies within
// [a * k_low, a * k_high] (constants of the image), (a + b) * half; otherwise the value with the larger weight, unless
// it is below a floor and its weighted product times half is below zero, in which case the larger of a and b.
void Sub_82A95780(PPCContext& ctx, uint8_t* base) {
  const double a = ctx.f1.f64, b = ctx.f2.f64, c = ctx.f3.f64, d = ctx.f4.f64;
  const double half = LoadFloat(base, 0x82000BAC);
  double result;
  if (b > double(float(a * double(LoadFloat(base, 0x8205B11C)))) ||
      b < double(float(a * double(LoadFloat(base, 0x82001C8C))))) {
    double weight = d;
    if (c > d) {
      result = a;
      weight = c;
    } else {
      result = b;
    }
    if (result < double(LoadFloat(base, 0x82000B80))) {
      const float product = float(double(float(weight * result)) * half);
      if (product < double(LoadFloat(base, 0x82000A90))) result = b > a ? b : a;
    }
  } else {
    result = double(float(double(float(a + b)) * half));
  }
  ctx.f1.f64 = result;
}

// Copies 80-byte elements from [r4, r5) to r6 (72 bytes each: words 0-17, with words 3 and 7 replaced by the
// constant at 0x820722CC), skipping the copy while the destination pointer is null; r3 ends at the advanced
// destination.
void Sub_825FF080(PPCContext& ctx, uint8_t* base) {
  uint32_t src = ctx.r4.u32, dst = ctx.r6.u32;
  const uint32_t end = ctx.r5.u32;
  ctx.r3.u64 = dst;
  if (src == end) return;
  const uint32_t constant = Load32(base, 0x820722CC);
  do {
    if (dst != 0) {
      for (uint32_t word = 0; word < 18; ++word) {
        Store32(base, dst + 4 * word, (word == 3 || word == 7) ? constant : Load32(base, src + 4 * word));
      }
    }
    src += 80;
    dst += 80;
  } while (src != end);
  ctx.r3.u64 = dst;
}

// Constructor of a 264-byte state object: ten floats set to the constant at 0x820722DC, flags set to 1, the rest
// cleared (bytes, halfwords and words as the original writes them).
void Sub_82463160(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  const uint32_t constant = Load32(base, 0x820722DC);
  for (uint32_t offset : {40u, 64u, 88u, 112u, 136u, 160u, 184u, 208u, 232u, 256u}) Store32(base, obj + offset, constant);
  for (uint32_t offset : {76u, 80u, 100u, 104u, 124u, 28u, 128u, 148u, 152u, 172u, 32u, 176u, 196u, 200u, 220u, 52u, 56u,
                          224u, 244u, 248u}) {
    Store32(base, obj + offset, 1);
  }
  for (uint32_t offset : {27u, 26u, 51u, 50u, 75u, 74u, 99u, 98u, 123u, 122u, 147u, 146u, 171u, 170u, 195u, 194u, 219u,
                          218u, 243u, 242u}) {
    Store8(base, obj + offset, 0);
  }
  for (uint32_t offset : {36u, 60u, 84u, 108u, 132u, 156u, 180u, 204u, 228u, 252u}) Store16(base, obj + offset, 0);
  for (uint32_t offset : {44u, 68u, 92u, 116u, 140u, 164u, 188u, 212u, 236u, 260u, 8u}) Store32(base, obj + offset, 0);
}

// Name lookup: FNV-1 hash (multiply then xor) of the string at r4, bucket in the global table (*0x82DE267C: mask at
// +0x2C064, 20-byte buckets at +0x27068), chain through +4 comparing the hash at +8 and the name at +16; the value at
// +20 is returned when the caller's key (*r5) is the global at 0x82C3E470 or the entry's owner key (*(*value+56)).
void Sub_823925C0(PPCContext& ctx, uint8_t* base) {
  const uint32_t name = ctx.r4.u32, key = ctx.r5.u32;
  uint32_t hash = 0x811C9DC5u;
  for (uint32_t p = name; Load8(base, p) != 0; ++p) hash = (hash * 0x01000193u) ^ Load8(base, p);
  const uint32_t table = Load32(base, 0x82DE267C) + 0x19040;
  const uint32_t index = Load32(base, table + 0x13024) & hash;
  const uint32_t bucket = table + ((uint32_t(uint64_t(index) + ((index << 2) & 0xFFFFFFFCu)) << 2) & 0xFFFFFFFCu);
  uint32_t entry = Load32(base, bucket + 0xE028);
  ctx.r3.u64 = 0;
  while (entry != 0) {
    if (Load32(base, entry + 8) == hash) {
      uint32_t p = Load32(base, entry + 16), q = name, difference;
      for (;;) {
        const uint32_t c = Load8(base, p);
        difference = c - Load8(base, q);
        if (c == 0) break;
        ++p;
        ++q;
        if (difference != 0) break;
      }
      if (difference == 0) {
        const uint32_t slot = entry + 20;
        if (slot == 0) return;
        const uint32_t caller = Load32(base, key);
        if (caller != Load32(base, 0x82C3E470) && caller != Load32(base, Load32(base, Load32(base, slot) + 56))) return;
        ctx.r3.u64 = Load32(base, slot);
        return;
      }
    }
    entry = Load32(base, entry + 4);
  }
}

// Ring buffer push: queue at r3 {+4 begin, +8 end, +16 read index, +20 write index, +24 count}; advances the write
// index (wrapping at the capacity (end - begin) / 4, resetting both indices when the write index was negative), pushes
// the read index along when caught up, stores *r4 at the write slot, bumps the count up to the capacity, and returns
// the write index.
void Sub_8246DAE0(PPCContext& ctx, uint8_t* base) {
  const uint32_t q = ctx.r3.u32;
  const auto capacity = [&] {
    const uint32_t begin = Load32(base, q + 4);
    return begin ? int32_t(Load32(base, q + 8) - begin) >> 2 : 0;
  };
  const int32_t write = int32_t(Load32(base, q + 20));
  if (write < 0) {
    Store32(base, q + 20, 0);
    Store32(base, q + 16, 0);
  } else {
    Store32(base, q + 20, uint32_t(write + 1));
  }
  const uint32_t begin = Load32(base, q + 4);
  if (int32_t(Load32(base, q + 20)) >= capacity()) Store32(base, q + 20, 0);
  const uint32_t slot = Load32(base, q + 20);
  if (slot == Load32(base, q + 16)) Store32(base, q + 16, Load32(base, q + 16) + 1);
  if (int32_t(Load32(base, q + 16)) >= capacity()) Store32(base, q + 16, 0);
  Store32(base, ((slot << 2) & 0xFFFFFFFCu) + begin, Load32(base, ctx.r4.u32));
  const uint32_t begin2 = Load32(base, q + 4);
  const uint32_t count = Load32(base, q + 24) + 1;
  Store32(base, q + 24, count);
  const int32_t limit = begin2 ? int32_t(Load32(base, q + 8) - begin2) >> 2 : 0;
  if (int32_t(count) > limit) Store32(base, q + 24, begin2 ? uint32_t(limit) : 0);
  ctx.r3.u64 = Load32(base, q + 20);
}

// Clears a slot of the current page's table (page index at 0x82DE2708, 832-byte pages after *r7, slot range at
// +816/+817 of the page, entries at +16 + 4 * (slot + 56)), sets flag 0x40000 at +1720 of *r7, and when the index
// r4 fits the manager's list at r3+3120, records (*r7, r7[1]) in that entry at (5 * r5 + r6 + 2) * 8 and marks it;
// then clears the byte at r3+2030. Nothing happens unless the slot (byte at r7+4) lies in the page's range.
void Sub_824DA608(PPCContext& ctx, uint8_t* base) {
  const uint32_t manager = ctx.r3.u32, list = ctx.r7.u32;
  const uint32_t page_index = Load32(base, 0x82DE2708);
  const uint32_t owner = Load32(base, list);
  bool in_range = false;
  if (owner != 0) {
    const uint32_t page = uint32_t(uint64_t(page_index) * 832 + owner);
    const uint32_t slot = Load8(base, list + 4), first = Load8(base, page + 816);
    in_range = slot >= first && int32_t(slot - first) < int32_t(uint32_t(Load8(base, page + 817)));
  }
  if (!in_range) return;
  const uint32_t page = uint32_t(uint64_t(page_index) * 832 + owner);
  const uint32_t relative = Load8(base, list + 4) - Load8(base, page + 816);
  const uint32_t entries = page + 16;
  uint32_t entry = 0;
  if (relative < Load8(base, entries + 801)) entry = Load32(base, (((relative + 56) << 2) & 0xFFFFFFFCu) + entries);
  Store8(base, entry + 93, Load8(base, entry + 93) & 0xF7);
  Store32(base, owner + 1720, Load32(base, owner + 1720) | 0x40000);
  const uint32_t head = manager + 3120;
  const int32_t index = int16_t(ctx.r4.u16);
  if (int32_t(Load32(base, head) - head - 16u) >> 2 > index) {
    const uint32_t item = Load32(base, ((uint32_t(index + 784) << 2) & 0xFFFFFFFCu) + manager);
    if (item != 0) {
      Store32(base, item + 8, 1);
      const int64_t row = int16_t(ctx.r5.u16), column = int16_t(ctx.r6.u16);
      const uint32_t at = ((uint32_t(row + ((uint32_t(row) << 2) & 0xFFFFFFFCu) + column + 2) << 3) & 0xFFFFFFF8u) + item;
      Store32(base, at, Load32(base, list));
      Store32(base, at + 4, Load32(base, list + 4));
    }
  }
  Store8(base, manager + 2030, 0);
}

// Relocates a loaded table: the two table offsets at +8 and +12 and the first word of each 8-byte entry of both
// tables become absolute (header address added). The original then tallies the entries' byte fields into a local
// array it never reads back (dead code with no effect outside the stack): not reproduced.
void Sub_82340008(PPCContext& ctx, uint8_t* base) {
  const uint32_t header = ctx.r3.u32;
  Store32(base, header + 8, Load32(base, header + 8) + header);
  Store32(base, header + 12, Load32(base, header + 12) + header);
  for (uint32_t table : {8u, 12u}) {
    const uint32_t count_at = header + (table - 8);
    if (Load32(base, count_at) == 0) continue;
    uint32_t i = 0, offset = 0;
    do {
      const uint32_t entry = Load32(base, header + table) + offset;
      Store32(base, entry, Load32(base, entry) + header);
      ++i;
      offset += 8;
    } while (i < Load32(base, count_at));
  }
}

// ORs the object's masks into the global render state masks: the five words at obj+12..+28 into 0x82D089E0,
// 0x82D0893C, 0x82D08910, 0x82D08914, 0x82D08918, then the twenty words at obj+32.. into 0x82D08730...
void Sub_823B7838(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  const auto merge = [&](uint32_t global, uint32_t field) { Store32(base, global, Load32(base, obj + field) | Load32(base, global)); };
  merge(0x82D08910, 20);
  merge(0x82D089E0, 12);
  merge(0x82D08914, 24);
  merge(0x82D0893C, 16);
  merge(0x82D08918, 28);
  for (uint32_t i = 0; i < 20; ++i) merge(0x82D08730 + 4 * i, 32 + 4 * i);
}

// Appends a glyph code to the string being built at r3 (write cursor in the global 0x82D100C0): the current symbol
// (*0x82D08D14) plus 23 when the global 0x82D08B98 is set, followed by 36, 37, 38 when the symbol is 2 or more;
// plus 11 otherwise. Nothing when no symbol is set.
void Sub_82944E58(PPCContext& ctx, uint8_t* base) {
  const uint32_t text = ctx.r3.u32;
  const uint32_t symbol = Load32(base, 0x82D08D14);
  if (symbol == 0) return;
  const uint32_t value = Load32(base, symbol);
  const auto append = [&](uint32_t code) {
    const uint32_t cursor = Load32(base, 0x82D100C0);
    Store8(base, cursor + text, uint8_t(code));
    Store32(base, 0x82D100C0, cursor + 1);
  };
  if (Load32(base, 0x82D08B98) == 0) {
    append(value + 11);
    return;
  }
  append(value + 23);
  if (value <= 1) return;
  append(36);
  append(37);
  append(38);
}

// Constructor of a 192-byte object: kind byte r5, word r4 at +4, state 1 at +18, 2 at +20, the rest cleared.
void Sub_8272F260(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  Store8(base, obj + 0, ctx.r5.u8);
  Store32(base, obj + 4, ctx.r4.u32);
  Store8(base, obj + 8, 0);
  Store32(base, obj + 12, 0);
  Store16(base, obj + 16, 0);
  Store8(base, obj + 18, 1);
  Store32(base, obj + 20, 2);
  for (uint32_t offset : {24u, 26u, 28u, 30u}) Store16(base, obj + offset, 0);
  for (uint32_t offset : {36u, 40u, 44u, 52u, 56u, 60u, 68u, 72u, 76u, 84u, 88u, 92u, 100u, 104u, 108u, 116u, 120u, 124u,
                          132u, 136u, 140u, 148u, 152u, 156u, 164u, 168u, 172u, 180u, 184u, 188u}) {
    Store32(base, obj + offset, 0);
  }
}

}  // namespace crackdown::hooks::decomp
