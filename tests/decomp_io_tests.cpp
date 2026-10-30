// Runs each native translation of src/decomp against the recompiled original in the guest sandbox on the same random
// inputs (registers, two object blocks, the image globals the function reads), and compares what they produce: the
// result register r3 (when the function returns one) and every changed word of guest memory outside the stack's red
// zone. The originals are extracted from generated/default at configure time (tests/cmake/decomp_originals.cmake);
// tests/decomp/functions.txt lists them. A difference names the function, the trial and the first differing item.
// Run: decomp_io_tests [--trials N]

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "check.h"
#include "decomp/decomp_hooks.h"
#include "guest_sandbox.h"

extern const guest_sandbox::FunctionEntry kDecompOriginals[];
extern const unsigned kDecompOriginalCount;

namespace {

using Native = void (*)(PPCContext&, uint8_t*);

constexpr uint32_t kBlockA = 0x40000000, kBlockB = 0x40010000, kBlockSize = 0x4000;
constexpr uint32_t kStack = 0x70000000, kStackSize = 0x10000;

std::atomic<uint32_t> g_running{0};
std::atomic<uint64_t> g_progress{0};
uint32_t g_default_csr = 0;

void Watchdog() {
  uint64_t last = g_progress.load();
  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(3));
    const uint64_t now = g_progress.load();
    if (now == last && g_running.load()) {
      std::printf("decomp_io_tests: sub_%08X has run for over 3 s; its input preparation must bound its loops\n",
                  g_running.load());
      std::fflush(stdout);
      std::_Exit(3);
    }
    last = now;
  }
}

// The guest memory the test writes before a run, so changes can be told from the initial contents.
struct Scene {
  guest_sandbox::Memory& memory = guest_sandbox::Memory::Get();
  std::unordered_map<uint32_t, uint32_t> written;  // address -> value of every word written (the last write wins)

  void Word(uint32_t address, uint32_t value) {
    memory.Write32(address, value);
    written[address] = value;
  }
  void Float(uint32_t address, float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    Word(address, bits);
  }
  void Bytes(uint32_t address, const char* text) {
    // Written as words so the pre-image is known; the trailing bytes of the last word stay zero.
    std::string s(text);
    s.resize((s.size() + 4) & ~size_t(3), '\0');
    for (size_t i = 0; i < s.size(); i += 4) {
      Word(address + uint32_t(i), (uint32_t(uint8_t(s[i])) << 24) | (uint32_t(uint8_t(s[i + 1])) << 16) |
                                      (uint32_t(uint8_t(s[i + 2])) << 8) | uint32_t(uint8_t(s[i + 3])));
    }
  }
  // A random block: words that are small values, plausible pointers into the blocks, or anything.
  void RandomBlock(uint32_t address, uint32_t size, std::mt19937& rng) {
    for (uint32_t i = 0; i < size; i += 4) {
      uint32_t value;
      switch (rng() % 6) {
        case 0: value = 0; break;
        case 1: value = rng() % 16; break;
        case 2: value = rng() % 1024; break;
        case 3: value = Pointer(rng); break;
        default: value = uint32_t(rng()); break;
      }
      Word(address + i, value);
    }
  }
  uint32_t Pointer(std::mt19937& rng) {
    const uint32_t block = rng() % 2 ? kBlockA : kBlockB;
    return block + (uint32_t(rng()) % (kBlockSize - 0x800) & ~3u);
  }
  float RandomFloat(std::mt19937& rng) { return float(int32_t(rng() % 20001) - 10000) / 128.0f; }
  uint32_t PreImage(uint32_t address) const {
    const auto it = written.find(address);
    return it == written.end() ? 0 : it->second;
  }
};

struct Case {
  uint32_t address;
  Native native;
  bool compare_r3;  // the function returns a value in r3
  bool compare_f1;  // the function returns a value in f1
  // Writes the inputs the function dereferences so its loops are bounded and its pointers land in the blocks.
  std::function<void(Scene&, PPCContext&, std::mt19937&)> prepare;
};

struct Outcome {
  uint64_t r3 = 0;
  uint64_t f1 = 0;  // bits
  std::vector<std::pair<uint32_t, uint32_t>> changes;  // (address, new value), sorted
};

