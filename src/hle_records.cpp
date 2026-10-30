// HLE renderer H1a, game side: see hle_records.h. Device layout from the draw functions' flush (sub_826B9C08): five
// 64-bit dirty masks at +0 (VS float constants, 4 float4 a bit from the most significant), +8 (PS float constants),
// +16 (shaders, render state 0x2000/0x2100/0x2180/0x2200), +24 (32 fetch slots, render state 0x2280) and +32 (render
// state 0x2300/0x2380, bool/loop constants), each cleared once its registers are written to the ring.

#include "hle_records.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/logging.h>

#include "guest_memory.h"
#include "hle_frame_abi.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

REXCVAR_DEFINE_BOOL(crackdown_hle_records, false, "Crackdown",
                    "Developer diagnostics (HLE renderer H1a): build a draw record from the D3D device state at every "
                    "flushing call and log their counters every 10 s; observe only")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(crackdown_hle_markers, false, "Crackdown",
                    "Developer diagnostics (HLE renderer H2): write a marker packet to the GPU command stream where "
                    "each tagged render pass begins and ends")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(crackdown_hle_plugin_channel, false, "Crackdown",
                    "Developer diagnostics (HLE renderer H1b): with crackdown_hle_records on, hand each frame's draw "
                    "records and state blocks to the GPU plugin (rex_gpu_hle_submit_frame, SDK patch 0129), which "
                    "logs what it receives under gpu_frame_stats")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace crackdown {
namespace {

using guest::Load32;

constexpr uint32_t kDirtyVsConstants = 0;
constexpr uint32_t kDirtyPsConstants = 8;
constexpr uint32_t kDirtyFetch = 24;  // low 32 bits: fetch slots, slot 0 the most significant
constexpr uint32_t kDirtyState = 32;  // low 38 bits: registers 0x2300-0x2325, 0x2300 the most significant
constexpr uint32_t kVsConstants = 0x700;   // float4 c0-c255, big-endian dwords
constexpr uint32_t kPsConstants = 0x1700;  // float4 c256-c511
constexpr uint32_t kFetch = 1024;          // 32 slots of 6 dwords
constexpr uint32_t kBoolLoop = 9984;       // 0x4900-0x4927
constexpr uint32_t kPixelShader = 12412;
constexpr uint32_t kVertexShader = 12416;
constexpr uint32_t kFetchDwords = 192;
constexpr uint32_t kBoolLoopDwords = 40;

// The last draw packet the call wrote: the device's ring pointer (device+48, the last dword written: the runtime
// writes with stwu) is walked back over at most 64 dwords to the latest type-3 DRAW_INDX (0x22: viz query, initiator,
// [DMA base, size]) or DRAW_INDX_2 (0x36: initiator, immediate data) header whose packet ends at or before it and
// whose size matches its initiator's index source.
void LastDrawPacket(uint8_t* base, uint32_t device, uint32_t out[3]) {
  out[0] = out[1] = out[2] = 0;
  const uint32_t last = Load32(base, device + 48);
  if (last < 0x40000000 || (last & 3)) return;
  for (uint32_t back = 0; back < 64; ++back) {
    const uint32_t at = last - back * 4;
    const uint32_t header = Load32(base, at);
    if ((header >> 30) != 3) continue;
    const uint32_t opcode = (header >> 8) & 0x7F;
    const uint32_t body = ((header >> 16) & 0x3FFF) + 1;
    if (body > back) continue;  // the packet would run past the last dword written
    if (opcode == 0x22) {
      const uint32_t initiator = Load32(base, at + 8);
      const bool dma = ((initiator >> 6) & 3) == 0;
      if (body != (dma ? 4u : 2u)) continue;
      out[0] = initiator;
      if (dma) {
        out[1] = Load32(base, at + 12);
        out[2] = Load32(base, at + 16);
      }
      return;
    }
    if (opcode == 0x36) {
      const uint32_t initiator = Load32(base, at + 4);
      if (((initiator >> 6) & 3) == 0) continue;
      out[0] = initiator;
      return;
    }
  }
}

// A shader object's microcode as sub_826C6C48 loads it (IM_LOAD): guest physical address and dwords. Pixel shader:
// blob = obj + [obj+64], code at [obj+24] + [blob+40], [blob+44] bytes. Vertex shader: variant v (1 when bit 0x20 of
// [obj+872] is set), blob = obj + [obj+896+8v], code at [obj+32] + [blob+872], [blob+876] bytes; the runtime patches
// that code in place for the vertex declaration before loading it.
void ShaderMicrocode(uint8_t* base, uint32_t object, bool vertex, uint32_t out[2]) {
  out[0] = out[1] = 0;
  if (object < 0x40000000) return;
  uint32_t code, bytes;
  if (vertex) {
    const uint32_t variant = (Load32(base, object + 872) & 0x20) ? 1 : 0;
    const uint32_t blob = object + Load32(base, object + 896 + 8 * variant);
    code = Load32(base, object + 32) + Load32(base, blob + 872);
    bytes = Load32(base, blob + 876);
  } else {
    const uint32_t blob = object + Load32(base, object + 64);
    code = Load32(base, object + 24) + Load32(base, blob + 40);
    bytes = Load32(base, blob + 44);
  }
  out[0] = (code & 0x1FFFFFFF) + ((((code >> 20) + 0x200) & 0x1000));
  out[1] = bytes >> 2;
}
constexpr uint32_t kPageDwords = 64;  // 16 float4
// The overlay's index space: float constants (VS c0-c255 then PS, in dwords), the fetch constants, then the resolve
// registers 0x2318-0x2327 (the device zeroes its copy after a resolve; the GPU keeps the values).
constexpr uint32_t kOverlayFetch = 2048;
constexpr uint32_t kOverlayRegisters = kOverlayFetch + kFetchDwords;
constexpr uint32_t kOverlayFirstRegister = 0x2318;
constexpr uint32_t kOverlayRegisterCount = 16;
constexpr uint32_t kOverlayDwords = kOverlayRegisters + kOverlayRegisterCount;
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
  uint64_t by_pass[kRenderPassCount + 1] = {};
  uint64_t by_kind[10] = {};
  uint64_t render_state_blocks = 0;
  uint64_t fetch_blocks = 0;
  uint64_t bool_loop_blocks = 0;
  uint64_t constant_pages = 0;
  uint64_t overlay_writes = 0;
  uint64_t segments = 0;
  uint64_t new_segments = 0;
  uint64_t decoded_hits = 0;
  uint64_t decoded_misses = 0;
  uint64_t predicated_run = 0;
  uint64_t predicated_skipped = 0;
  uint64_t last_bin_mask = 0;
  uint64_t last_bin_select = 0;
  uint64_t block_bytes = 0;
  uint64_t cycles = 0;
  // Cycles by step: overlay (writes queued by the walks), render state, fetch, float constant pages.
  uint64_t step_cycles[4] = {};
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
  // Float and fetch constants the GPU holds from writes that are not the device's flush, since a flush last wrote
  // those registers: shader literal loads (sub_826C6A50), PM4 written between hooked calls (the engine's prebaked
  // binds), BeginVertices' vertex fetch. Big-endian like the device. Writes wait in `pending_before` (written before
  // the call) or `pending_after` (during it, after its flush) until the call's record applies them.
  bool overlay[kOverlayDwords] = {};
  // Which fetch slots and resolve registers have overlay values, and whether the fetch overlay changed since the
  // fetch block was last built: the per-record merges only look where the overlay holds something.
  uint32_t fetch_overlay_slots = 0;
  uint32_t page_overlay = 0;  // float pages that may hold overlay values (bit p: VS page p, 16 + p: PS page p)
  uint32_t register_overlay = 0;
  bool fetch_changed = true;
  uint32_t overlay_values[kOverlayDwords];
  // Command buffer walk (HleBufferBegin..HleBufferEnd): the render state image (big-endian, 0x2000-0x23FF), its
  // shaders' microcode, the device, and the float pages the buffer's constant writes touched since its last draw.
  // PA_SC_WINDOW_OFFSET and the window scissor (0x2080-0x2082) as PM4 outside the device's flush last wrote them
  // (big-endian), and the device's own values when that value was first used: the GPU keeps the written value until
  // the device's copy changes (then its flush rewrites it).
  bool window_written[3] = {};
  bool window_fresh[3] = {};
  uint32_t window_values[3] = {};
  uint32_t window_device[3] = {};
  bool buffer_active = false;
  uint32_t buffer_registers[0x400];
  uint32_t buffer_ucode[4];
  uint32_t buffer_bool_loop[kBoolLoopDwords];
  bool buffer_bool_loop_changed = false;
  const uint8_t* buffer_dev = nullptr;
  uint32_t buffer_touched = 0;
  std::vector<uint32_t> pending_before;  // {first, count, values...} in the overlay's index space
  std::vector<uint32_t> pending_after;
  // Command buffer segments seen so far ((address << 20) | dwords): how many a decoded-segment cache would hold.
  std::unordered_set<uint64_t> segments_seen;
  std::unordered_map<uint32_t, uint64_t> untagged_callers;  // diagnostics, per log window
  Counters counters;
  std::chrono::steady_clock::time_point window_start = std::chrono::steady_clock::now();
  uint64_t window_tsc = __rdtsc();
  // crackdown_hle_plugin_channel: the frame's finished records, handed to the plugin at the frame's end.
  std::vector<DrawRecord> frame_records;
  uint64_t frame_number = 0;
};
State g_state;
thread_local DrawRecord t_record;
// Whether t_record holds a record not yet added to the frame (records are completed by the notes that follow their
// call: BeginVertices' vertices, command buffer segments), so it is added when the next one starts or the frame ends.
thread_local bool t_record_pending = false;

bool PluginChannelEnabled() { return REXCVAR_GET(crackdown_hle_plugin_channel); }

using SubmitFrame = int (*)(const HleFrameAbi*);

// The GPU plugin's rex_gpu_hle_submit_frame (any build configuration's file name), looked up once.
SubmitFrame FindSubmitFrame() {
  static SubmitFrame submit = [] {
    for (const char* postfix : {"", "rd", "d"}) {
#if defined(_WIN32)
      std::string name = std::string("rexgpu-xenos") + postfix + ".dll";
      if (HMODULE module = GetModuleHandleA(name.c_str())) {
        return reinterpret_cast<SubmitFrame>(GetProcAddress(module, "rex_gpu_hle_submit_frame"));
      }
#else
      std::string name = std::string("librexgpu-xenos") + postfix + ".so";
      if (void* module = dlopen(name.c_str(), RTLD_NOW | RTLD_NOLOAD)) {
        return reinterpret_cast<SubmitFrame>(dlsym(module, "rex_gpu_hle_submit_frame"));
      }
#endif
    }
    return SubmitFrame(nullptr);
  }();
  return submit;
}
thread_local uint32_t t_pass = 0;
constexpr const char* kRenderPassNames[kRenderPassCount + 1] = {"other", "early target", "shadow map", "shadow helper a", "shadow helper b", "shadow atlas", "opaque scene", "depth resolve", "normal resolve", "half-res", "screen space", "colour a", "colour b", "query effects a", "query effects b", "light apply", "light pools", "particles", "late draw", "post-processing", "late a", "late b", "late c", "late d", "late e", "late f", "objects a", "objects b", "objects c", "hud"};

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

// Applies queued writes to the overlay. Returns the float pages they touched (bit p: VS page p, bit 16 + p: PS page
// p).
uint32_t ApplyPending(State& state, std::vector<uint32_t>& pending) {
  uint32_t touched = 0;
  for (size_t i = 0; i + 2 <= pending.size();) {
    const uint32_t first = pending[i];
    const uint32_t count = pending[i + 1];
    for (uint32_t k = 0; k < count && first + k < kOverlayDwords; ++k) {
      const uint32_t index = first + k;
      state.overlay[index] = true;
      state.overlay_values[index] = pending[i + 2 + k];
      if (index < kOverlayFetch) {
        touched |= 1u << (index / kPageDwords);
        state.page_overlay |= 1u << (index / kPageDwords);
      } else if (index < kOverlayRegisters) {
        state.fetch_overlay_slots |= 1u << ((index - kOverlayFetch) / 6);
        state.fetch_changed = true;
      } else {
        state.register_overlay |= 1u << (index - kOverlayRegisters);
      }
    }
    i += 2 + count;
    ++state.counters.overlay_writes;
  }
  pending.clear();
  return touched;
}

// The overlay around a call's flush: writes made before it, then the flush replaces the dirty float constants (4
// float4 a mask bit, most significant first) and fetch slots (one bit each) with the device's values, then the writes
// made during the call.
uint32_t ApplyOverlay(State& state, const HleRecordMasks& masks) {
  uint32_t touched = ApplyPending(state, state.pending_before);
  for (uint32_t stage = 0; stage < 2; ++stage) {
    uint64_t dirty = stage ? masks.ps : masks.vs;
    while (dirty) {
      const uint32_t bit = uint32_t(std::countl_zero(dirty));
      dirty &= ~(uint64_t(1) << (63 - bit));
      std::fill_n(state.overlay + stage * 1024 + bit * 16, 16, false);
    }
  }
  // Fetch slots flushed from the device (slot 0 the most significant mask bit).
  uint32_t fetch = 0;
  for (uint32_t slots = state.fetch_overlay_slots; slots;) {
    const uint32_t slot = uint32_t(std::countr_zero(slots));
    slots &= slots - 1;
    if ((masks.fetch >> (31 - slot)) & 1) fetch |= 1u << slot;
  }
  for (uint32_t slots = fetch; slots;) {
    const uint32_t slot = uint32_t(std::countr_zero(slots));
    slots &= slots - 1;
    std::fill_n(state.overlay + kOverlayFetch + slot * 6, 6, false);
    state.fetch_overlay_slots &= ~(1u << slot);
    state.fetch_changed = true;
  }
  // The resolve registers the flush rewrites from the device (which zeroes them after a resolve).
  for (uint32_t registers = state.register_overlay; registers;) {
    const uint32_t index = uint32_t(std::countr_zero(registers));
    registers &= registers - 1;
    const uint32_t k = kOverlayFirstRegister + index - 0x2300;
    if (k < 38 && ((masks.state >> (37 - k)) & 1)) {
      state.overlay[kOverlayRegisters + index] = false;
      state.register_overlay &= ~(1u << index);
    }
  }
  touched |= ApplyPending(state, state.pending_after);
  return touched;
}

// The fetch constants the GPU holds: the device's with the overlay over them.
// `dirty`: the fetch slot mask of the call (0 and an unchanged overlay keep the current block).
void RefreshFetch(State& state, const uint8_t* dev, uint32_t dirty) {
  if (!dirty && !state.fetch_changed && state.fetch != UINT32_MAX) {
    return;
  }
  state.fetch_changed = false;
  uint32_t values[kFetchDwords];
  std::memcpy(values, dev + kFetch, sizeof(values));
  for (uint32_t slots = state.fetch_overlay_slots; slots;) {
    const uint32_t slot = uint32_t(std::countr_zero(slots));
    slots &= slots - 1;
    for (uint32_t k = slot * 6; k < slot * 6 + 6; ++k) {
      if (state.overlay[kOverlayFetch + k]) values[k] = state.overlay_values[kOverlayFetch + k];
    }
  }
  state.fetch = Refresh(state, state.fetch, reinterpret_cast<const uint8_t*>(values), kFetchDwords,
                        state.counters.fetch_blocks);
}

void Queue(std::vector<uint32_t>& pending, uint32_t first, uint32_t dwords, const uint8_t* values) {
  pending.push_back(first);
  pending.push_back(dwords);
  const size_t at = pending.size();
  pending.resize(at + dwords);
  std::memcpy(pending.data() + at, values, dwords * 4);
}

// Float constant pages: the device's values with the GPU's literal values over them. Only pages whose dirty bits are
// set or that literal loads touched are compared (a page is four mask bits).
void RefreshPages(State& state, uint32_t stage, uint32_t* pages, const uint8_t* source, uint64_t dirty,
                  uint32_t touched, bool all) {
  for (uint32_t page = 0; page < DrawRecord::kConstantPages; ++page) {
    const uint64_t bits = (dirty << (4 * page)) >> 60;
    if (!all && !bits && !(touched & (1u << (stage * 16 + page)))) continue;
    const uint32_t current = all ? UINT32_MAX : pages[page];
    const uint32_t first = stage * 1024 + page * kPageDwords;
    if (!(state.page_overlay & (1u << (stage * 16 + page)))) {
      pages[page] = Refresh(state, current, source + page * kPageDwords * 4, kPageDwords, state.counters.constant_pages);
      continue;
    }
    uint32_t values[kPageDwords];
    std::memcpy(values, source + page * kPageDwords * 4, sizeof(values));
    bool any = false;
    for (uint32_t k = 0; k < kPageDwords; ++k) {
      if (state.overlay[first + k]) {
        values[k] = state.overlay_values[first + k];
        any = true;
      }
    }
    if (!any) state.page_overlay &= ~(1u << (stage * 16 + page));
    pages[page] = Refresh(state, current, reinterpret_cast<const uint8_t*>(values), kPageDwords,
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
        "constant pages {:.0f}, {:.1f} KB; overlay writes {:.0f}; command buffer segments {:.0f} ({:.1f} new, {} known); {:.0f} ns a record",
        double(c.records) / frames, double(c.by_kind[1]) / frames, double(c.by_kind[2]) / frames,
        double(c.by_kind[3] + c.by_kind[4]) / frames, double(c.by_kind[5]) / frames, double(c.by_kind[6]) / frames,
        double(c.by_kind[7]) / frames, double(c.by_kind[8]) / frames, double(c.render_state_blocks) / frames,
        double(c.fetch_blocks) / frames, double(c.bool_loop_blocks) / frames, double(c.constant_pages) / frames,
        double(c.block_bytes) / frames / 1024.0, double(c.overlay_writes) / frames, double(c.segments) / frames,
        double(c.new_segments) / frames, state.segments_seen.size(),
        double(c.cycles) * ns_per_cycle / double(c.records));
    REXLOG_INFO("[HLE records] predicated packets per frame: {:.0f} run, {:.0f} skipped; last bin mask {:016X} select {:016X}; "
                "decoded buffers per frame: {:.0f} cached, {:.1f} decoded; ns a record: overlay {:.0f}, render state "
                "{:.0f}, fetch {:.0f}, constant pages {:.0f}",
                double(c.predicated_run) / frames, double(c.predicated_skipped) / frames, c.last_bin_mask,
                c.last_bin_select, double(c.decoded_hits) / frames, double(c.decoded_misses) / frames,
                double(c.step_cycles[0]) * ns_per_cycle / double(c.records),
                double(c.step_cycles[1]) * ns_per_cycle / double(c.records),
                double(c.step_cycles[2]) * ns_per_cycle / double(c.records),
                double(c.step_cycles[3]) * ns_per_cycle / double(c.records));
  }
  if (c.frames && c.records) {
    std::string passes;
    for (uint32_t pass = 0; pass <= kRenderPassCount; ++pass) {
      if (c.by_pass[pass]) passes += fmt::format(" {} {:.0f},", RenderPassName(pass), double(c.by_pass[pass]) / c.frames);
    }
    REXLOG_INFO("[HLE records] records per frame by pass:{}", passes);
    std::vector<std::pair<uint64_t, uint32_t>> callers;
    for (const auto& [caller, count] : state.untagged_callers) callers.push_back({count, caller});
    std::sort(callers.rbegin(), callers.rend());
    std::string top;
    for (size_t i = 0; i < callers.size() && i < 16; ++i) {
      top += fmt::format(" {:08X} {:.0f},", callers[i].second, double(callers[i].first) / c.frames);
    }
    REXLOG_INFO("[HLE records] untagged records per frame by caller:{}", top);
  }
  state.counters = {};
  state.untagged_callers.clear();
  state.window_start = now;
  state.window_tsc = tsc;
}

}  // namespace

