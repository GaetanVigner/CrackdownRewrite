// Runs the recompiled leaf functions of the title's D3D runtime (no calls, no MMIO; extracted from generated/ at
// configure time, see tests/cmake/guest_sandbox.cmake) in the guest sandbox on seeded random devices and arguments:
// - each run twice from the same state gives the same result (no reads of uninitialized host state);
// - a hash of each function's outputs (registers and changed memory) matches tests/golden/guest_d3d_leaf.txt, so a
//   codegen change (an SDK update, the codegen patches) that alters a function's semantics shows up; set
//   CRACKDOWN_UPDATE_GOLDEN=1 to rewrite the file after checking such a change;
// - the device fields each function writes are classified against the device layout the HLE records use
//   (src/hle_records.cpp), and the fields outside it are listed for the native renderer's device model.
// Run: guest_d3d_leaf_tests [--layout]

#include <algorithm>
#include <atomic>
#include <iterator>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "check.h"
#include "guest_sandbox.h"

extern const guest_sandbox::FunctionEntry kGuestD3dLeafFunctions[];
extern const unsigned kGuestD3dLeafFunctionCount;

namespace {

constexpr uint32_t kDevice = 0x40000000;
constexpr uint32_t kDeviceSize = 0x4000;  // past the last field the records read (bin mask at 12428)
constexpr uint32_t kStack = 0x70000000;
constexpr uint32_t kStackSize = 0x10000;
constexpr int kTrials = 8;
// Functions that walk lists or wait on counters read from the device: on random devices they loop for a very long
// time, so they are not run (the watchdog names any new one).
constexpr uint32_t kSkip[] = {0x826B3238, 0x826B3290, 0x826B3178, 0x826B31D8, 0x826CF588, 0x826C4C70, 0x826B59B0, 0x826B5828};

uint32_t g_default_csr = 0;
std::atomic<uint32_t> g_running{0};
std::atomic<uint64_t> g_progress{0};

void Watchdog() {
  uint64_t last = g_progress.load();
  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(3));
    uint64_t now = g_progress.load();
    if (now == last && g_running.load()) {
      std::printf("guest_d3d_leaf_tests: sub_%08X has run for over 3 s on random input; add it to kSkip\n",
                  g_running.load());
      std::fflush(stdout);
      std::_Exit(3);
    }
    last = now;
  }
}

#if defined(_WIN32)
// A host exception in guest code (an integer division by zero, say, which a PowerPC does not trap) names the function.
LONG CALLBACK FaultReporter(EXCEPTION_POINTERS* info) {
  const DWORD code = info->ExceptionRecord->ExceptionCode;
  if (code == EXCEPTION_ACCESS_VIOLATION || !g_running.load()) return EXCEPTION_CONTINUE_SEARCH;
  std::printf("guest_d3d_leaf_tests: host exception 0x%08lX in sub_%08X\n", code, g_running.load());
  std::fflush(stdout);
  std::_Exit(4);
}
#endif

uint64_t Fnv(uint64_t hash, const void* data, size_t size) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 0x100000001B3ull;
  return hash;
}

struct RunResult {
  uint64_t hash = 0xCBF29CE484222325ull;
  std::vector<uint32_t> device_writes;  // dword offsets in the device that changed
  bool outside = false;                 // changed memory outside the device and the stack
};