Outcome Run(const Case& c, guest_sandbox::GuestFunction function, uint32_t seed) {
  Scene scene;
  uint8_t* base = scene.memory.base();
  std::mt19937 rng(seed);
  scene.RandomBlock(kBlockA, kBlockSize, rng);
  scene.RandomBlock(kBlockB, kBlockSize, rng);
  scene.memory.Fill(kStack, kStackSize, 0);
  // The image constants every batch-1 function may read: 0.0 / 1.0 at 0x82000A90 / 0x82000A94 (random here, so a
  // translation that hard-codes them is caught), the fraction at 0x82001978, and the orb map constants.
  scene.Float(0x82000A90, scene.RandomFloat(rng));
  scene.Float(0x82000A94, scene.RandomFloat(rng));
  scene.Float(0x82001978, scene.RandomFloat(rng));
  scene.Float(0x8207047C, scene.RandomFloat(rng));
  scene.Float(0x82071FE0, scene.RandomFloat(rng));
  scene.Float(0x820ED468, scene.RandomFloat(rng));
  // Batch 2 constants and globals: the pick's thresholds, the two constructor constants, the render state masks.
  for (uint32_t address : {0x82000BACu, 0x8205B11Cu, 0x82001C8Cu, 0x82000B80u, 0x82C626C4u}) {
    scene.Float(address, scene.RandomFloat(rng));
  }
  scene.Word(0x820722CC, uint32_t(rng()));
  scene.Word(0x820722DC, uint32_t(rng()));
  for (uint32_t i = 0; i < 40; ++i) scene.Word(0x82D08730 + 4 * i, uint32_t(rng()));
  for (uint32_t address : {0x82D089E0u, 0x82D08910u, 0x82D08914u, 0x82D08918u, 0x82D0893Cu}) scene.Word(address, uint32_t(rng()));
  // Batch 3 globals: thresholds, flag bytes, colour constants.
  for (uint32_t address : {0x82071E28u, 0x82070E6Cu}) scene.Float(address, scene.RandomFloat(rng));
  scene.Word(0x82BAA608, rng() % 2 ? 0 : uint32_t(rng()));
  for (uint32_t address : {0x82DE247Cu, 0x82C3E400u, 0x82C3E404u, 0x82BAE734u, 0x82BAE744u}) scene.Word(address, uint32_t(rng()));

  rex::ppc::FPSCRRegister::Platform::setcsr(g_default_csr);
  PPCContext ctx{};
  ctx.fpscr.InitHost();
  ctx.r1.u64 = kStack + kStackSize - 0x1000;
  ctx.r3.u64 = kBlockA;
  ctx.r4.u64 = kBlockB;
  for (PPCRegister* reg : {&ctx.r5, &ctx.r6, &ctx.r7, &ctx.r8, &ctx.r9, &ctx.r10}) {
    reg->u64 = rng() % 2 ? rng() % 64 : uint32_t(rng());
  }
  for (PPCRegister* reg : {&ctx.f1, &ctx.f2, &ctx.f3, &ctx.f4}) reg->f64 = double(scene.RandomFloat(rng));
  ctx.lr = 0x82000000;
  if (c.prepare) c.prepare(scene, ctx, rng);

  g_running = c.address;
  function(ctx, base);
  g_running = 0;
  ++g_progress;

  Outcome outcome;
  outcome.r3 = ctx.r3.u64;
  outcome.f1 = ctx.f1.u64;
  for (uint32_t page : scene.memory.touched()) {
    if (page >= kStack && page < kStack + kStackSize) continue;  // red zone scratch is not an output
    for (uint32_t offset = 0; offset < guest_sandbox::kPageSize; offset += 4) {
      const uint32_t address = page + offset;
      const uint32_t value = scene.memory.Read32(address);
      if (value != scene.PreImage(address)) outcome.changes.emplace_back(address, value);
    }
  }
  std::sort(outcome.changes.begin(), outcome.changes.end());
  scene.memory.Reset();
  return outcome;
}

// --- input preparation per function ---------------------------------------------------------------------------------

