// HLE renderer H1a, game side: see hle_records.h. Device layout from the draw functions' flush (sub_826B9C08): five
// 64-bit dirty masks at +0 (VS float constants, 4 float4 a bit from the most significant), +8 (PS float constants),
// +16 (shaders, render state 0x2000/0x2100/0x2180/0x2200), +24 (32 fetch slots, render state 0x2280) and +32 (render
// state 0x2300/0x2380, bool/loop constants), each cleared once its registers are written to the ring.

#include "hle_records.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstring>
#include <vector>

#include <intrin.h>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "guest_memory.h"

REXCVAR_DEFINE_BOOL(crackdown_hle_records, false, "Crackdown",
                    "Developer diagnostics (HLE renderer H1a): build a draw record from the D3D device state at every "
                    "flushing call and log their counters every 10 s; observe only")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace crackdown {
namespace {

using guest::Load32;

constexpr uint32_t kDirtyVsConstants = 0;
constexpr uint32_t kDirtyPsConstants = 8;
constexpr uint32_t kVsConstants = 0x700;   // float4 c0-c255, big-endian dwords
constexpr uint32_t kPsConstants = 0x1700;  // float4 c256-c511
constexpr uint32_t kFetch = 1024;          // 32 slots of 6 dwords
constexpr uint32_t kBoolLoop = 9984;       // 0x4900-0x4927
constexpr uint32_t kPixelShader = 12412;
constexpr uint32_t kVertexShader = 12416;
constexpr uint32_t kFetchDwords = 192;
constexpr uint32_t kBoolLoopDwords = 40;
constexpr uint32_t kPageDwords = 64;  // 16 float4
// Block arena capacity: a frame appends ~2 MB in the city, so it is compacted every frame or two without reallocating.
constexpr size_t kArenaDwords = size_t(4) << 20;

// The render-state shadow groups (register range -> device offset), as the flush writes them.
struct Group {
  uint32_t first_register;
  uint32_t count;
  uint32_t device_offset;
};
constexpr Group kRenderStateGroups[] = {
    {0x2000, 16, 10240}, {0x2080, 3, 10304},  {0x2100, 21, 10316}, {0x2180, 5, 10400},
    {0x2200, 12, 10420}, {0x2280, 21, 10468}, {0x2300, 38, 10552}, {0x2380, 8, 10704},
};
constexpr uint32_t RenderStateDwords() {
  uint32_t total = 0;
  for (const Group& group : kRenderStateGroups) total += group.count;
  return total;
}
constexpr uint32_t kRenderStateDwords = RenderStateDwords();

// Block layout of the GDTR trace (SDK patch 0108): render state 0x2000 (0x400), floats 0x4000 (2048), fetch 0x4800
// (192), bool/loop 0x4900 (40).
constexpr uint32_t kTraceFloats = 0x400;
constexpr uint32_t kTraceFetch = kTraceFloats + 2048;
constexpr uint32_t kTraceBoolLoop = kTraceFetch + 192;

struct Counters {
  uint64_t records = 0;
  uint64_t by_kind[9] = {};
  uint64_t render_state_blocks = 0;
  uint64_t fetch_blocks = 0;
  uint64_t bool_loop_blocks = 0;
  uint64_t constant_pages = 0;
  uint64_t block_bytes = 0;
  uint64_t cycles = 0;
  uint64_t frames = 0;
};

struct State {
  // Guards the arena against the frame reset; one guest thread issues the D3D calls, so it is uncontended.
  std::atomic_flag lock = ATOMIC_FLAG_INIT;
  std::vector<uint32_t> arena;
  // The blocks the device holds now (offsets in the arena), shared by records until the game changes them.
  uint32_t render_state = UINT32_MAX;
  uint32_t fetch = UINT32_MAX;
  uint32_t bool_loop = UINT32_MAX;
  uint32_t vs_pages[DrawRecord::kConstantPages];
  uint32_t ps_pages[DrawRecord::kConstantPages];
  bool pages_valid = false;
  Counters counters;
  std::chrono::steady_clock::time_point window_start = std::chrono::steady_clock::now();
  uint64_t window_tsc = __rdtsc();
};
State g_state;
thread_local DrawRecord t_record;

class SpinLock {
 public:
  explicit SpinLock(std::atomic_flag& flag) : flag_(flag) {
    while (flag_.test_and_set(std::memory_order_acquire)) _mm_pause();
  }
  ~SpinLock() { flag_.clear(std::memory_order_release); }

 private:
  std::atomic_flag& flag_;
};

uint32_t Append(std::vector<uint32_t>& arena, const uint8_t* source, uint32_t dwords) {
  const uint32_t offset = uint32_t(arena.size());
  const uint32_t* first = reinterpret_cast<const uint32_t*>(source);
  arena.insert(arena.end(), first, first + dwords);
  return offset;
}

// A new block when the device's copy differs from the current one; the comparison is what makes the result exact
// even if a flush the hooks do not see cleared a dirty bit.
uint32_t Refresh(State& state, uint32_t current, const uint8_t* source, uint32_t dwords, uint64_t& created) {
  if (current != UINT32_MAX &&
      std::memcmp(state.arena.data() + current, source, dwords * sizeof(uint32_t)) == 0) {
    return current;
  }
  ++created;
  state.counters.block_bytes += dwords * sizeof(uint32_t);
  return Append(state.arena, source, dwords);
}

// Float constant pages: only the ones whose dirty bits are set are compared (a page is four mask bits).
void RefreshPages(State& state, uint32_t* pages, const uint8_t* source, uint64_t dirty, bool all) {
  for (uint32_t page = 0; page < DrawRecord::kConstantPages; ++page) {
    const uint64_t bits = (dirty << (4 * page)) >> 60;
    if (!all && !bits) continue;
    pages[page] = Refresh(state, all ? UINT32_MAX : pages[page], source + page * kPageDwords * 4, kPageDwords,
                          state.counters.constant_pages);
  }
}

void LogWindow(State& state) {
  const auto now = std::chrono::steady_clock::now();
  const double seconds = std::chrono::duration<double>(now - state.window_start).count();
  if (seconds < 10.0) return;
  const uint64_t tsc = __rdtsc();
  const Counters& c = state.counters;
  if (c.frames && c.records) {
    const double ns_per_cycle = seconds * 1e9 / double(tsc - state.window_tsc);
    const double frames = double(c.frames);
    REXLOG_INFO(
        "[HLE records] per frame: {:.0f} records (draw {:.0f}, indexed {:.0f}, begin {:.0f}, cb {:.0f}, clear "
        "{:.0f}, resolve {:.0f}, gpr {:.0f}); new blocks: render state {:.0f}, fetch {:.0f}, bool/loop {:.0f}, "
        "constant pages {:.0f}, {:.1f} KB; {:.0f} ns a record",
        double(c.records) / frames, double(c.by_kind[1]) / frames, double(c.by_kind[2]) / frames,
        double(c.by_kind[3] + c.by_kind[4]) / frames, double(c.by_kind[5]) / frames, double(c.by_kind[6]) / frames,
        double(c.by_kind[7]) / frames, double(c.by_kind[8]) / frames, double(c.render_state_blocks) / frames,
        double(c.fetch_blocks) / frames, double(c.bool_loop_blocks) / frames, double(c.constant_pages) / frames,
        double(c.block_bytes) / frames / 1024.0, double(c.cycles) * ns_per_cycle / double(c.records));
  }
  state.counters = {};
  state.window_start = now;
  state.window_tsc = tsc;
}

}  // namespace

bool HleRecordsEnabled() {
  return REXCVAR_GET(crackdown_hle_records);
}

HleRecordMasks HleReadRecordMasks(uint8_t* base, uint32_t device) {
  HleRecordMasks masks;
  if (HleRecordsEnabled()) {
    std::memcpy(&masks.vs, base + device + kDirtyVsConstants, 8);
    std::memcpy(&masks.ps, base + device + kDirtyPsConstants, 8);
    masks.vs = std::byteswap(masks.vs);
    masks.ps = std::byteswap(masks.ps);
  }
  return masks;
}

const DrawRecord* HleRecordCall(uint8_t* base, uint32_t device, RecordKind kind, const uint32_t args[4],
                                const HleRecordMasks& masks) {
  if (!HleRecordsEnabled()) {
    return nullptr;
  }
  const uint64_t start = __rdtsc();
  State& state = g_state;
  SpinLock lock(state.lock);
  const uint8_t* dev = base + device;
  DrawRecord& record = t_record;
  record.kind = kind;
  std::copy(args, args + 4, record.args);
  record.vertex_shader = Load32(base, device + kVertexShader);
  record.pixel_shader = Load32(base, device + kPixelShader);

  // The render-state groups are not contiguous in the device: gather them, then share or append.
  uint8_t gathered[kRenderStateDwords * 4];
  uint32_t position = 0;
  for (const Group& group : kRenderStateGroups) {
    std::memcpy(gathered + position, dev + group.device_offset, group.count * 4);
    position += group.count * 4;
  }
  state.render_state =
      Refresh(state, state.render_state, gathered, kRenderStateDwords, state.counters.render_state_blocks);
  state.fetch = Refresh(state, state.fetch, dev + kFetch, kFetchDwords, state.counters.fetch_blocks);
  state.bool_loop = Refresh(state, state.bool_loop, dev + kBoolLoop, kBoolLoopDwords, state.counters.bool_loop_blocks);
  const bool all = !state.pages_valid;
  RefreshPages(state, state.vs_pages, dev + kVsConstants, masks.vs, all);
  RefreshPages(state, state.ps_pages, dev + kPsConstants, masks.ps, all);
  state.pages_valid = true;

  record.render_state = state.render_state;
  record.fetch = state.fetch;
  record.bool_loop = state.bool_loop;
  std::copy(std::begin(state.vs_pages), std::end(state.vs_pages), record.vs_pages);
  std::copy(std::begin(state.ps_pages), std::end(state.ps_pages), record.ps_pages);

  ++state.counters.records;
  ++state.counters.by_kind[uint32_t(kind)];
  state.counters.cycles += __rdtsc() - start;
  return &record;
}

void HleExpandRecord(const DrawRecord& record, uint32_t* values) {
  State& state = g_state;
  SpinLock lock(state.lock);
  auto host = [](uint32_t big_endian) { return std::byteswap(big_endian); };
  const uint32_t* arena = state.arena.data();
  uint32_t position = record.render_state;
  for (const Group& group : kRenderStateGroups) {
    for (uint32_t i = 0; i < group.count; ++i) values[group.first_register - 0x2000 + i] = host(arena[position + i]);
    position += group.count;
  }
  for (uint32_t i = 0; i < kFetchDwords; ++i) values[kTraceFetch + i] = host(arena[record.fetch + i]);
  for (uint32_t i = 0; i < kBoolLoopDwords; ++i) values[kTraceBoolLoop + i] = host(arena[record.bool_loop + i]);
  for (uint32_t page = 0; page < DrawRecord::kConstantPages; ++page) {
    for (uint32_t i = 0; i < kPageDwords; ++i) {
      values[kTraceFloats + page * kPageDwords + i] = host(arena[record.vs_pages[page] + i]);
      values[kTraceFloats + 1024 + page * kPageDwords + i] = host(arena[record.ps_pages[page] + i]);
    }
  }
}

void HleRecordsFrame() {
  State& state = g_state;
  {
    SpinLock lock(state.lock);
    ++state.counters.frames;
    // Keep only the blocks the device holds now, so the arena does not grow across frames.
    if (state.arena.capacity() < kArenaDwords) state.arena.reserve(kArenaDwords);
    if (state.render_state != UINT32_MAX && state.arena.size() > kArenaDwords / 2) {
      std::vector<uint32_t> kept;
      kept.reserve(kArenaDwords);
      auto keep = [&](uint32_t& offset, uint32_t dwords) {
        const uint32_t moved = uint32_t(kept.size());
        kept.insert(kept.end(), state.arena.begin() + offset, state.arena.begin() + offset + dwords);
        offset = moved;
      };
      keep(state.render_state, kRenderStateDwords);
      keep(state.fetch, kFetchDwords);
      keep(state.bool_loop, kBoolLoopDwords);
      for (uint32_t& page : state.vs_pages) keep(page, kPageDwords);
      for (uint32_t& page : state.ps_pages) keep(page, kPageDwords);
      state.arena.swap(kept);
    }
  }
  if (HleRecordsEnabled()) {
    LogWindow(state);
  }
}

}  // namespace crackdown