RunResult RunOnce(const guest_sandbox::FunctionEntry& entry, uint32_t seed) {
  guest_sandbox::Memory& memory = guest_sandbox::Memory::Get();
  uint8_t* base = memory.base();
  std::mt19937 rng(seed);
  // Device: random words; stack: zero. Everything else reads as zero (committed on first touch).
  std::vector<uint8_t> device_before(kDeviceSize);
  for (uint32_t i = 0; i < kDeviceSize; i += 4) {
    uint32_t value = rng() % 4 ? uint32_t(rng()) : uint32_t(rng() % 16);
    memory.Write32(kDevice + i, value);
  }
  std::memcpy(device_before.data(), base + kDevice, kDeviceSize);
  memory.Fill(kStack, kStackSize, 0);

  // As the runtime does for each guest thread: the host's default FP control word (rounding to nearest, exceptions
  // masked), cached in fpscr.csr (a zero csr would unmask every exception at the first write).
  rex::ppc::FPSCRRegister::Platform::setcsr(g_default_csr);
  PPCContext ctx{};
  ctx.fpscr.InitHost();
  ctx.r1.u64 = kStack + kStackSize - 0x1000;
  ctx.r3.u64 = kDevice;
  for (PPCRegister* reg : {&ctx.r4, &ctx.r5, &ctx.r6, &ctx.r7, &ctx.r8, &ctx.r9, &ctx.r10}) {
    switch (rng() % 4) {
      case 0: reg->u64 = 0; break;
      case 1: reg->u64 = rng() % 2; break;
      case 2: reg->u64 = rng() % 64; break;
      default: reg->u64 = rng(); break;
    }
  }
  for (PPCRegister* reg : {&ctx.f1, &ctx.f2, &ctx.f3, &ctx.f4}) {
    reg->f64 = double(int32_t(rng() % 2001) - 1000) / 64.0;
  }
  ctx.lr = 0x82000000;

  g_running = entry.address;
  entry.function(ctx, base);
  g_running = 0;
  ++g_progress;

  RunResult result;
  for (const PPCRegister* reg : {&ctx.r3, &ctx.r4, &ctx.r5, &ctx.r6, &ctx.r7, &ctx.r8, &ctx.r9, &ctx.r10, &ctx.r11,
                                 &ctx.r12, &ctx.f1, &ctx.f2}) {
    result.hash = Fnv(result.hash, &reg->u64, 8);
  }
  for (uint32_t offset = 0; offset < kDeviceSize; offset += 4) {
    if (std::memcmp(base + kDevice + offset, device_before.data() + offset, 4)) {
      result.device_writes.push_back(offset);
      result.hash = Fnv(result.hash, &offset, 4);
      result.hash = Fnv(result.hash, base + kDevice + offset, 4);
    }
  }
  // Other touched pages started at zero (or are the stack, zero before the call).
  for (uint32_t page : memory.touched()) {
    if (page >= kDevice && page < kDevice + kDeviceSize) continue;
    for (uint32_t offset = 0; offset < guest_sandbox::kPageSize; offset += 4) {
      uint32_t value;
      std::memcpy(&value, base + page + offset, 4);
      if (value) {
        if (page < kStack || page >= kStack + kStackSize) result.outside = true;
        uint32_t address = page + offset;
        result.hash = Fnv(result.hash, &address, 4);
        result.hash = Fnv(result.hash, &value, 4);
      }
    }
  }
  // Every touched page reads zero again for the next run.
  memory.Reset();
  return result;
}

// The device layout the HLE records rely on (src/hle_records.cpp, HLE_RENDERER_PLAN.md "device map").
std::string Classify(uint32_t offset) {
  struct Range {
    uint32_t first, size;
    const char* name;
  };
  static const Range kRanges[] = {
      {0, 8, "dirty VS constants"},  {8, 8, "dirty PS constants"},     {16, 8, "dirty render state"},
      {24, 8, "dirty fetch"},        {32, 8, "dirty 0x2300 registers"}, {48, 4, "ring write pointer"},
      {56, 4, "ring segment end"},   {1024, 32 * 24, "fetch constants"},
      {0x700, 256 * 16, "VS float constants"}, {0x1700, 256 * 16, "PS float constants"},
      {9984, 40 * 4, "bool/loop constants"},
      {10240, 16 * 4, "registers 0x2000"},  {10304, 3 * 4, "registers 0x2080"},  {10316, 21 * 4, "registers 0x2100"},
      {10400, 5 * 4, "registers 0x2180"},   {10420, 12 * 4, "registers 0x2200"}, {10468, 21 * 4, "registers 0x2280"},
      {10552, 38 * 4, "registers 0x2300"},  {10704, 8 * 4, "registers 0x2380"},
      {12412, 4, "pixel shader"}, {12416, 4, "vertex shader"}, {12428, 8, "bin mask"},
  };
  for (const Range& range : kRanges) {
    if (offset >= range.first && offset < range.first + range.size) return range.name;
  }
  return "";
}

std::map<uint32_t, uint64_t> ReadGolden(bool& present) {
  std::map<uint32_t, uint64_t> golden;
  std::ifstream in(CRACKDOWN_GOLDEN);
  present = bool(in);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream fields(line);
    std::string address, hash;
    fields >> address >> hash;
    golden[uint32_t(std::stoul(address, nullptr, 16))] = std::stoull(hash, nullptr, 16);
  }
  return golden;
}

}  // namespace