void ChunkList(Scene& s, PPCContext& ctx, std::mt19937& rng) {
  // r4: 1-4 chunks {size, tag, 4 payload words}, sizes 24-64, one of them tagged 0x40000000 half of the time.
  const uint32_t list = kBlockB + 0x100;
  uint32_t at = list;
  const uint32_t count = 1 + rng() % 4;
  const uint32_t tagged = rng() % 2 ? rng() % count : 99;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t size = 24 + 4 * (rng() % 11);
    s.Word(at, size);
    s.Word(at + 4, i == tagged ? 0x40000000 : uint32_t(rng()));
    for (uint32_t k = 0; k < 4; ++k) s.Word(at + 8 + 4 * k, uint32_t(rng()));
    at += size;
  }
  s.Word(at, 0);
  ctx.r4.u64 = list;
}

void PlayerEntity(Scene& s, PPCContext& ctx, std::mt19937& rng) {
  // Global player pointer into block A; the entity in block B with a small flag index; the two expected words.
  s.Word(0x82DEE658, kBlockA + 0x200);
  s.Word(kBlockB + 0x400, rng() % 2 ? s.PreImage(kBlockA + 0x200) : uint32_t(rng()));
  s.Word(kBlockB + 0x400 + 80, rng() % 2 ? rng() % 64 : uint32_t(rng()) % 0x1000);
  s.Word(0x82BAAC1C, rng() % 4 ? rng() % 2 : uint32_t(rng()));
  s.Word(0x82BAAC20, rng() % 2 ? 1 : uint32_t(rng()));
  ctx.r4.u64 = kBlockB + 0x400;
}

void PointerArray(Scene& s, PPCContext& ctx, std::mt19937& rng) {
  // obj+80: 0-8 pointers into the blocks, obj+64 the end pointer.
  const uint32_t obj = kBlockA + 0x800;
  const uint32_t count = rng() % 9;
  for (uint32_t i = 0; i < count; ++i) s.Word(obj + 80 + 4 * i, s.Pointer(rng));
  s.Word(obj + 64, obj + 80 + 4 * count);
  ctx.r3.u64 = obj;
}

void IndexedTable(Scene& s, PPCContext& ctx, std::mt19937& rng) {
  const uint32_t obj = kBlockA + 0x1000, table = kBlockB + 0x1000, pairs = kBlockB + 0x1100;
  s.Word(obj + 8, kBlockA + 0x1040);
  s.Word(kBlockA + 0x1040, table);
  const uint32_t count = rng() % 6;
  s.Word(obj + 4, (rng() % 8) << 16 | (rng() % 7));  // index in the low halfword
  s.Word(table + 40, count);
  s.Word(table + 52, pairs);
  for (uint32_t i = 0; i < count; ++i) {
    s.Word(pairs + 8 * i, rng() % 3 ? rng() % 8 : 0xFFFFFFFFu);
    s.Word(pairs + 8 * i + 4, s.Pointer(rng));
  }
  ctx.r3.u64 = obj;
}

void TwoCached(Scene& s, PPCContext& ctx, std::mt19937& rng) {
  const uint32_t obj = kBlockA + 0x40;
  ctx.r3.u64 = obj;
  switch (rng() % 3) {
    case 0: ctx.r4.u64 = s.PreImage(obj + 56); break;
    case 1: ctx.r4.u64 = s.PreImage(obj + 80); break;
    default: break;
  }
}

void NameString(Scene& s, PPCContext& ctx, std::mt19937& rng) {
  static const char* const kNames[] = {"", "a", "abc", "abcd", "abce", "abcdef", "zzz"};
  s.Bytes(0x82080E6C, kNames[rng() % 7]);
  s.Bytes(kBlockB + 0x40, kNames[rng() % 7]);
  ctx.r4.u64 = kBlockB + 0x40;
}

void CharacterState(Scene& s, PPCContext& ctx, std::mt19937& rng) {
  const uint32_t character = kBlockA + 0x100;
  const uint32_t state = kBlockB + 0x200;  // the block the link points 32 bytes past
  s.Word(character + 292, rng() % 4 ? state + 32 : 0);
  s.Word(state + 6304, rng() % 2 ? 0 : uint32_t(rng()));
  s.Word(state + 6340, uint32_t(rng()));
  s.Word(state + 5456, uint32_t(rng()));
  ctx.r3.u64 = character;
}

void OrbMapView(Scene& s, PPCContext& ctx, std::mt19937& rng) {
  s.Float(kBlockA + 860, s.RandomFloat(rng));
  s.Float(kBlockA + 864, s.RandomFloat(rng));
  ctx.f1.f64 = rng() % 3 ? double(s.RandomFloat(rng)) : 0.1 * double(rng() % 100);
}