const char* RenderPassName(uint32_t pass) {
  return pass <= kRenderPassCount ? kRenderPassNames[pass] : "?";
}

uint32_t HleEnterRenderPass(uint32_t pass) {
  const uint32_t previous = t_pass;
  t_pass = pass;
  return previous;
}

bool HleMarkersEnabled() { return REXCVAR_GET(crackdown_hle_markers); }

uint64_t HleRecordsFrameNumber() {
  State& state = g_state;
  SpinLock lock(state.lock);
  return state.frame_number;
}

uint32_t HleRecordsInFrame() {
  State& state = g_state;
  SpinLock lock(state.lock);
  return uint32_t(state.frame_records.size());
}

uint32_t HleCurrentRenderPass() {
  return t_pass;
}

void HleLeaveRenderPass(uint32_t previous) {
  t_pass = previous;
}

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
    uint64_t fetch;
    std::memcpy(&fetch, base + device + kDirtyFetch, 8);
    masks.fetch = uint32_t(std::byteswap(fetch));
    std::memcpy(&masks.state, base + device + kDirtyState, 8);
    masks.state = std::byteswap(masks.state);
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
  if (t_record_pending && PluginChannelEnabled()) state.frame_records.push_back(record);
  t_record_pending = true;
  record.kind = kind;
  record.pass = t_pass;
  std::copy(args, args + 4, record.args);
  record.up_vertices = 0;
  record.up_dwords = 0;
  record.segment_count = 0;
  record.vertex_shader = Load32(base, device + kVertexShader);
  record.pixel_shader = Load32(base, device + kPixelShader);
  ShaderMicrocode(base, record.vertex_shader, true, &record.ucode[0]);
  ShaderMicrocode(base, record.pixel_shader, false, &record.ucode[2]);
  LastDrawPacket(base, device, record.draw);

  const uint32_t touched = ApplyOverlay(state, masks);
  const uint64_t after_overlay = __rdtsc();
  // The render-state groups are not contiguous in the device: gather them, then share or append.
  uint8_t gathered[kRenderStateDwords * 4];
  uint32_t position = 0;
  for (const Group& group : kRenderStateGroups) {
    std::memcpy(gathered + position, dev + group.device_offset, group.count * 4);
    position += group.count * 4;
  }
  // The window offset and scissor the engine's PM4 wrote (the 0x2080 group, gathered second, after 16 dwords),
  // until the device's own copy of the register changes.
  for (uint32_t k = 0; k < 3; ++k) {
    if (!state.window_written[k]) continue;
    uint32_t device_value;
    std::memcpy(&device_value, gathered + (16 + k) * 4, 4);
    if (state.window_fresh[k]) {
      state.window_device[k] = device_value;
      state.window_fresh[k] = false;
    } else if (device_value != state.window_device[k]) {
      state.window_written[k] = false;
      continue;
    }
    std::memcpy(gathered + (16 + k) * 4, &state.window_values[k], 4);
  }
  // The resolve registers the GPU holds from a resolve's own writes (all in the 0x2300 group, gathered 7th).
  constexpr uint32_t kResolveGroupPosition = (16 + 3 + 21 + 5 + 12 + 21) * 4;
  for (uint32_t registers = state.register_overlay; registers;) {
    const uint32_t index = uint32_t(std::countr_zero(registers));
    registers &= registers - 1;
    const uint32_t k = kOverlayFirstRegister + index - 0x2300;
    if (k < 38) {
      std::memcpy(gathered + kResolveGroupPosition + 4 * k, &state.overlay_values[kOverlayRegisters + index], 4);
    }
  }
  state.render_state =
      Refresh(state, state.render_state, gathered, kRenderStateDwords, state.counters.render_state_blocks);
  const uint64_t after_render_state = __rdtsc();
  RefreshFetch(state, dev, masks.fetch);
  const uint64_t after_fetch = __rdtsc();
  state.bool_loop = Refresh(state, state.bool_loop, dev + kBoolLoop, kBoolLoopDwords, state.counters.bool_loop_blocks);
  const bool all = !state.pages_valid;
  RefreshPages(state, 0, state.vs_pages, dev + kVsConstants, masks.vs, touched, all);
  RefreshPages(state, 1, state.ps_pages, dev + kPsConstants, masks.ps, touched, all);
  state.pages_valid = true;

  record.render_state = state.render_state;
  record.fetch = state.fetch;
  record.bool_loop = state.bool_loop;
  std::copy(std::begin(state.vs_pages), std::end(state.vs_pages), record.vs_pages);
  std::copy(std::begin(state.ps_pages), std::end(state.ps_pages), record.ps_pages);

  ++state.counters.records;
  ++state.counters.by_kind[uint32_t(kind)];
  ++state.counters.by_pass[t_pass];
  const uint64_t end = __rdtsc();
  state.counters.cycles += end - start;
  state.counters.step_cycles[0] += after_overlay - start;
  state.counters.step_cycles[1] += after_render_state - after_overlay;
  state.counters.step_cycles[2] += after_fetch - after_render_state;
  state.counters.step_cycles[3] += end - after_fetch;
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

