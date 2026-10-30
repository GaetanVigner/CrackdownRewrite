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
  std::vector<std::pair<uint32_t, uint32_t>> written;  // (address, value) of every word written

  void Word(uint32_t address, uint32_t value) {
    memory.Write32(address, value);
    written.emplace_back(address, value);
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
    for (auto it = written.rbegin(); it != written.rend(); ++it) {
      if (it->first == address) return it->second;
    }
    return 0;
  }
};

struct Case {
  uint32_t address;
  Native native;
  bool compare_r3;  // the function returns a value in r3
  // Writes the inputs the function dereferences so its loops are bounded and its pointers land in the blocks.
  std::function<void(Scene&, PPCContext&, std::mt19937&)> prepare;
};

struct Outcome {
  uint64_t r3 = 0;
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

const Case kCases[] = {
    {0x8213BC38, &crackdown::hooks::decomp::Sub_8213BC38, false, ChunkList},
    {0x82144D80, &crackdown::hooks::decomp::Sub_82144D80, true, PlayerEntity},
    {0x8216F470, &crackdown::hooks::decomp::Sub_8216F470, false, PointerArray},
    {0x821CAA40, &crackdown::hooks::decomp::Sub_821CAA40, true, IndexedTable},
    {0x821E72D0, &crackdown::hooks::decomp::Sub_821E72D0, true, TwoCached},
    {0x821E8E90, &crackdown::hooks::decomp::Sub_821E8E90, true, NameString},
    {0x822B7408, &crackdown::hooks::decomp::Sub_822B7408, true, CharacterState},
    {0x823E9BD0, &crackdown::hooks::decomp::Sub_823E9BD0, false, OrbMapView},
    {0x82794868, &crackdown::hooks::decomp::Sub_82794868, false, nullptr},
    {0x8294AA18, &crackdown::hooks::decomp::Sub_8294AA18, false, ProfilerSlot},
    {0x82981388, &crackdown::hooks::decomp::Sub_82981388, true, EntryArray},
    {0x82A94078, &crackdown::hooks::decomp::Sub_82A94078, true, FloatArray},
    {0x82B47D50, &crackdown::hooks::decomp::Sub_82B47D50, false, nullptr},
};

}  // namespace

int main(int argc, char** argv) {
  int trials = 64;
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