void ProfilerSlot(Scene& s, PPCContext& ctx, std::mt19937& rng) {
  s.Word(0x82D08E44, kBlockA + 0x3000);
  s.Float(kBlockA + 0x3000, s.RandomFloat(rng));
  s.Float(kBlockA + 0x3004, s.RandomFloat(rng));
  s.Word(0x82D07F9C, kBlockB);
  ctx.r3.u64 = rng() % 3 ? rng() % 64 : rng() % 512;
}

void EntryArray(Scene& s, PPCContext& ctx, std::mt19937& rng) {
  const uint32_t obj = kBlockA + 0x40, array = kBlockB + 0x40;
  const uint32_t count = rng() % 7;
  s.Word(obj + 60, count);
  s.Word(obj + 56, array);
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t entry = kBlockB + 0x1000 + 0x40 * i;
    s.Word(array + 4 * i, entry);
    for (uint32_t k = 12; k <= 24; k += 4) s.Word(entry + k, rng() % 2 ? rng() % 1000 : uint32_t(rng()));
  }
  ctx.r3.u64 = obj;
}

void FloatArray(Scene& s, PPCContext& ctx, std::mt19937& rng) {
  const uint32_t count = rng() % 2 ? rng() % 100 : rng() % 8;
  for (uint32_t i = 0; i < count; ++i) s.Float(kBlockA + 4 * i, std::abs(s.RandomFloat(rng)));
  // Sometimes a nonsense count: a negative one (skips both loops) or a small overrun into random memory; a large one
  // would walk the whole address space in both versions.
  ctx.r4.u64 = rng() % 8 ? count : (rng() % 2 ? 0xFFFFFFFFu : count + rng() % 300);
}

// --- batch 2 ---------------------------------------------------------------------------------------------------------

uint32_t Fnv1(const char* text) {
  uint32_t hash = 0x811C9DC5u;
  for (; *text; ++text) hash = (hash * 0x01000193u) ^ uint8_t(*text);
  return hash;
}

void Curves(Scene& s, PPCContext& ctx, std::mt19937& rng) {
  const uint32_t obj = kBlockA + 0x100, a = kBlockA + 0x1000, b = kBlockA + 0x1800;
  s.Word(obj + 528, rng() % 5 ? (rng() % 5) << 24 : 0xFF000000u);  // signed byte count in the top byte
  s.Word(obj + 632, kBlockB + 0x1000);
  s.Word(a + 136, kBlockB + 0x2000);
  s.Word(b + 136, kBlockB + 0x2800);
  for (uint32_t i = 0; i < 5 * 36; i += 4) {
    s.Float(kBlockB + 0x2000 + i, s.RandomFloat(rng));
    s.Float(kBlockB + 0x2800 + i, s.RandomFloat(rng));
  }
  ctx.r3.u64 = obj;
  ctx.r4.u64 = a;
  ctx.r5.u64 = b;
}

void Weighted(Scene& s, PPCContext& ctx, std::mt19937& rng) {
  for (PPCRegister* reg : {&ctx.f1, &ctx.f2, &ctx.f3, &ctx.f4}) {
    reg->f64 = rng() % 4 ? double(s.RandomFloat(rng)) : double(rng() % 7) / 3.0;
  }
}

void Elements(Scene& s, PPCContext& ctx, std::mt19937& rng) {
  const uint32_t count = rng() % 5, src = kBlockA + 0x100;
  for (uint32_t i = 0; i < count * 80; i += 4) s.Word(src + i, uint32_t(rng() % 100000));
  ctx.r4.u64 = src;
  ctx.r5.u64 = src + 80 * count;
  ctx.r6.u64 = rng() % 4 ? kBlockB + 0x100 : 0;
}