void HleNoteStateWrite(HleStateSpace space, uint32_t first, uint32_t dwords, const uint8_t* values,
                       HleWritePhase phase) {
  if (space == HleStateSpace::kBoolLoop) return;  // command buffer walks only (HleBufferBoolLoop)
  uint32_t base_index = space == HleStateSpace::kFloat ? 0 : kOverlayFetch;
  uint32_t limit = space == HleStateSpace::kFloat ? 2048 : kFetchDwords;
  if (space == HleStateSpace::kRegister) {
    const uint32_t first_register = 0x2000 + first;
    // The window offset and scissor, written by the engine's own PM4 (not the device's flush).
    if (HleRecordsEnabled() && first_register < 0x2083 && first_register + dwords > 0x2080) {
      State& state = g_state;
      SpinLock lock(state.lock);
      for (uint32_t r = std::max(first_register, 0x2080u); r < std::min(first_register + dwords, 0x2083u); ++r) {
        std::memcpy(&state.window_values[r - 0x2080], values + 4 * (r - first_register), 4);
        state.window_written[r - 0x2080] = true;
        state.window_fresh[r - 0x2080] = true;
      }
    }
    // Otherwise only the resolve registers are kept: clip the run to them.
    const uint32_t end = std::min(first_register + dwords, kOverlayFirstRegister + kOverlayRegisterCount);
    if (first_register >= end || end <= kOverlayFirstRegister) return;
    const uint32_t skip = first_register < kOverlayFirstRegister ? kOverlayFirstRegister - first_register : 0;
    values += skip * 4;
    first = first_register + skip - kOverlayFirstRegister;
    dwords = end - (first_register + skip);
    base_index = kOverlayRegisters;
    limit = kOverlayRegisterCount;
  }
  if (!HleRecordsEnabled() || first >= limit) {
    return;
  }
  State& state = g_state;
  SpinLock lock(state.lock);
  std::vector<uint32_t>& pending = phase == HleWritePhase::kBeforeFlush ? state.pending_before : state.pending_after;
  Queue(pending, base_index + first, std::min(dwords, limit - first), values);
  if (phase == HleWritePhase::kAfterRecord) {
    // The record is built: the next one sees these values unless its flush replaces them, like any earlier write.
    state.buffer_touched |= ApplyPending(state, state.pending_after);
  }
}

