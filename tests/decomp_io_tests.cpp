// Runs each native translation of src/decomp against the recompiled original in the guest sandbox on the same random
// inputs (registers, two object blocks, the image globals the function reads), and compares what they produce: the
// result register r3 (when the function returns one) and every changed word of guest memory outside the stack's red
// zone. The originals are extracted from generated/default at configure time (tests/cmake/decomp_originals.cmake);
// tests/decomp/functions.txt lists them. A difference names the function, the trial and the first differing item.
// Run: decomp_io_tests [--trials N]

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
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
  void Double(uint32_t address, double value) {
    uint64_t bits;
    std::memcpy(&bits, &value, 8);
    Word(address, uint32_t(bits >> 32));
    Word(address + 4, uint32_t(bits));
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
      switch (rng() % 8) {
        case 0: value = 0; break;
        case 1: value = rng() % 16; break;
        case 2: value = rng() % 1024; break;
        case 3: value = Pointer(rng); break;
        // Float NaNs and infinities (signalling NaNs included): a float copied through the PowerPC's lfs / stfs
        // comes out quiet, and a translation that copies the bits as they are is caught here.
        case 4: value = 0x7F800000u | (uint32_t(rng()) & 0x807FFFFFu); break;
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

// A guest word a run changed, with the other word of its 8-byte group (so a NaN double can be recognised).
struct Change {
  uint32_t address, value, partner;
  bool operator<(const Change& other) const { return address < other.address; }
};

struct Outcome {
  uint64_t r3 = 0;
  uint64_t f1 = 0;  // bits
  std::vector<Change> changes;  // sorted by address
};

// NaN payloads are not game state: the recompiled code's x86 conversions quiet a signalling NaN where the console's
// lfs / stfs did not, and the operand order of a NaN-producing addition picks which payload survives. Two values that
// are both NaNs (as floats, or as the double their 8-byte group forms) count as equal.
bool FloatNaN(uint32_t bits) { return (bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0; }
bool DoubleNaN(const Change& c) {
  const uint32_t hi = (c.address & 4) ? c.partner : c.value, lo = (c.address & 4) ? c.value : c.partner;
  return (hi & 0x7FF00000u) == 0x7FF00000u && ((hi & 0x000FFFFFu) != 0 || lo != 0);
}
bool NaNWrite(const Change& c) { return FloatNaN(c.value) || DoubleNaN(c); }

// The first difference between two runs' memory changes that is not a NaN payload, or an empty string.
std::string CompareChanges(const std::vector<Change>& expected, const std::vector<Change>& actual) {
  char text[160];
  size_t i = 0, j = 0;
  while (i < expected.size() || j < actual.size()) {
    if (i < expected.size() && j < actual.size() && expected[i].address == actual[j].address) {
      const Change &e = expected[i], &a = actual[j];
      if (e.value != a.value && !(FloatNaN(e.value) && FloatNaN(a.value)) && !(DoubleNaN(e) && DoubleNaN(a))) {
        std::snprintf(text, sizeof text, "memory: original wrote %08X=%08X, native %08X", e.address, e.value, a.value);
        return text;
      }
      ++i;
      ++j;
    } else if (j >= actual.size() || (i < expected.size() && expected[i].address < actual[j].address)) {
      if (!NaNWrite(expected[i])) {
        std::snprintf(text, sizeof text, "memory: native missed the write %08X=%08X", expected[i].address, expected[i].value);
        return text;
      }
      ++i;
    } else {
      if (!NaNWrite(actual[j])) {
        std::snprintf(text, sizeof text, "memory: native wrote %08X=%08X, the original did not", actual[j].address, actual[j].value);
        return text;
      }
      ++j;
    }
  }
  return "";
}

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
  // Batch 4 globals.
  for (uint32_t address : {0x82BADDC8u, 0x8309153Cu, 0x82BAE054u, 0x82BAE058u}) scene.Word(address, uint32_t(rng()));
  // Batch 5 globals: a global object chain, a profiler block pointer, a counter, an RTTI offset word of the image.
  scene.Word(0x82DE2674, kBlockA + 0x500);
  scene.Word(kBlockA + 0x500 + 628, kBlockA + 0x600);
  scene.Float(0x820722D0, scene.RandomFloat(rng));
  scene.Word(0x82BAE748, uint32_t(rng()));
  scene.Word(0x82DE44A8, uint32_t(rng()));
  scene.Word(0x820E7484, rng() % 64);
  scene.Word(0x82D07F9C, kBlockB);
  // Batch 6 globals: constants, the LCG seed, two doubles of the floor helper.
  for (uint32_t address : {0x820ED650u, 0x82BADF60u, 0x82C63D5Cu, 0x830D218Cu, 0x82CFDF48u}) scene.Word(address, uint32_t(rng()));
  scene.Float(0x820EDB48, scene.RandomFloat(rng));
  scene.Float(0x82BAB364, scene.RandomFloat(rng));
  scene.Double(0x82063D20, rng() % 2 ? 4503599627370496.0 : double(scene.RandomFloat(rng)));
  scene.Double(0x820ED510, rng() % 2 ? -1.0 : double(scene.RandomFloat(rng)));
  // Batch 7 globals: constants, two LCG seeds, a global with seven handle pointers.
  for (uint32_t address : {0x8206F394u, 0x82E34428u, 0x830A5B70u, 0x8207094Cu, 0x82071E88u, 0x820709A8u, 0x8204AAC8u}) {
    scene.Word(address, uint32_t(rng()));
  }
  scene.Float(0x820ED814, scene.RandomFloat(rng));
  scene.Word(0x82DE4160, kBlockA + 0x700);
  for (uint32_t offset : {256u, 268u, 272u, 276u, 280u, 284u, 288u}) scene.Word(kBlockA + 0x700 + offset, scene.Pointer(rng));
  // Batch 8 globals: constants and the registered global object (vtable with a small RTTI offset).
  for (uint32_t address : {0x820023C8u, 0x82000A88u, 0x82000B84u, 0x82D300F4u}) scene.Word(address, uint32_t(rng()));
  for (uint32_t address : {0x82BADF5Cu, 0x82C626C0u, 0x8205AFB0u, 0x8205EA10u}) scene.Float(address, scene.RandomFloat(rng));
  scene.Word(0x82D300B4, kBlockA + 0x900);
  scene.Word(kBlockA + 0x904, rng() % 64);
  // Batch 9 globals: a session pair, the weather constants, more image constants.
  scene.Word(0x82DE3EA8, rng() % 4 ? kBlockA + 0xA00 : 0);
  scene.Word(0x82DE3EA0, kBlockA + 0xB00);
  for (uint32_t address : {0x82DE3FE8u, 0x820ED56Cu, 0x8207092Cu, 0x83098208u, 0x820EDB14u, 0x82000A8Cu, 0x820EE164u,
                           0x820EE160u, 0x820EE15Cu, 0x820EE158u, 0x820EE154u, 0x82072D7Cu, 0x820704ACu, 0x82071F00u,
                           0x820ED5F0u}) {
    scene.Word(address, uint32_t(rng()));
  }
  for (uint32_t address : {0x820708BCu, 0x820EDDB0u, 0x82BAE768u}) scene.Float(address, scene.RandomFloat(rng));

  // Batch 10 globals.
  for (uint32_t address : {0x82071A84u}) scene.Word(address, uint32_t(rng()));
  for (uint32_t address : {0x820EDA8Cu, 0x820ED728u, 0x820722E8u, 0x82C63FE4u}) scene.Float(address, scene.RandomFloat(rng));

  // Batch 11 globals.
  for (uint32_t address : {0x82BAB4F8u, 0x82001C44u, 0x82001C48u}) scene.Float(address, scene.RandomFloat(rng));
  scene.Word(0x82DE2368, kBlockA + 0xC00);
  scene.Double(0x820ED4C0, double(scene.RandomFloat(rng)));

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
      if (value != scene.PreImage(address)) {
        outcome.changes.push_back({address, value, scene.memory.Read32(address ^ 4)});
      }
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

void Globals4(Scene& s, PPCContext& ctx, std::mt19937& rng) {
  // A global counter pointer and a global block pointer into block A with small counts.
  s.Word(0x82D82F5C, kBlockA + 0x300);
  s.Word(kBlockA + 0x300, rng() % 8);
  s.Word(0x82DE4F40, kBlockA + 0x400);
  s.Word(kBlockA + 0x400, rng() % 6);
  (void)ctx;
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
    // batch 4
    {0x821681D0, &crackdown::hooks::decomp::Sub_821681D0, false, false, nullptr},
    {0x821C5D38, &crackdown::hooks::decomp::Sub_821C5D38, false, false, nullptr},
    {0x821C5D70, &crackdown::hooks::decomp::Sub_821C5D70, false, false, nullptr},
    {0x82202068, &crackdown::hooks::decomp::Sub_82202068, true, false, nullptr},
    {0x822ECBC0, &crackdown::hooks::decomp::Sub_822ECBC0, false, false, nullptr},
    {0x822FE9F0, &crackdown::hooks::decomp::Sub_822FE9F0, false, false, nullptr},
    {0x82305158, &crackdown::hooks::decomp::Sub_82305158, false, false, nullptr},
    {0x8230EE50, &crackdown::hooks::decomp::Sub_8230EE50, false, false, nullptr},
    {0x82315E00, &crackdown::hooks::decomp::Sub_82315E00, false, false, nullptr},
    {0x824748B0, &crackdown::hooks::decomp::Sub_824748B0, false, false, nullptr},
    {0x82474CA8, &crackdown::hooks::decomp::Sub_82474CA8, false, false, nullptr},
    {0x8258AEA8, &crackdown::hooks::decomp::Sub_8258AEA8, false, false, nullptr},
    {0x825BFA18, &crackdown::hooks::decomp::Sub_825BFA18, false, false, Globals4},
    {0x8261BCF8, &crackdown::hooks::decomp::Sub_8261BCF8, true, false, nullptr},
    {0x8264FAA0, &crackdown::hooks::decomp::Sub_8264FAA0, false, false, nullptr},
    {0x82793ED8, &crackdown::hooks::decomp::Sub_82793ED8, false, false, nullptr},
    {0x827F4660, &crackdown::hooks::decomp::Sub_827F4660, true, false, nullptr},
    {0x82832F88, &crackdown::hooks::decomp::Sub_82832F88, false, false, nullptr},
    {0x8287A968, &crackdown::hooks::decomp::Sub_8287A968, true, false, nullptr},
    {0x8293D600, &crackdown::hooks::decomp::Sub_8293D600, false, false, nullptr},
    {0x829518F0, &crackdown::hooks::decomp::Sub_829518F0, false, false, nullptr},
    {0x82AC4470, &crackdown::hooks::decomp::Sub_82AC4470, false, true, nullptr},
    {0x82AEDC80, &crackdown::hooks::decomp::Sub_82AEDC80, false, false, Globals4},
    {0x82B0FDA8, &crackdown::hooks::decomp::Sub_82B0FDA8, false, false, nullptr},
    {0x82B0FE18, &crackdown::hooks::decomp::Sub_82B0FE18, false, false, nullptr},
    // batch 5
    {0x8216F4E8, &crackdown::hooks::decomp::Sub_8216F4E8, false, false, nullptr},
    {0x821F3320, &crackdown::hooks::decomp::Sub_821F3320, false, false, nullptr},
    {0x821F39B0, &crackdown::hooks::decomp::Sub_821F39B0, false, false, nullptr},
    {0x82284F58, &crackdown::hooks::decomp::Sub_82284F58, false, false, nullptr},
    {0x822EB240, &crackdown::hooks::decomp::Sub_822EB240, false, false, nullptr},
    {0x822FB238, &crackdown::hooks::decomp::Sub_822FB238, false, false, nullptr},
    {0x8230A578, &crackdown::hooks::decomp::Sub_8230A578, false, false, nullptr},
    {0x82315CE0, &crackdown::hooks::decomp::Sub_82315CE0, false, false, nullptr},
    {0x823FA9A8, &crackdown::hooks::decomp::Sub_823FA9A8, false, false, nullptr},
    {0x82473790, &crackdown::hooks::decomp::Sub_82473790, false, false, nullptr},
    {0x82479668, &crackdown::hooks::decomp::Sub_82479668, false, false, nullptr},
    {0x825D3808, &crackdown::hooks::decomp::Sub_825D3808, false, false, nullptr},
    {0x825F6CC8, &crackdown::hooks::decomp::Sub_825F6CC8, false, false, nullptr},
    {0x82626BE8, &crackdown::hooks::decomp::Sub_82626BE8, false, false, nullptr},
    {0x82694200, &crackdown::hooks::decomp::Sub_82694200, false, false, nullptr},
    {0x827944D8, &crackdown::hooks::decomp::Sub_827944D8, false, false, nullptr},
    {0x82794510, &crackdown::hooks::decomp::Sub_82794510, false, false, nullptr},
    {0x8281B120, &crackdown::hooks::decomp::Sub_8281B120, false, false, nullptr},
    {0x828FEE28, &crackdown::hooks::decomp::Sub_828FEE28, false, false, nullptr},
    {0x82953330, &crackdown::hooks::decomp::Sub_82953330, false, false, nullptr},
    {0x829EBFF8, &crackdown::hooks::decomp::Sub_829EBFF8, false, false, nullptr},
    {0x82A3F0A0, &crackdown::hooks::decomp::Sub_82A3F0A0, false, false, nullptr},
    {0x82A792E0, &crackdown::hooks::decomp::Sub_82A792E0, false, false, nullptr},
    {0x82B37038, &crackdown::hooks::decomp::Sub_82B37038, false, false, nullptr},
    {0x82B37118, &crackdown::hooks::decomp::Sub_82B37118, false, false, nullptr},
    // batch 6
    {0x8213E3C8, &crackdown::hooks::decomp::Sub_8213E3C8, false, false, nullptr},
    {0x82185E40, &crackdown::hooks::decomp::Sub_82185E40, true, false, nullptr},
    {0x8218DC98, &crackdown::hooks::decomp::Sub_8218DC98, false, false, nullptr},
    {0x82213798, &crackdown::hooks::decomp::Sub_82213798, false, false, nullptr},
    {0x822E9A98, &crackdown::hooks::decomp::Sub_822E9A98, false, false, nullptr},
    {0x822F9FA0, &crackdown::hooks::decomp::Sub_822F9FA0, false, false, nullptr},
    {0x82303670, &crackdown::hooks::decomp::Sub_82303670, false, false, nullptr},
    {0x823CA8F0, &crackdown::hooks::decomp::Sub_823CA8F0, false, false, nullptr},
    {0x823CAA10, &crackdown::hooks::decomp::Sub_823CAA10, false, false, nullptr},
    {0x8240B648, &crackdown::hooks::decomp::Sub_8240B648, false, false, nullptr},
    {0x82476C50, &crackdown::hooks::decomp::Sub_82476C50, false, false, nullptr},
    {0x82581ED0, &crackdown::hooks::decomp::Sub_82581ED0, false, false, nullptr},
    {0x82597840, &crackdown::hooks::decomp::Sub_82597840, false, false, nullptr},
    {0x826011F8, &crackdown::hooks::decomp::Sub_826011F8, false, false, nullptr},
    {0x82652B30, &crackdown::hooks::decomp::Sub_82652B30, false, false, nullptr},
    {0x82726F50, &crackdown::hooks::decomp::Sub_82726F50, false, true, nullptr},
    {0x82726FC8, &crackdown::hooks::decomp::Sub_82726FC8, true, false, nullptr},
    {0x82807698, &crackdown::hooks::decomp::Sub_82807698, true, false, nullptr},
    {0x8284D670, &crackdown::hooks::decomp::Sub_8284D670, true, false, nullptr},
    {0x8298FA58, &crackdown::hooks::decomp::Sub_8298FA58, false, false, nullptr},
    {0x829F0F40, &crackdown::hooks::decomp::Sub_829F0F40, false, false, nullptr},
    {0x82AB52F0, &crackdown::hooks::decomp::Sub_82AB52F0, true, false, nullptr},
    {0x82AB5FD0, &crackdown::hooks::decomp::Sub_82AB5FD0, false, true, nullptr},
    {0x82B48FE8, &crackdown::hooks::decomp::Sub_82B48FE8, false, false, nullptr},
    {0x82B4A590, &crackdown::hooks::decomp::Sub_82B4A590, false, false, nullptr},
    // batch 7
    {0x821577C0, &crackdown::hooks::decomp::Sub_821577C0, false, false, nullptr},
    {0x821584F8, &crackdown::hooks::decomp::Sub_821584F8, false, false, nullptr},
    {0x82158588, &crackdown::hooks::decomp::Sub_82158588, false, false, nullptr},
    {0x82271AD8, &crackdown::hooks::decomp::Sub_82271AD8, false, false, nullptr},
    {0x822F9AD8, &crackdown::hooks::decomp::Sub_822F9AD8, false, false, nullptr},
    {0x822FD2E8, &crackdown::hooks::decomp::Sub_822FD2E8, false, false, nullptr},
    {0x8230E280, &crackdown::hooks::decomp::Sub_8230E280, false, false, nullptr},
    {0x8232EFB0, &crackdown::hooks::decomp::Sub_8232EFB0, false, false, nullptr},
    {0x823CD950, &crackdown::hooks::decomp::Sub_823CD950, false, false, nullptr},
    {0x8240B7B8, &crackdown::hooks::decomp::Sub_8240B7B8, false, false, nullptr},
    {0x82474CE0, &crackdown::hooks::decomp::Sub_82474CE0, false, false, nullptr},
    {0x82477E88, &crackdown::hooks::decomp::Sub_82477E88, false, false, nullptr},
    {0x8250ADE0, &crackdown::hooks::decomp::Sub_8250ADE0, false, false, nullptr},
    {0x825111A8, &crackdown::hooks::decomp::Sub_825111A8, false, false, nullptr},
    {0x826D5C28, &crackdown::hooks::decomp::Sub_826D5C28, false, false, nullptr},
    {0x82730D90, &crackdown::hooks::decomp::Sub_82730D90, false, false, nullptr},
    {0x827C7F08, &crackdown::hooks::decomp::Sub_827C7F08, true, false, nullptr},
    {0x8283EEC8, &crackdown::hooks::decomp::Sub_8283EEC8, true, false, nullptr},
    {0x82846518, &crackdown::hooks::decomp::Sub_82846518, false, false, nullptr},
    {0x8297DEB0, &crackdown::hooks::decomp::Sub_8297DEB0, false, false, nullptr},
    {0x8298D7B0, &crackdown::hooks::decomp::Sub_8298D7B0, false, false, nullptr},
    {0x82A24A70, &crackdown::hooks::decomp::Sub_82A24A70, true, false, nullptr},
    {0x82AB52B0, &crackdown::hooks::decomp::Sub_82AB52B0, true, false, nullptr},
    {0x82B4A610, &crackdown::hooks::decomp::Sub_82B4A610, false, false, nullptr},
    {0x82B4A7A0, &crackdown::hooks::decomp::Sub_82B4A7A0, false, false, nullptr},
    // batch 8
    {0x82157A18, &crackdown::hooks::decomp::Sub_82157A18, false, false, nullptr},
    {0x821584B0, &crackdown::hooks::decomp::Sub_821584B0, false, false, nullptr},
    {0x82158540, &crackdown::hooks::decomp::Sub_82158540, false, false, nullptr},
    {0x82213708, &crackdown::hooks::decomp::Sub_82213708, false, false, nullptr},
    {0x822137E0, &crackdown::hooks::decomp::Sub_822137E0, false, true, nullptr},
    {0x822FC1A8, &crackdown::hooks::decomp::Sub_822FC1A8, false, false, nullptr},
    {0x82314E50, &crackdown::hooks::decomp::Sub_82314E50, false, false, nullptr},
    {0x82316328, &crackdown::hooks::decomp::Sub_82316328, false, false, nullptr},
    {0x823D19A8, &crackdown::hooks::decomp::Sub_823D19A8, false, false, nullptr},
    {0x8240B8D0, &crackdown::hooks::decomp::Sub_8240B8D0, false, false, nullptr},
    {0x82474118, &crackdown::hooks::decomp::Sub_82474118, false, false, nullptr},
    {0x824C55E8, &crackdown::hooks::decomp::Sub_824C55E8, false, true, nullptr},
    {0x82581CF8, &crackdown::hooks::decomp::Sub_82581CF8, false, false, nullptr},
    {0x8259FD50, &crackdown::hooks::decomp::Sub_8259FD50, false, false, nullptr},
    {0x825FBDB8, &crackdown::hooks::decomp::Sub_825FBDB8, false, false, nullptr},
    {0x82740710, &crackdown::hooks::decomp::Sub_82740710, false, false, nullptr},
    {0x82794E68, &crackdown::hooks::decomp::Sub_82794E68, false, false, nullptr},
    {0x82832BF8, &crackdown::hooks::decomp::Sub_82832BF8, true, false, nullptr},
    {0x8285E988, &crackdown::hooks::decomp::Sub_8285E988, false, false, nullptr},
    {0x8294DDD0, &crackdown::hooks::decomp::Sub_8294DDD0, false, false, nullptr},
    {0x829DC450, &crackdown::hooks::decomp::Sub_829DC450, false, false, nullptr},
    {0x82A4BF98, &crackdown::hooks::decomp::Sub_82A4BF98, false, false, nullptr},
    {0x82A97790, &crackdown::hooks::decomp::Sub_82A97790, true, false, nullptr},
    {0x82B0FEC0, &crackdown::hooks::decomp::Sub_82B0FEC0, false, false, nullptr},
    {0x82B52010, &crackdown::hooks::decomp::Sub_82B52010, false, false, nullptr},
    // batch 9
    {0x82140CE0, &crackdown::hooks::decomp::Sub_82140CE0, false, true, nullptr},
    {0x8215A4A0, &crackdown::hooks::decomp::Sub_8215A4A0, false, false, nullptr},
    {0x821AA050, &crackdown::hooks::decomp::Sub_821AA050, false, false, nullptr},
    {0x822C5EC8, &crackdown::hooks::decomp::Sub_822C5EC8, false, false, nullptr},
    {0x822DEFD0, &crackdown::hooks::decomp::Sub_822DEFD0, false, false, nullptr},
    {0x822F0F08, &crackdown::hooks::decomp::Sub_822F0F08, false, false, nullptr},
    {0x82307B70, &crackdown::hooks::decomp::Sub_82307B70, false, false, nullptr},
    {0x82309F98, &crackdown::hooks::decomp::Sub_82309F98, false, false, nullptr},
    {0x8232AD98, &crackdown::hooks::decomp::Sub_8232AD98, false, false, nullptr},
    {0x8240BC48, &crackdown::hooks::decomp::Sub_8240BC48, false, false, nullptr},
    {0x82475DD0, &crackdown::hooks::decomp::Sub_82475DD0, false, false, nullptr},
    {0x824DE458, &crackdown::hooks::decomp::Sub_824DE458, false, false, nullptr},
    {0x82555B38, &crackdown::hooks::decomp::Sub_82555B38, false, false, nullptr},
    {0x82581E18, &crackdown::hooks::decomp::Sub_82581E18, false, true, nullptr},
    {0x825A7068, &crackdown::hooks::decomp::Sub_825A7068, true, false, nullptr},
    {0x8273CC98, &crackdown::hooks::decomp::Sub_8273CC98, false, false, nullptr},
    {0x82794140, &crackdown::hooks::decomp::Sub_82794140, false, false, nullptr},
    {0x82845E00, &crackdown::hooks::decomp::Sub_82845E00, false, false, nullptr},
    {0x8287CCF0, &crackdown::hooks::decomp::Sub_8287CCF0, false, false, nullptr},
    {0x8292D4B0, &crackdown::hooks::decomp::Sub_8292D4B0, true, false, nullptr},
    {0x8298A828, &crackdown::hooks::decomp::Sub_8298A828, false, false, nullptr},
    {0x82A45648, &crackdown::hooks::decomp::Sub_82A45648, true, false, nullptr},
    {0x82A85FD0, &crackdown::hooks::decomp::Sub_82A85FD0, true, false, nullptr},
    {0x82B0FF08, &crackdown::hooks::decomp::Sub_82B0FF08, false, false, nullptr},
    {0x82B0FF58, &crackdown::hooks::decomp::Sub_82B0FF58, false, false, nullptr},
    // batch 10
    {0x82140C90, &crackdown::hooks::decomp::Sub_82140C90, false, true, nullptr},
    {0x82199AA8, &crackdown::hooks::decomp::Sub_82199AA8, false, false, nullptr},
    {0x821A9C00, &crackdown::hooks::decomp::Sub_821A9C00, false, false, nullptr},
    {0x822979C8, &crackdown::hooks::decomp::Sub_822979C8, false, false, nullptr},
    {0x823159A0, &crackdown::hooks::decomp::Sub_823159A0, false, false, nullptr},
    {0x8232AD48, &crackdown::hooks::decomp::Sub_8232AD48, false, false, nullptr},
    {0x823EA600, &crackdown::hooks::decomp::Sub_823EA600, false, false, nullptr},
    {0x8240C038, &crackdown::hooks::decomp::Sub_8240C038, false, false, nullptr},
    {0x8247A720, &crackdown::hooks::decomp::Sub_8247A720, false, false, nullptr},
    {0x82481B08, &crackdown::hooks::decomp::Sub_82481B08, false, false, nullptr},
    {0x8258C098, &crackdown::hooks::decomp::Sub_8258C098, false, false, nullptr},
    {0x825DD2B0, &crackdown::hooks::decomp::Sub_825DD2B0, false, false, nullptr},
    {0x825F1890, &crackdown::hooks::decomp::Sub_825F1890, false, false, nullptr},
    {0x8272A530, &crackdown::hooks::decomp::Sub_8272A530, false, false, nullptr},
    {0x82793DF8, &crackdown::hooks::decomp::Sub_82793DF8, false, false, nullptr},
    {0x827C5CC8, &crackdown::hooks::decomp::Sub_827C5CC8, false, false, nullptr},
    {0x828397B8, &crackdown::hooks::decomp::Sub_828397B8, true, false, nullptr},
    {0x8284DBD8, &crackdown::hooks::decomp::Sub_8284DBD8, false, false, nullptr},
    {0x8285B180, &crackdown::hooks::decomp::Sub_8285B180, false, false, nullptr},
    {0x829EC830, &crackdown::hooks::decomp::Sub_829EC830, false, false, nullptr},
    {0x829F1438, &crackdown::hooks::decomp::Sub_829F1438, false, false, nullptr},
    {0x82AAAB88, &crackdown::hooks::decomp::Sub_82AAAB88, false, false, nullptr},
    {0x82AED2C0, &crackdown::hooks::decomp::Sub_82AED2C0, false, false, nullptr},
    {0x82B0FB40, &crackdown::hooks::decomp::Sub_82B0FB40, false, false, nullptr},
    {0x82B22708, &crackdown::hooks::decomp::Sub_82B22708, false, false, nullptr},
    // batch 11
    {0x82150CF0, &crackdown::hooks::decomp::Sub_82150CF0, false, true, nullptr},
    {0x821A17C8, &crackdown::hooks::decomp::Sub_821A17C8, false, true, nullptr},
    {0x821BC260, &crackdown::hooks::decomp::Sub_821BC260, false, true, nullptr},
    {0x82312AB8, &crackdown::hooks::decomp::Sub_82312AB8, false, false, nullptr},
    {0x823137F8, &crackdown::hooks::decomp::Sub_823137F8, false, false, nullptr},
    {0x82315270, &crackdown::hooks::decomp::Sub_82315270, false, false, nullptr},
    {0x8240BAC8, &crackdown::hooks::decomp::Sub_8240BAC8, false, false, nullptr},
    {0x82445008, &crackdown::hooks::decomp::Sub_82445008, false, false, nullptr},
    {0x824A3BC8, &crackdown::hooks::decomp::Sub_824A3BC8, false, false, nullptr},
    {0x82586538, &crackdown::hooks::decomp::Sub_82586538, false, false, nullptr},
    {0x82587D30, &crackdown::hooks::decomp::Sub_82587D30, false, false, nullptr},
    {0x825DD398, &crackdown::hooks::decomp::Sub_825DD398, false, false, nullptr},
    {0x8272C558, &crackdown::hooks::decomp::Sub_8272C558, false, false, nullptr},
    {0x82794F60, &crackdown::hooks::decomp::Sub_82794F60, false, false, nullptr},
    {0x82795160, &crackdown::hooks::decomp::Sub_82795160, false, false, nullptr},
    {0x82839CF0, &crackdown::hooks::decomp::Sub_82839CF0, true, false, nullptr},
    {0x82844E88, &crackdown::hooks::decomp::Sub_82844E88, false, false, nullptr},
    {0x828CDC28, &crackdown::hooks::decomp::Sub_828CDC28, false, false, nullptr},
    {0x82957EE8, &crackdown::hooks::decomp::Sub_82957EE8, false, false, nullptr},
    {0x829DCDC8, &crackdown::hooks::decomp::Sub_829DCDC8, false, false, nullptr},
    {0x829EC030, &crackdown::hooks::decomp::Sub_829EC030, false, false, nullptr},
    {0x82A4F8A8, &crackdown::hooks::decomp::Sub_82A4F8A8, false, false, nullptr},
    {0x82A85E18, &crackdown::hooks::decomp::Sub_82A85E18, true, false, nullptr},
    {0x82B47F28, &crackdown::hooks::decomp::Sub_82B47F28, false, false, nullptr},
    {0x82B6F290, &crackdown::hooks::decomp::Sub_82B6F290, false, false, nullptr},
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
      } else if (c.compare_f1 && expected.f1 != actual.f1 &&
                 !(std::isnan(std::bit_cast<double>(expected.f1)) && std::isnan(std::bit_cast<double>(actual.f1)))) {
        problem = "f1 " + std::to_string(std::bit_cast<double>(expected.f1)) + " vs " +
                  std::to_string(std::bit_cast<double>(actual.f1));
      } else {
        problem = CompareChanges(expected.changes, actual.changes);
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