void HashTable(Scene& s, PPCContext& ctx, std::mt19937& rng) {
  static const char* const kNames[] = {"", "alpha", "beta", "gamma", "delta", "alphx"};
  const uint32_t table = 0x40020000, entries = kBlockA + 0x1000, strings = kBlockA + 0x3000;
  s.Word(0x82DE267C, table);
  s.Word(table + 0x2C064, 3);
  for (uint32_t i = 0; i < 6; ++i) s.Bytes(strings + 0x40 * i, kNames[i]);
  const uint32_t count = rng() % 4;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t entry = entries + 0x40 * i, name = rng() % 6;
    const uint32_t hash = rng() % 4 ? Fnv1(kNames[name]) : uint32_t(rng());
    s.Word(entry + 8, hash);
    s.Word(entry + 16, strings + 0x40 * name);
    s.Word(entry + 20, kBlockB + 0x100 + 0x80 * i);
    s.Word(kBlockB + 0x100 + 0x80 * i + 56, kBlockB + 0x140 + 0x80 * i);
    s.Word(kBlockB + 0x140 + 0x80 * i, rng() % 2 ? 7 : uint32_t(rng()));
    const uint32_t bucket = table + 0x27068 + 20 * (hash & 3);
    s.Word(entry + 4, s.PreImage(bucket));  // chain in front of the bucket's current head
    s.Word(bucket, entry);
  }
  ctx.r4.u64 = strings + 0x40 * (rng() % 6);
  ctx.r5.u64 = kBlockA + 0x200;
  s.Word(kBlockA + 0x200, rng() % 2 ? 7 : uint32_t(rng()));
  s.Word(0x82C3E470, rng() % 2 ? 7 : uint32_t(rng()));
}

void RingBuffer(Scene& s, PPCContext& ctx, std::mt19937& rng) {
  const uint32_t q = kBlockA + 0x100, storage = rng() % 5 ? kBlockB + 0x100 : 0;
  s.Word(q + 4, storage);
  s.Word(q + 8, storage + 4 * (rng() % 9));
  s.Word(q + 16, rng() % 4 ? rng() % 10 : 0xFFFFFFFFu);
  s.Word(q + 20, rng() % 4 ? rng() % 10 : 0xFFFFFFFEu);
  s.Word(q + 24, rng() % 12);
  ctx.r3.u64 = q;
  ctx.r4.u64 = kBlockA + 0x200;
}

void PageSlot(Scene& s, PPCContext& ctx, std::mt19937& rng) {
  const uint32_t list = kBlockA + 0x100, manager = kBlockA + 0x1000, owner = rng() % 4 ? kBlockB + 0x100 : 0;
  const uint32_t page_index = rng() % 3;
  s.Word(0x82DE2708, page_index);
  s.Word(list, owner);
  s.Word(list + 4, (rng() % 10) << 24);
  const uint32_t page = owner + 832 * page_index;
  s.Word(page + 816, ((rng() % 5) << 24) | ((rng() % 7) << 16));  // first slot, slot count
  for (uint32_t slot = 0; slot < 10; ++slot) s.Word(page + 16 + 4 * (slot + 56), kBlockA + 0x2000 + 96 * slot);
  s.Word(manager + 3120, manager + 3120 + 16 + 4 * (rng() % 6));
  for (uint32_t index = 0; index < 4; ++index) {
    s.Word(manager + 4 * (index + 784), rng() % 3 ? kBlockB + 0x2000 + 0x200 * index : 0);
  }
  ctx.r3.u64 = manager;
  ctx.r7.u64 = list;
  for (PPCRegister* reg : {&ctx.r4, &ctx.r5, &ctx.r6}) reg->u64 = rng() % 5 ? rng() % 4 : 0xFFFFu;
}

void LoadedTable(Scene& s, PPCContext& ctx, std::mt19937& rng) {
  const uint32_t header = kBlockA + 0x100;
  s.Word(header, rng() % 4);
  s.Word(header + 4, rng() % 4);
  s.Word(header + 8, 0x100);
  s.Word(header + 12, 0x200);
  for (uint32_t i = 0; i < 8; ++i) {
    s.Word(header + 0x100 + 8 * i, rng() % 0x1000);
    s.Word(header + 0x100 + 8 * i + 4, rng() % 4);
    s.Word(header + 0x200 + 8 * i, rng() % 0x1000);
  }
  ctx.r3.u64 = header;
}

void Glyphs(Scene& s, PPCContext& ctx, std::mt19937& rng) {
  s.Word(0x82D08D14, rng() % 4 ? kBlockA + 0x300 : 0);
  s.Word(kBlockA + 0x300, rng() % 4);
  s.Word(0x82D08B98, rng() % 2);
  s.Word(0x82D100C0, rng() % 64);
  ctx.r3.u64 = kBlockB + 0x100;
}