int main(int argc, char** argv) {
  const bool print_layout = argc > 1 && std::string(argv[1]) == "--layout";
  g_default_csr = rex::ppc::FPSCRRegister::Platform::getcsr();
  std::thread(Watchdog).detach();
#if defined(_WIN32)
  guest_sandbox::Memory::Get();  // its handler first: commit-on-touch faults are not errors
  AddVectoredExceptionHandler(0, FaultReporter);
#endif
  std::printf("guest_d3d_leaf_tests: %u leaf functions\n", kGuestD3dLeafFunctionCount);

  std::map<uint32_t, uint64_t> hashes;
  std::map<std::string, int> field_writers;
  std::map<uint32_t, int> unknown_fields;  // device offset -> functions writing it
  int outside = 0;
  for (unsigned i = 0; i < kGuestD3dLeafFunctionCount; ++i) {
    const guest_sandbox::FunctionEntry& entry = kGuestD3dLeafFunctions[i];
    if (std::find(std::begin(kSkip), std::end(kSkip), entry.address) != std::end(kSkip)) continue;
    const auto function_start = std::chrono::steady_clock::now();
    uint64_t combined = 0xCBF29CE484222325ull;
    std::map<std::string, bool> fields;
    bool writes_outside = false;
    for (int trial = 0; trial < kTrials; ++trial) {
      const uint32_t seed = entry.address ^ uint32_t(trial * 0x9E3779B9u);
      RunResult first = RunOnce(entry, seed);
      RunResult second = RunOnce(entry, seed);
      if (first.hash != second.hash) {
        std::printf("  sub_%08X: two runs from the same state differ\n", entry.address);
        CHECK(first.hash == second.hash);
      }
      combined = Fnv(combined, &first.hash, 8);
      writes_outside |= first.outside;
      for (uint32_t offset : first.device_writes) {
        std::string name = Classify(offset);
        if (name.empty()) {
          ++unknown_fields[offset];
          name = "unknown";
        }
        fields[name] = true;
      }
    }
    hashes[entry.address] = combined;
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - function_start).count();
    if (seconds > 0.5) std::printf("  sub_%08X took %.1f s\n", entry.address, seconds);
    outside += writes_outside;
    for (const auto& [name, _] : fields) ++field_writers[name];
    if (print_layout && !fields.empty()) {
      std::printf("  sub_%08X writes:", entry.address);
      for (const auto& [name, _] : fields) std::printf(" [%s]", name.c_str());
      std::printf("%s\n", writes_outside ? " [memory outside the device]" : "");
    }
  }

  std::printf("Device fields written (functions): ");
  for (const auto& [name, count] : field_writers) std::printf("%s %d; ", name.c_str(), count);
  std::printf("\nFunctions writing memory outside the device and stack: %d\n", outside);
  std::printf("Device dwords outside the records' layout: %zu (--layout lists them)\n", unknown_fields.size());
  if (print_layout && !unknown_fields.empty()) {
    std::printf("Device dwords outside the records' layout (offset: functions):");
    for (const auto& [offset, count] : unknown_fields) std::printf(" %u:%d", offset, count);
    std::printf("\n");
  }

  bool golden_present = false;
  std::map<uint32_t, uint64_t> golden = ReadGolden(golden_present);
  if (std::getenv("CRACKDOWN_UPDATE_GOLDEN")) {
    std::ofstream out(CRACKDOWN_GOLDEN, std::ios::trunc);
    out << "# guest_d3d_leaf_tests: hash of each D3D runtime leaf function's outputs on " << kTrials
        << " seeded random inputs (address, hash). Rewritten with CRACKDOWN_UPDATE_GOLDEN=1.\n";
    for (const auto& [address, hash] : hashes) {
      char line[64];
      std::snprintf(line, sizeof(line), "%08X %016llX\n", address, static_cast<unsigned long long>(hash));
      out << line;
    }
    std::printf("Golden hashes written to %s\n", CRACKDOWN_GOLDEN);
  } else if (!golden_present) {
    std::printf("No golden hashes yet (%s); run with CRACKDOWN_UPDATE_GOLDEN=1 to create them\n", CRACKDOWN_GOLDEN);
  } else {
    int changed = 0, missing = 0;
    for (const auto& [address, hash] : hashes) {
      auto it = golden.find(address);
      if (it == golden.end()) {
        ++missing;
      } else if (it->second != hash) {
        std::printf("  sub_%08X: outputs differ from the golden hash\n", address);
        ++changed;
      }
    }
    if (missing) std::printf("%d function(s) not in the golden file (new in this codegen)\n", missing);
    CHECK(changed == 0);
  }
  return check::Finish("guest_d3d_leaf_tests");
}