void HleBufferBegin(uint8_t* base, uint32_t device) {
  if (!HleRecordsEnabled()) return;
  State& state = g_state;
  SpinLock lock(state.lock);
  if (state.render_state == UINT32_MAX) return;
  // The image starts as the call's render state (its record was just built).
  std::memset(state.buffer_registers, 0, sizeof(state.buffer_registers));
  uint32_t position = state.render_state;
  for (const Group& group : kRenderStateGroups) {
    std::memcpy(state.buffer_registers + (group.first_register - 0x2000), state.arena.data() + position,
                group.count * 4);
    position += group.count;
  }
  std::copy(std::begin(t_record.ucode), std::end(t_record.ucode), state.buffer_ucode);
  if (state.bool_loop != UINT32_MAX) {
    std::memcpy(state.buffer_bool_loop, state.arena.data() + state.bool_loop, sizeof(state.buffer_bool_loop));
  }
  state.buffer_bool_loop_changed = false;
  state.buffer_dev = base + device;
  state.buffer_touched = 0;
  state.buffer_active = true;
}

void HleBufferRegisters(uint32_t first, uint32_t dwords, const uint8_t* values) {
  State& state = g_state;
  if (!state.buffer_active || first >= 0x400) return;
  SpinLock lock(state.lock);
  std::memcpy(state.buffer_registers + first, values, std::min(dwords, 0x400 - first) * 4);
}