const Case kCases[] = {
    {0x8213BC38, &crackdown::hooks::decomp::Sub_8213BC38, false, false, ChunkList},
    {0x82144D80, &crackdown::hooks::decomp::Sub_82144D80, true, false, PlayerEntity},
    {0x8216F470, &crackdown::hooks::decomp::Sub_8216F470, false, false, PointerArray},
    {0x821CAA40, &crackdown::hooks::decomp::Sub_821CAA40, true, false, IndexedTable},
    {0x821E72D0, &crackdown::hooks::decomp::Sub_821E72D0, true, false, TwoCached},
    {0x821E8E90, &crackdown::hooks::decomp::Sub_821E8E90, true, false, NameString},
    {0x822B7408, &crackdown::hooks::decomp::Sub_822B7408, true, false, CharacterState},
    {0x823E9BD0, &crackdown::hooks::decomp::Sub_823E9BD0, false, false, OrbMapView},
    {0x82794868, &crackdown::hooks::decomp::Sub_82794868, false, false, nullptr},
    {0x8294AA18, &crackdown::hooks::decomp::Sub_8294AA18, false, false, ProfilerSlot},
    {0x82981388, &crackdown::hooks::decomp::Sub_82981388, true, false, EntryArray},
    {0x82A94078, &crackdown::hooks::decomp::Sub_82A94078, true, false, FloatArray},
    {0x82B47D50, &crackdown::hooks::decomp::Sub_82B47D50, false, false, nullptr},
    // batch 2
    {0x82340008, &crackdown::hooks::decomp::Sub_82340008, false, false, LoadedTable},
    {0x823925C0, &crackdown::hooks::decomp::Sub_823925C0, true, false, HashTable},
    {0x823B7838, &crackdown::hooks::decomp::Sub_823B7838, false, false, nullptr},
    {0x82463160, &crackdown::hooks::decomp::Sub_82463160, false, false, nullptr},
    {0x8246DAE0, &crackdown::hooks::decomp::Sub_8246DAE0, true, false, RingBuffer},
    {0x824ABED0, &crackdown::hooks::decomp::Sub_824ABED0, false, false, Curves},
    {0x824DA608, &crackdown::hooks::decomp::Sub_824DA608, false, false, PageSlot},
    {0x825FF080, &crackdown::hooks::decomp::Sub_825FF080, false, false, Elements},
    {0x8272F260, &crackdown::hooks::decomp::Sub_8272F260, false, false, nullptr},
    {0x82944E58, &crackdown::hooks::decomp::Sub_82944E58, false, false, Glyphs},
    {0x82A95780, &crackdown::hooks::decomp::Sub_82A95780, false, true, Weighted},
    // batch 3: small loop-free leaves, default scene
    {0x8213F180, &crackdown::hooks::decomp::Sub_8213F180, false, false, nullptr},
    {0x821BFCC0, &crackdown::hooks::decomp::Sub_821BFCC0, false, false, nullptr},
    {0x821F58D0, &crackdown::hooks::decomp::Sub_821F58D0, false, false, nullptr},
    {0x82258550, &crackdown::hooks::decomp::Sub_82258550, false, false, nullptr},
    {0x82270B80, &crackdown::hooks::decomp::Sub_82270B80, false, false, nullptr},
    {0x822EBBB0, &crackdown::hooks::decomp::Sub_822EBBB0, false, false, nullptr},
    {0x82311448, &crackdown::hooks::decomp::Sub_82311448, false, false, nullptr},
    {0x82326300, &crackdown::hooks::decomp::Sub_82326300, false, false, nullptr},
    {0x82369348, &crackdown::hooks::decomp::Sub_82369348, false, true, nullptr},
    {0x824879B8, &crackdown::hooks::decomp::Sub_824879B8, false, false, nullptr},
    {0x824888C8, &crackdown::hooks::decomp::Sub_824888C8, false, false, nullptr},
    {0x825065E0, &crackdown::hooks::decomp::Sub_825065E0, false, false, nullptr},
    {0x8253C118, &crackdown::hooks::decomp::Sub_8253C118, true, false, nullptr},
    {0x8265CDC8, &crackdown::hooks::decomp::Sub_8265CDC8, false, false, nullptr},
    {0x8269B3C8, &crackdown::hooks::decomp::Sub_8269B3C8, false, false, nullptr},
    {0x8273B2F8, &crackdown::hooks::decomp::Sub_8273B2F8, false, false, nullptr},
    {0x82793BB8, &crackdown::hooks::decomp::Sub_82793BB8, false, false, nullptr},
    {0x8281BDD0, &crackdown::hooks::decomp::Sub_8281BDD0, true, false, nullptr},
    {0x828D2700, &crackdown::hooks::decomp::Sub_828D2700, false, false, nullptr},
    {0x82993038, &crackdown::hooks::decomp::Sub_82993038, false, false, nullptr},
    {0x829F1AD8, &crackdown::hooks::decomp::Sub_829F1AD8, true, false, nullptr},
    {0x82A85C38, &crackdown::hooks::decomp::Sub_82A85C38, false, false, nullptr},
    {0x82A89BE0, &crackdown::hooks::decomp::Sub_82A89BE0, true, false, nullptr},
    {0x82B1F4A8, &crackdown::hooks::decomp::Sub_82B1F4A8, false, false, nullptr},
    {0x82B1F4D0, &crackdown::hooks::decomp::Sub_82B1F4D0, false, false, nullptr},
};

}  // namespace

int main(int argc, char** argv) {
  int trials = 48;
  if (argc > 2 && std::string(argv[1]) == "--trials") trials = std::atoi(argv[2]);
  g_default_csr = rex::ppc::FPSCRRegister::Platform::getcsr();
  std::thread(Watchdog).detach();
  guest_sandbox::Memory::Get();

  int failures = 0;
  for (const Case& c : kCases) {
    const guest_sandbox::FunctionEntry* original = nullptr;
    for (unsigned i = 0; i < kDecompOriginalCount; ++i) {
      if (kDecompOriginals[i].address == c.address) original = &kDecompOriginals[i];
    }
    if (!original) {
      std::printf("sub_%08X: not extracted (add it to tests/decomp/functions.txt)\n", c.address);
      ++failures;
      continue;
    }
    int mismatches = 0;
    for (int trial = 0; trial < trials && mismatches < 3; ++trial) {
      const uint32_t seed = 0x5EED0000u + uint32_t(trial) * 7919u + (c.address & 0xFFFF);
      const Outcome expected = Run(c, original->function, seed);
      const Outcome actual = Run(c, c.native, seed);
      std::string problem;
      if (c.compare_r3 && expected.r3 != actual.r3) {
        problem = "r3 " + std::to_string(expected.r3) + " vs " + std::to_string(actual.r3);
      } else if (c.compare_f1 && expected.f1 != actual.f1) {
        double e, a;
        std::memcpy(&e, &expected.f1, 8);
        std::memcpy(&a, &actual.f1, 8);
        problem = "f1 " + std::to_string(e) + " vs " + std::to_string(a);
      } else if (expected.changes != actual.changes) {
        size_t i = 0;
        while (i < expected.changes.size() && i < actual.changes.size() && expected.changes[i] == actual.changes[i]) ++i;
        char text[160];
        if (i < expected.changes.size() && i < actual.changes.size()) {
          std::snprintf(text, sizeof text, "memory: original wrote %08X=%08X, native %08X=%08X", expected.changes[i].first,
                        expected.changes[i].second, actual.changes[i].first, actual.changes[i].second);
        } else if (i < expected.changes.size()) {
          std::snprintf(text, sizeof text, "memory: native missed the write %08X=%08X", expected.changes[i].first,
                        expected.changes[i].second);
        } else {
          std::snprintf(text, sizeof text, "memory: native wrote %08X=%08X, the original did not", actual.changes[i].first,
                        actual.changes[i].second);
        }
        problem = text;
      }
      if (!problem.empty()) {
        std::printf("sub_%08X trial %d (seed %08X): %s\n", c.address, trial, seed, problem.c_str());
        ++mismatches;
      }
    }
    if (mismatches) ++failures;
    else std::printf("sub_%08X: %d trials equal\n", c.address, trials);
  }
  std::printf("decomp_io_tests: %zu translations, %d failing\n", std::size(kCases), failures);
  return failures ? 1 : 0;
}