void HleBufferShader(bool pixel, uint32_t physical, uint32_t dwords) {
  State& state = g_state;
  if (!state.buffer_active) return;
  SpinLock lock(state.lock);
  state.buffer_ucode[pixel ? 2 : 0] = physical;
  state.buffer_ucode[pixel ? 3 : 1] = dwords;
}

void HleBufferBoolLoop(uint32_t first, uint32_t dwords, const uint8_t* values) {
  State& state = g_state;
  if (!state.buffer_active || first >= kBoolLoopDwords) return;
  SpinLock lock(state.lock);
  std::memcpy(state.buffer_bool_loop + first, values, std::min(dwords, kBoolLoopDwords - first) * 4);
  state.buffer_bool_loop_changed = true;
}

void HleBufferDraw(const uint32_t draw[3]) {
  State& state = g_state;
  if (!state.buffer_active) return;
  const uint64_t start = __rdtsc();
  SpinLock lock(state.lock);
  DrawRecord record = {};
  record.kind = RecordKind::kBufferDraw;
  record.pass = t_pass;
  std::copy(state.buffer_ucode, state.buffer_ucode + 4, record.ucode);
  std::copy(draw, draw + 3, record.draw);
  uint8_t gathered[kRenderStateDwords * 4];
  uint32_t position = 0;
  for (const Group& group : kRenderStateGroups) {
    std::memcpy(gathered + position, state.buffer_registers + (group.first_register - 0x2000), group.count * 4);
    position += group.count * 4;
  }
  state.render_state =
      Refresh(state, state.render_state, gathered, kRenderStateDwords, state.counters.render_state_blocks);
  RefreshFetch(state, state.buffer_dev, 0);
  if (state.buffer_bool_loop_changed) {
    state.bool_loop = Refresh(state, state.bool_loop, reinterpret_cast<const uint8_t*>(state.buffer_bool_loop),
                              kBoolLoopDwords, state.counters.bool_loop_blocks);
    state.buffer_bool_loop_changed = false;
  }
  RefreshPages(state, 0, state.vs_pages, state.buffer_dev + kVsConstants, 0, state.buffer_touched, !state.pages_valid);
  RefreshPages(state, 1, state.ps_pages, state.buffer_dev + kPsConstants, 0, state.buffer_touched, !state.pages_valid);
  state.pages_valid = true;
  state.buffer_touched = 0;
  record.render_state = state.render_state;
  record.fetch = state.fetch;
  record.bool_loop = state.bool_loop;
  std::copy(std::begin(state.vs_pages), std::end(state.vs_pages), record.vs_pages);
  std::copy(std::begin(state.ps_pages), std::end(state.ps_pages), record.ps_pages);
  if (PluginChannelEnabled()) state.frame_records.push_back(record);
  ++state.counters.records;
  ++state.counters.by_kind[uint32_t(RecordKind::kBufferDraw)];
  ++state.counters.by_pass[t_pass];
  state.counters.cycles += __rdtsc() - start;
}

void HleBufferEnd() {
  State& state = g_state;
  if (!state.buffer_active) return;
  SpinLock lock(state.lock);
  // The buffer's render state stays on the GPU; the device's next flush rewrites what it dirtied, and the render
  // state block follows the device again at the next call's record.
  state.buffer_active = false;
}

void HleNoteUpVertices(uint8_t* base, uint32_t device, uint32_t physical, uint32_t dwords) {
  DrawRecord& record = t_record;
  record.up_vertices = physical;
  record.up_dwords = dwords;
  // BeginVertices clears vertex fetch constant 94 (slot 31, second) and points 95 (third) at the vertices the game
  // writes after the call; the GPU keeps both until slot 31 is flushed again.
  const uint32_t words[6] = {0, 0, std::byteswap(1u), 0, std::byteswap(physical | 3),
                             std::byteswap(0x10000000u | (dwords << 2) | 2)};
  State& state = g_state;
  SpinLock lock(state.lock);
  for (uint32_t k : {2u, 3u, 4u, 5u}) {
    state.overlay[kOverlayFetch + 31 * 6 + k] = true;
    state.overlay_values[kOverlayFetch + 31 * 6 + k] = words[k];
  }
  state.fetch_overlay_slots |= 1u << 31;
  state.fetch_changed = true;
  RefreshFetch(state, base + device, 0);
  record.fetch = state.fetch;
}

void HleNoteUntaggedCaller(uint32_t caller) {
  State& state = g_state;
  SpinLock lock(state.lock);
  ++state.untagged_callers[caller];
}

void HleNoteDecodedBuffer(bool hit) {
  ++(hit ? g_state.counters.decoded_hits : g_state.counters.decoded_misses);
}

void HleNotePredicated(bool passes, uint64_t bin_mask, uint64_t bin_select) {
  Counters& counters = g_state.counters;
  ++(passes ? counters.predicated_run : counters.predicated_skipped);
  counters.last_bin_mask = bin_mask;
  counters.last_bin_select = bin_select;
}

void HleNoteSegment(uint32_t physical, uint32_t dwords) {
  DrawRecord& record = t_record;
  if (record.segment_count < DrawRecord::kMaxSegments) {
    record.segments[record.segment_count][0] = physical;
    record.segments[record.segment_count][1] = dwords;
  }
  ++record.segment_count;
  State& state = g_state;
  ++state.counters.segments;
  SpinLock lock(state.lock);
  if (state.segments_seen.insert((uint64_t(physical) << 20) | dwords).second) ++state.counters.new_segments;
}

void HleRecordsFrame() {
  State& state = g_state;
  {
    SpinLock lock(state.lock);
    ++state.counters.frames;
    // Before the arena is compacted: the frame's records reference its blocks.
    if (PluginChannelEnabled() && HleRecordsEnabled()) {
      if (t_record_pending) state.frame_records.push_back(t_record);
      if (SubmitFrame submit = FindSubmitFrame()) {
        HleFrameAbi frame = {};
        frame.version = kHleFrameAbiVersion;
        frame.struct_size = sizeof(HleFrameAbi);
        frame.frame = state.frame_number;
        frame.records = state.frame_records.data();
        frame.record_count = uint32_t(state.frame_records.size());
        frame.record_size = sizeof(DrawRecord);
        frame.arena = state.arena.data();
        frame.arena_dwords = uint32_t(state.arena.size());
        frame.ucode_offset = uint32_t(offsetof(DrawRecord, ucode));
        frame.state_offset = uint32_t(offsetof(DrawRecord, render_state));
        static const auto groups = [] {
          std::vector<uint32_t> pairs;
          for (const Group& group : kRenderStateGroups) pairs.insert(pairs.end(), {group.first_register, group.count});
          return pairs;
        }();
        frame.group_count = uint32_t(groups.size() / 2);
        frame.groups = groups.data();
        frame.pass_names = kRenderPassNames;
        frame.pass_count = kRenderPassCount;
        frame.draw_offset = uint32_t(offsetof(DrawRecord, draw));
        submit(&frame);
      }
    }
    state.frame_records.clear();
    t_record_pending = false;
    ++state.frame_number;
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
