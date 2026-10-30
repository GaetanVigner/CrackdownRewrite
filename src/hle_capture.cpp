// HLE renderer milestones 1-2, game side: see hle_capture.h. Device layout from the reverse engineering of the
// statically linked XDK D3D library (HLE_RENDERER_PLAN.md): r3 of every draw is the device; +48 is the command
// buffer write pointer (the last dword written), +13076 the one of BeginVertices/BeginIndexedVertices until
// EndVertices; every dirty state group is flushed before the DRAW packet, so the shadow plus the call's own packets
// (walked in order, INDIRECT_BUFFERs followed) is the register state of each draw.

#include "hle_capture.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/system/kernel_state.h>

#include "guest_memory.h"
#include "guest_hooks.h"
#include "hang_watchdog.h"
#include "hle_records.h"
#include "pm4_decode.h"
#include "platform.h"
#include "single_tile_scene.h"

REXCVAR_DEFINE_BOOL(crackdown_hle_capture, false, "Crackdown",
                    "Developer diagnostics (HLE renderer): F8 records the next frames from the D3D device (game side) "
                    "and the GPU command processor to logs/hle_game_<n>.bin and logs/hle_gpu_<n>.bin");
REXCVAR_DEFINE_INT32(crackdown_hle_capture_frames, 3, "Crackdown", "Frames an F8 HLE capture records on the GPU side");
REXCVAR_DEFINE_INT32(crackdown_hle_auto_capture, 0, "Crackdown",
                     "Developer diagnostics: captures taken as if by F8 once a busy scene (1,500 D3D calls a frame or "
                     "more) has run for 20 s, then every 30 s while it lasts, up to this many a session");

namespace crackdown {
namespace {

using guest::Load32;

constexpr uint32_t kTraceVersion = 2;
constexpr uint32_t kUnknownRegister = 0xCDCDCDCD;

// The register blocks of SDK patch 0108, in its order.
struct Block {
  uint32_t first;
  uint32_t count;
};
constexpr Block kBlocks[] = {{0x2000, 0x400}, {0x4000, 2048}, {0x4800, 192}, {0x4900, 40}};

// Shadow groups of the render-state block: register range -> device offset (the flush emitters' map).
struct ShadowGroup {
  uint32_t first_register;
  uint32_t count;
  uint32_t device_offset;
};
constexpr ShadowGroup kRenderStateGroups[] = {
    {0x2000, 0x10, 10240}, {0x2080, 3, 10304}, {0x2100, 0x15, 10316},
    {0x2180, 5, 10400},    {0x2200, 0xC, 10420}, {0x2280, 0x15, 10468},
};
constexpr uint32_t kConstantsOffset = 0x700;   // 0x4000-0x47FF, VS then PS float constants
constexpr uint32_t kFetchOffset = 1024;        // 0x4800-0x48BF
constexpr uint32_t kBoolLoopOffset = 9984;     // 0x4900-0x4927
constexpr uint32_t kWritePointer = 48;
constexpr uint32_t kBeginWritePointer = 13076;
// The end limit of the ring segment being written; a call that changes it switched segments (sub_826BB408), so
// (pre, post] is not one contiguous run of its packets.
constexpr uint32_t kSegmentEnd = 56;
constexpr uint32_t kBinMaskLow = 12428;
constexpr uint32_t kPixelShader = 12412;
constexpr uint32_t kVertexShader = 12416;
constexpr uint32_t kMaxCallDwords = 0x10000;

struct Capture {
  std::mutex mutex;
  std::FILE* file = nullptr;
  std::filesystem::path path;
  uint32_t frame = 0;
  uint32_t frames_left = 0;
  uint32_t sequence = 0;
  uint32_t draws = 0;
  uint32_t unparsed_calls = 0;
  // The ring position the walk has reached (guest virtual, the last dword walked; 0 unknown) and the end of its
  // segment. PM4 written between hooked calls (constant writes of unhooked D3D functions) is walked from there.
  uint32_t cursor = 0;
  uint32_t cursor_segment_end = 0;
  uint32_t gap_dwords = 0;
  uint32_t gap_draws = 0;
  // With crackdown_hle_records on: logs/hle_rec_<n>.bin, the same draws with the state rebuilt from their H1a draw
  // record (hle_records.h) instead of the walk, for tools/hle-trace-compare.ps1 against the GPU side.
  std::FILE* records_file = nullptr;
};
Capture g_capture;

// The draw record of the hooked call being walked (nullptr outside one, or with records off).
const DrawRecord* g_pending_record = nullptr;

// Hooked D3D calls this frame, for crackdown_hle_auto_capture.
std::atomic<uint32_t> g_frame_calls{0};

// Where the draw being recorded comes from, written in the record's (GPU-side) visibility-query field: the
// comparison reports each source apart. Set by the hooks, under the capture mutex.
enum class Source : uint32_t { kDraw = 0, kCommandBuffer = 1, kGprFlush = 2, kClear = 3, kResolve = 4 };
Source g_source = Source::kDraw;
std::atomic<bool> g_armed{false};
std::atomic<bool> g_recording{false};

// The physical address the D3D library writes for a guest virtual address.
uint32_t PhysicalAddress(uint32_t va) {
  return (va & 0x1FFFFFFF) + ((va >> 20) >= 0xE00 ? 0x1000 : 0);
}

void WriteU32(std::FILE* file, uint32_t value) { std::fwrite(&value, sizeof(value), 1, file); }


// Microcode identity: dword count and the first eight ucode dwords, host order (what the GPU side records).
struct ShaderHead {
  uint32_t dwords = 0;
  uint32_t head[8] = {};
};

// The register state of one draw, in the trace's block layout. Render-state registers no shadow group covers and no
// packet wrote stay kUnknownRegister, which the comparison skips.
struct RegisterImage {
  std::vector<uint32_t> values;
  // Registers the walked packets wrote (not the shadow): what a command buffer sets itself.
  std::vector<uint8_t> written;
  ShaderHead vs, ps;

  RegisterImage() {
    uint32_t total = 0;
    for (const Block& block : kBlocks) total += block.count;
    values.assign(total, kUnknownRegister);
    written.assign(total, 0);
  }

  uint32_t* Find(uint32_t reg) {
    uint32_t position = 0;
    for (const Block& block : kBlocks) {
      if (reg >= block.first && reg < block.first + block.count) {
        return &values[position + reg - block.first];
      }
      position += block.count;
    }
    return nullptr;
  }

  void Set(uint32_t reg, uint32_t value) {
    if (uint32_t* slot = Find(reg)) *slot = value;
  }
};

// The float constants the command stream has written since the capture started, as the GPU's register file holds
// them. The shadow alone is not enough: shader literal constants are loaded (LOAD_ALU_CONSTANT) only when a shader
// is bound, so later draws with the same shader keep them on the GPU while neither the shadow nor their own packets
// carry them. Constants written by predicated packets (per tile) are kUnknownRegister: they differ between tiles.
struct GpuFloatConstants {
  static constexpr uint32_t kFirst = 0x4000;
  static constexpr uint32_t kCount = 2048;
  uint32_t values[kCount];
  bool seen[kCount];

  void Clear() { std::fill(std::begin(seen), std::end(seen), false); }
  void Note(uint32_t reg, uint32_t value) {
    if (reg >= kFirst && reg < kFirst + kCount) {
      values[reg - kFirst] = value;
      seen[reg - kFirst] = true;
    }
  }
};
GpuFloatConstants g_gpu_constants;

// Guest physical memory, where command buffers, ring segments and shader microcode live (big-endian dwords).
const uint8_t* PhysicalBase() {
  static const uint8_t* base = rex::system::kernel_state()->memory()->physical_membase();
  return base;
}

uint32_t ReadPhysical(uint32_t physical) {
  const uint8_t* host = PhysicalBase() + (physical & 0x1FFFFFFF);
  uint32_t value;
  std::memcpy(&value, host, sizeof(value));
  return std::byteswap(value);
}

ShaderHead ShaderFromGuest(uint8_t* base, uint32_t va, uint32_t dwords) {
  ShaderHead shader;
  if (!va || !dwords || dwords >= 0x10000) return shader;
  shader.dwords = dwords;
  for (uint32_t i = 0; i < 8 && i < dwords; ++i) shader.head[i] = Load32(base, va + 4 * i);
  return shader;
}

ShaderHead ShaderFromPhysical(uint32_t physical, uint32_t dwords) {
  ShaderHead shader;
  if (!dwords || dwords >= 0x10000) return shader;
  shader.dwords = dwords;
  for (uint32_t i = 0; i < 8 && i < dwords; ++i) shader.head[i] = ReadPhysical(physical + 4 * i);
  return shader;
}

ShaderHead DevicePixelShader(uint8_t* base, uint32_t device) {
  const uint32_t ps = Load32(base, device + kPixelShader);
  if (!guest::Plausible(ps)) return {};
  const uint32_t program = ps + Load32(base, ps + 64);
  return ShaderFromGuest(base, Load32(base, ps + 24) + Load32(base, program + 40), Load32(base, program + 44) / 4);
}

ShaderHead DeviceVertexShader(uint8_t* base, uint32_t device) {
  const uint32_t vs = Load32(base, device + kVertexShader);
  if (!guest::Plausible(vs)) return {};
  const uint32_t variant = (Load32(base, vs + 872) & 0x20) ? 1 : 0;
  const uint32_t program = vs + Load32(base, vs + 896 + 8 * variant);
  return ShaderFromGuest(base, Load32(base, vs + 32) + Load32(base, program + 872), Load32(base, program + 876) / 4);
}

// The device shadow: what the GPU registers hold before the call's own packets.
RegisterImage ImageFromShadow(uint8_t* base, uint32_t device) {
  RegisterImage image;
  for (const ShadowGroup& group : kRenderStateGroups) {
    for (uint32_t i = 0; i < group.count; ++i) {
      image.Set(group.first_register + i, Load32(base, device + group.device_offset + 4 * i));
    }
  }
  for (uint32_t i = 0; i < kBlocks[1].count; ++i) image.Set(0x4000 + i, Load32(base, device + kConstantsOffset + 4 * i));
  for (uint32_t i = 0; i < kBlocks[2].count; ++i) image.Set(0x4800 + i, Load32(base, device + kFetchOffset + 4 * i));
  for (uint32_t i = 0; i < kBlocks[3].count; ++i) image.Set(0x4900 + i, Load32(base, device + kBoolLoopOffset + 4 * i));
  for (uint32_t i = 0; i < GpuFloatConstants::kCount; ++i) {
    if (g_gpu_constants.seen[i]) image.Set(GpuFloatConstants::kFirst + i, g_gpu_constants.values[i]);
  }
  image.vs = DeviceVertexShader(base, device);
  image.ps = DevicePixelShader(base, device);
  return image;
}

// True while walking the PM4 between hooked calls: its writes reach the GPU's running copy, its draws are only counted.
bool g_gap_walk = false;

// Diagnostics: the first time a float constant receives a PM4 type-3 DRAW_INDX header, the packet that wrote it is
// logged (a walk that lost its alignment).
uint32_t g_walk_packet = 0;
uint32_t g_walk_header = 0;
uint32_t g_walk_depth = 0;

// A register write the walk applies: to the draw's image and, for float constants, to the GPU's running copy.
void Apply(RegisterImage& image, uint32_t reg, uint32_t value, bool predicated = false) {
  static bool logged = false;
  const uint32_t opcode = (value >> 8) & 0x7F;
  if (!logged && reg >= 0x4000 && reg < 0x4800 && (value & 0xC00080FE) == 0xC0000000 &&
      ((value >> 16) & 0x3FFF) < 8 && (opcode == 0x22 || opcode == 0x36)) {
    logged = true;
    REXLOG_WARN("[HLE] float constant 0x{:04X} set to {:08X} by the packet {:08X} at physical {:08X} (depth {}, source {})",
                reg, value, g_walk_header, g_walk_packet, g_walk_depth, uint32_t(g_source));
  }
  const uint32_t applied = predicated ? kUnknownRegister : value;
  if (uint32_t* slot = image.Find(reg)) {
    *slot = applied;
    image.written[slot - image.values.data()] = 1;
  }
  g_gpu_constants.Note(reg, applied);
}

void WriteShader(std::FILE* file, const ShaderHead& shader) {
  WriteU32(file, shader.dwords);
  for (uint32_t dword : shader.head) WriteU32(file, dword);
}

// Record kinds: 1 draw, 2 resolve (EDRAM copy mode), as on the GPU side.
void RecordDraw(uint8_t* base, uint32_t device, const RegisterImage& image, uint32_t packet_physical,
                uint32_t header, uint32_t initiator, uint32_t index_base) {
  Capture& capture = g_capture;
  if (g_gap_walk) {
    ++capture.gap_draws;
    return;
  }
  const uint32_t mode = image.values[0x208] & 7;  // RB_MODECONTROL (0x2208) edram_mode
  const uint32_t sequence = capture.sequence++;
  auto write = [&](std::FILE* file, const uint32_t* values) {
    WriteU32(file, mode == 6 ? 2 : 1);
    WriteU32(file, capture.frame);
    WriteU32(file, sequence);
    WriteU32(file, packet_physical);
    WriteU32(file, initiator);
    WriteU32(file, index_base);
    WriteU32(file, 0);  // index count (not compared)
    WriteU32(file, g_pending_record ? g_pending_record->pass : 0);  // index format (not compared): the render pass
    WriteU32(file, header & 1);
    WriteU32(file, Load32(base, device + kBinMaskLow));
    WriteU32(file, 0);
    WriteU32(file, uint32_t(g_source));  // the GPU side's visibility-query kill field
    WriteShader(file, image.vs);
    WriteShader(file, image.ps);
    std::fwrite(values, sizeof(uint32_t), image.values.size(), file);
  };
  write(capture.file, image.values.data());
  if (capture.records_file && g_pending_record) {
    std::vector<uint32_t> rebuilt(image.values);
    HleExpandRecord(*g_pending_record, rebuilt.data());
    // A command buffer draw runs with the state its record inherits plus what the buffer writes; the buffer's part
    // is a record of its decoded segments (later), so here it is taken from the walk to check the inherited part.
    if (g_source == Source::kCommandBuffer) {
      for (size_t i = 0; i < rebuilt.size(); ++i) {
        if (image.written[i]) rebuilt[i] = image.values[i];
      }
    }
    write(capture.records_file, rebuilt.data());
  }
  ++capture.draws;
}

// Base register of SET_CONSTANT / LOAD_ALU_CONSTANT types: ALU, fetch, bool, loop, registers.
uint32_t ConstantBase(uint32_t type) {
  switch (type) {
    case 0: return 0x4000;
    case 1: return 0x4800;
    case 2: return 0x4900;
    case 3: return 0x4908;
    case 4: return 0x2000;
    default: return 0;
  }
}

constexpr uint32_t kMaxIndirectDepth = 4;

// Walks `dwords` of PM4 at a guest physical address, applying the register and constant writes to `image` in order,
// following INDIRECT_BUFFERs, and records every DRAW packet with the state it executes with. Writes of predicated
// packets (per-tile values of predicated tiling) leave their registers unknown; the comparison skips those.
void WalkPm4(uint8_t* base, uint32_t device, RegisterImage& image, uint32_t physical, uint32_t dwords, uint32_t depth) {
  uint32_t i = 0;
  while (i < dwords) {
    const uint32_t at = physical + 4 * i;
    const uint32_t header = ReadPhysical(at);
    g_walk_packet = at;
    g_walk_header = header;
    g_walk_depth = depth;
    const uint32_t type = header >> 30;
    auto data = [&](uint32_t n) { return ReadPhysical(at + 4 * (1 + n)); };
    if (type == 0) {
      const uint32_t count = ((header >> 16) & 0x3FFF) + 1;
      const uint32_t first = header & 0x7FFF;
      const bool one_register = (header >> 15) & 1;
      for (uint32_t m = 0; m < count && i + 1 + m < dwords; ++m) Apply(image, one_register ? first : first + m, data(m));
      i += 1 + count;
      continue;
    }
    if (type == 1) {
      Apply(image, header & 0x7FF, data(0));
      Apply(image, (header >> 11) & 0x7FF, data(1));
      i += 3;
      continue;
    }
    if (type == 2) {
      i += 1;
      continue;
    }
    const uint32_t count = ((header >> 16) & 0x3FFF) + 1;
    const uint32_t opcode = (header >> 8) & 0x7F;
    const bool predicated = header & 1;
    switch (opcode) {
      case 0x2D: {  // SET_CONSTANT
        const uint32_t offset_type = data(0);
        const uint32_t first = ConstantBase((offset_type >> 16) & 0xFF) + (offset_type & 0x7FF);
        for (uint32_t k = 0; k + 1 < count; ++k) Apply(image, first + k, data(1 + k), predicated);
        break;
      }
      case 0x2F: {  // LOAD_ALU_CONSTANT: constants from memory
        const uint32_t address = data(0) & 0x3FFFFFFF;
        const uint32_t offset_type = data(1);
        const uint32_t size = data(2) & 0xFFF;
        const uint32_t first = ConstantBase((offset_type >> 16) & 0xFF) + (offset_type & 0x7FF);
        for (uint32_t k = 0; k < size; ++k) Apply(image, first + k, ReadPhysical(address + 4 * k), predicated);
        break;
      }
      case 0x27: {  // IM_LOAD: shader microcode from memory
        const uint32_t address_type = data(0);
        const ShaderHead shader = ShaderFromPhysical(address_type & ~3u, data(1) & 0xFFFF);
        if ((address_type & 3) == 0) image.vs = shader;
        if ((address_type & 3) == 1) image.ps = shader;
        break;
      }
      case 0x2B: {  // IM_LOAD_IMMEDIATE: inline microcode
        const ShaderHead shader = ShaderFromPhysical(at + 12, data(1) & 0xFFFF);
        if ((data(0) & 3) == 0) image.vs = shader;
        if ((data(0) & 3) == 1) image.ps = shader;
        break;
      }
      case 0x22:  // DRAW_INDX: viz query, initiator, index base, size
        RecordDraw(base, device, image, at, header, data(1), data(2));
        break;
      case 0x36:  // DRAW_INDX_2: initiator, then inline indices
        RecordDraw(base, device, image, at, header, data(0), 0);
        break;
      case 0x37:  // INDIRECT_BUFFER_PFD
      case 0x3F:  // INDIRECT_BUFFER
        if (depth < kMaxIndirectDepth) {
          const uint32_t target = data(0) & 0x1FFFFFFF;
          const uint32_t target_dwords = data(1) & 0xFFFFF;
          // Logged as a range (u32 kind 4, frame, sequence, physical address, dwords), then walked.
          WriteU32(g_capture.file, 4);
          WriteU32(g_capture.file, g_capture.frame);
          WriteU32(g_capture.file, g_capture.sequence);
          WriteU32(g_capture.file, target);
          WriteU32(g_capture.file, target_dwords);
          WalkPm4(base, device, image, target, target_dwords, depth + 1);
        }
        break;
      default:
        break;
    }
    i += 1 + count;
  }
}

// Walks the PM4 a D3D call wrote into the ring, (pre, post] (guest virtual, the last dword written), on top of the
// device shadow after the call. A ring segment switch (post outside the range) records the shadow alone.
bool RecordCall(uint8_t* base, uint32_t device, uint32_t pre, uint32_t post, bool same_segment,
                bool record_unparsed = true) {
  RegisterImage image = ImageFromShadow(base, device);
  if (!same_segment || !post || post <= pre || (post - pre) / 4 > kMaxCallDwords) {
    ++g_capture.unparsed_calls;
    if (record_unparsed) RecordDraw(base, device, image, 0, 0, 0, 0);
    return false;
  }
  WalkPm4(base, device, image, PhysicalAddress(pre + 4), (post - pre) / 4, 0);
  return true;
}

// Walks the ring from the cursor to `pre`, the write pointer at the start of a hooked call, if both lie in the
// segment the cursor was left in.
void WalkGap(uint8_t* base, uint32_t device, uint32_t pre, uint32_t segment_end) {
  Capture& capture = g_capture;
  if (!capture.cursor || segment_end != capture.cursor_segment_end || pre <= capture.cursor ||
      (pre - capture.cursor) / 4 > kMaxCallDwords) {
    return;
  }
  RegisterImage image = ImageFromShadow(base, device);
  g_gap_walk = true;
  WalkPm4(base, device, image, PhysicalAddress(capture.cursor + 4), (pre - capture.cursor) / 4, 0);
  g_gap_walk = false;
  capture.gap_dwords += (pre - capture.cursor) / 4;
}

// Moves the cursor to the end of a hooked call's output. After a Begin* call the ring holds the vertices the game
// writes next, not PM4: the cursor is unknown until the next call ends.
void AdvanceCursor(uint8_t* base, uint32_t device, uint32_t post) {
  g_capture.cursor = post;
  g_capture.cursor_segment_end = Load32(base, device + kSegmentEnd);
}

// RunCommandBuffer(dev, cb, predSelect) replays a command buffer stored in a mesh resource (precompiled PM4,
// relocated at load) with one INDIRECT_BUFFER per segment. Normally the walk of the PM4 the call wrote follows those
// INDIRECT_BUFFERs; when the call's output straddles a ring segment, this falls back to the segment list the
// reverse engineering found at cb+116 ({next, count, (physical address, dwords) x count}; unconfirmed: the first
// capture found no segment there, so its first words are logged once).
void RecordCommandBuffer(uint8_t* base, uint32_t device, uint32_t command_buffer) {
  static bool logged = false;
  if (!logged && guest::Plausible(command_buffer)) {
    logged = true;
    std::string words;
    for (uint32_t offset = 100; offset < 164; offset += 4) {
      words += fmt::format(" +{}={:08X}", offset, Load32(base, command_buffer + offset));
    }
    REXLOG_WARN("[HLE] command buffer {:08X}:{}", command_buffer, words);
  }
  constexpr uint32_t kSegmentList = 116;
  constexpr uint32_t kMaxNodes = 64;
  Capture& capture = g_capture;
  RegisterImage image = ImageFromShadow(base, device);
  uint32_t node = guest::Plausible(command_buffer) ? Load32(base, command_buffer + kSegmentList) : 0;
  for (uint32_t n = 0; n < kMaxNodes && guest::Plausible(node); ++n) {
    const uint32_t count = Load32(base, node + 4);
    for (uint32_t s = 0; s < count && s < 256; ++s) {
      const uint32_t physical = Load32(base, node + 8 + 8 * s) & 0x1FFFFFFF;
      const uint32_t dwords = Load32(base, node + 12 + 8 * s) & 0xFFFFF;
      WriteU32(capture.file, 4);
      WriteU32(capture.file, capture.frame);
      WriteU32(capture.file, capture.sequence);
      WriteU32(capture.file, physical);
      WriteU32(capture.file, dwords);
      WalkPm4(base, device, image, physical, dwords, 1);
    }
    node = Load32(base, node);
  }
}

// H1a records: the PM4 written to the ring between hooked calls (the engine's prebaked shader binds, unhooked D3D
// functions) is walked for its float, fetch and resolve register writes, which reach the GPU without the device's
// flush. The cursor is the last ring dword walked (guest virtual, 0 unknown), taken after g_record_cursor_switches
// ring segment switches.
uint32_t g_record_cursor = 0;
// The GPU's bin mask and select as the walked packets set them (persistent, like the command processor's), for
// their predicated packets.
uint64_t g_record_bin_mask = ~uint64_t(0);
uint64_t g_record_bin_select = ~uint64_t(0);

// Diagnostics: where the walk is (0 ring gap, else the RecordKind of the call whose output it walks), and how many
// bin mask/select packets it has logged.
uint32_t g_walk_source = 0;
uint32_t g_bin_packets_logged = 0;
// Whether the walk is a RunCommandBuffer call's output: its draws become kBufferDraw records.
bool g_buffer_walk = false;

// The PM4 decoder (pm4_decode.h); a decode that stops before its buffer's end is logged (the first 32 times).
using pm4::DecodedPm4;
using pm4::KnownOpcode;
using pm4::WalkOp;

void LogDecodeStop(uint32_t at, uint32_t index, uint32_t dwords, uint32_t header) {
  static uint32_t logged = 0;
  if (logged < 32) {
    ++logged;
    REXLOG_INFO("[HLE records] decode stopped at {:08X} (dword {} of {}): header {:08X} (source {})", at, index,
                dwords, header, g_walk_source);
  }
}

void DecodePm4(uint32_t physical, uint32_t dwords, DecodedPm4& out) {
  pm4::DecodePm4(PhysicalBase(), physical, dwords, out, &LogDecodeStop);
}

// Indirect buffers decoded once (command buffer segments, the engine's prebaked binds: static data), keyed by
// (address << 20) | dwords; dropped when a command buffer load relocates or fixes up any buffer.
std::unordered_map<uint64_t, DecodedPm4> g_decoded_buffers;
uint32_t g_decoded_loads = 0;

void ExecuteOps(const DecodedPm4& decoded, uint32_t depth, HleWritePhase phase);

void ExecuteIndirect(uint32_t physical, uint32_t dwords, uint32_t depth, HleWritePhase phase) {
  const uint32_t loads = CommandBufferLoads();
  if (loads != g_decoded_loads) {
    g_decoded_buffers.clear();
    g_decoded_loads = loads;
  }
  const uint64_t key = (uint64_t(physical) << 20) | dwords;
  auto it = g_decoded_buffers.find(key);
  const bool hit = it != g_decoded_buffers.end();
  if (!hit) {
    it = g_decoded_buffers.emplace(key, DecodedPm4{}).first;
    DecodePm4(physical, dwords, it->second);
  }
  HleNoteDecodedBuffer(hit);
  ExecuteOps(it->second, depth, phase);
}

void ExecuteOps(const DecodedPm4& decoded, uint32_t depth, HleWritePhase phase) {
  for (const WalkOp& op : decoded.ops) {
    // The command processor skips a predicated packet whose bin mask and select share no bit, before running it.
    if (op.predicated) {
      const bool passes = (g_record_bin_mask & g_record_bin_select) != 0;
      HleNotePredicated(passes, g_record_bin_mask, g_record_bin_select);
      if (!passes) continue;
    }
    switch (op.kind) {
      case WalkOp::kBin: {
        auto set_low = [](uint64_t& value, uint32_t low) { value = (value & ~uint64_t(0xFFFFFFFF)) | low; };
        auto set_high = [](uint64_t& value, uint32_t high) {
          value = (value & 0xFFFFFFFF) | (uint64_t(high) << 32);
        };
        if (op.code == 0x50) g_record_bin_mask = (uint64_t(op.first) << 32) | op.count;
        if (op.code == 0x51) g_record_bin_select = (uint64_t(op.first) << 32) | op.count;
        if (op.code == 0x60) set_low(g_record_bin_mask, op.first);
        if (op.code == 0x61) set_high(g_record_bin_mask, op.first);
        if (op.code == 0x62) set_low(g_record_bin_select, op.first);
        if (op.code == 0x63) set_high(g_record_bin_select, op.first);
        static uint32_t odd_logged = 0;
        const uint32_t low = uint32_t(g_record_bin_select), high = uint32_t(g_record_bin_select >> 32);
        if (odd_logged < 16 && ((low != 0xFFFFFFFF && low > 0xFF && low != 0x80000003) ||
                                (high != 0xFFFFFFFF && high > 0xFF))) {
          ++odd_logged;
          REXLOG_WARN("[HLE records] unusual bin select {:016X} from packet 0x{:02X} {:08X} (source {}, depth {})",
                      g_record_bin_select, op.code, op.first, g_walk_source, depth);
        }
        if (g_bin_packets_logged < 48) {
          ++g_bin_packets_logged;
          REXLOG_INFO("[HLE records] bin packet 0x{:02X} {:08X} {:08X} (predicated {}, source {}, depth {}): mask "
                      "{:016X} select {:016X}",
                      op.code, op.first, op.count, op.predicated, g_walk_source, depth, g_record_bin_mask,
                      g_record_bin_select);
        }
        break;
      }
      case WalkOp::kWrite:
        HleNoteStateWrite(HleStateSpace(op.code), op.first, op.count,
                          reinterpret_cast<const uint8_t*>(decoded.pool.data() + op.values), phase);
        if (g_buffer_walk && HleStateSpace(op.code) == HleStateSpace::kRegister) {
          HleBufferRegisters(op.first, op.count, reinterpret_cast<const uint8_t*>(decoded.pool.data() + op.values));
        }
        if (g_buffer_walk && HleStateSpace(op.code) == HleStateSpace::kBoolLoop) {
          HleBufferBoolLoop(op.first, op.count, reinterpret_cast<const uint8_t*>(decoded.pool.data() + op.values));
        }
        break;
      case WalkOp::kDraw:
        if (g_buffer_walk) {
          const uint32_t draw[3] = {op.first, op.count, op.values};
          HleBufferDraw(draw);
        }
        break;
      case WalkOp::kShader:
        if (g_buffer_walk && op.code <= 1) HleBufferShader(op.code == 1, op.first, op.count);
        break;
      case WalkOp::kIndirect:
        if (depth < kMaxIndirectDepth) ExecuteIndirect(op.first, op.count, depth + 1, phase);
        break;
    }
  }
}

// Walks PM4 written to the ring (changes every frame: decoded, not cached).
void WalkConstantWrites(uint32_t physical, uint32_t dwords, uint32_t source, HleWritePhase phase) {
  thread_local DecodedPm4 decoded;
  decoded.ops.clear();
  decoded.pool.clear();
  DecodePm4(physical, dwords, decoded);
  g_walk_source = source;
  ExecuteOps(decoded, 0, phase);
}

// The ring is a chain of segments the D3D library kicks to the GPU one at a time (sub_826BB408 switches to the next,
// which can be anywhere): the switches so far, and the last one's old segment end and new segment start (write
// pointers: the last dword written).
uint32_t g_ring_switches = 0;
uint32_t g_switch_old_end = 0;
uint32_t g_switch_new_start = 0;

// Calls `walk(physical, dwords)` for the ring dwords in (from, to], `from` taken when g_ring_switches was
// `from_switches`: one run in the same segment, two across one switch; false (nothing walked) across more.
template <typename Walk>
bool WalkRing(uint32_t from, uint32_t from_switches, uint32_t to, Walk walk) {
  auto run = [&](uint32_t begin, uint32_t end) {
    if (end > begin && (end - begin) / 4 <= kMaxCallDwords) walk(PhysicalAddress(begin + 4), (end - begin) / 4);
  };
  if (g_ring_switches == from_switches) {
    run(from, to);
    return true;
  }
  if (g_ring_switches == from_switches + 1) {
    run(from, g_switch_old_end);
    run(g_switch_new_start, to);
    return true;
  }
  return false;
}

uint32_t g_record_cursor_switches = 0;

void WalkRecordGap(uint32_t pre) {
  if (g_record_cursor) {
    WalkRing(g_record_cursor, g_record_cursor_switches, pre, [](uint32_t physical, uint32_t dwords) {
      WalkConstantWrites(physical, dwords, 0, HleWritePhase::kBeforeFlush);
    });
  }
}

// The INDIRECT_BUFFERs among the PM4 a RunCommandBuffer call wrote: the command buffer segments it replays.
void NoteSegments(uint32_t physical, uint32_t dwords) {
  for (uint32_t i = 0; i < dwords;) {
    const uint32_t header = ReadPhysical(physical + 4 * i);
    const uint32_t type = header >> 30;
    if (type == 2) {
      ++i;
      continue;
    }
    const uint32_t count = ((header >> 16) & 0x3FFF) + 1;
    if (type == 3 && ((header >> 8) & 0x7F) == 0x3F && i + 2 < dwords) {
      HleNoteSegment(ReadPhysical(physical + 4 * (i + 1)) & 0x1FFFFFFF, ReadPhysical(physical + 4 * (i + 2)) & 0xFFFFF);
    }
    i += 1 + count;
  }
}

// Runs a hooked call (r3 = device, arguments r4-r7) and returns its H1a draw record (nullptr with records off): the
// dirty masks are read before the call flushes them, the state after.
// The D3D device the hooked calls use (for the pass markers, written outside a call).
uint32_t g_hle_device = 0;
uint32_t g_markers_written = 0, g_markers_skipped = 0;

// Writes a pass marker (a type-3 NOP) to the device's ring if its segment has room (the runtime's own check: the
// write pointer, the last dword written, at most the segment's limit at device+56); skipped otherwise.
void WriteHleMarker(uint8_t* base, uint32_t pass, uint32_t kind) {
  if (!g_hle_device || !HleMarkersEnabled()) return;
  const uint32_t device = g_hle_device;
  uint32_t pointer = Load32(base, device + kWritePointer);
  if (!guest::Plausible(pointer) || pointer > Load32(base, device + 56)) {
    ++g_markers_skipped;
    return;
  }
  const uint32_t words[6] = {0xC0001000u | (4u << 16), kHleMarkerMagic, pass, uint32_t(HleRecordsFrameNumber()),
                             kind, HleRecordsInFrame()};
  for (uint32_t word : words) {
    pointer += 4;
    guest::Store32(base, pointer, word);
  }
  guest::Store32(base, device + kWritePointer, pointer);
  ++g_markers_written;
}

template <typename Original>
const DrawRecord* CallWithRecord(PPCContext& ctx, uint8_t* base, RecordKind kind, Original original) {
  const uint32_t device = ctx.r3.u32;
  g_hle_device = device;
  const uint32_t args[4] = {ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32};
  g_frame_calls.fetch_add(1, std::memory_order_relaxed);
  const HleRecordMasks masks = HleReadRecordMasks(base, device);
  const uint32_t pre = Load32(base, device + kWritePointer);
  const uint32_t pre_switches = g_ring_switches;
  const uint32_t caller = uint32_t(ctx.lr);
  if (HleRecordsEnabled()) WalkRecordGap(pre);
  original();
  const DrawRecord* record = HleRecordCall(base, device, kind, args, masks);
  if (!record) {
    return nullptr;
  }
  // After a Begin* call the ring holds the vertices the game writes next, not PM4: the cursor is unknown until the
  // next call ends.
  const bool begin = kind == RecordKind::kBeginVertices || kind == RecordKind::kBeginIndexedVertices;
  g_record_cursor = begin ? 0 : Load32(base, device + kWritePointer);
  g_record_cursor_switches = g_ring_switches;
  if (record->pass == 0) HleNoteUntaggedCaller(caller);
  if (kind == RecordKind::kBeginVertices && ctx.r3.u32) {
    // BeginVertices(device, primitive, vertex count, stride) returns where the game writes the vertices.
    HleNoteUpVertices(base, device, PhysicalAddress(ctx.r3.u32), args[1] * args[2] / 4);
  }
  if (kind == RecordKind::kCommandBuffer) {
    // Diagnostics: the predicate selects RunCommandBuffer is called with (r5), the first sixteen distinct ones.
    static uint32_t seen[16];
    static uint32_t seen_count = 0;
    if (seen_count < 16 && std::find(seen, seen + seen_count, args[1]) == seen + seen_count) {
      seen[seen_count++] = args[1];
      REXLOG_INFO("[HLE records] RunCommandBuffer predicate select {:08X} (command buffer {:08X})", args[1], args[0]);
    }
  }
  if (kind == RecordKind::kCommandBuffer || kind == RecordKind::kClear || kind == RecordKind::kResolve ||
      kind == RecordKind::kGprFlush) {
    // These calls write their own state to the ring (a command buffer through its segments); the constants they
    // leave on the GPU apply to the draws after them.
    const uint32_t post = Load32(base, device + kWritePointer);
    if (kind == RecordKind::kCommandBuffer) {
      HleBufferBegin(base, device);
      g_buffer_walk = true;
    }
    const bool walkable = WalkRing(pre, pre_switches, post, [&](uint32_t physical, uint32_t dwords) {
      if (kind == RecordKind::kCommandBuffer) NoteSegments(physical, dwords);
      WalkConstantWrites(physical, dwords, uint32_t(kind), HleWritePhase::kAfterRecord);
    });
    static uint32_t skipped = 0;
    if (!walkable) ++skipped;
    if (!walkable && (skipped & (skipped - 1)) == 0) {
      // Diagnostics: logged at 1, 2, 4, 8... calls.
      REXLOG_INFO("[HLE records] {} calls' output not walked (several ring segment switches), last a kind {} call",
                  skipped, uint32_t(kind));
    }
    if (!walkable && kind == RecordKind::kCommandBuffer) {
      // The call's output spans two ring segments: replay the buffer's segments from its own list instead
      // (cb + 116: nodes {next, count, then (address, dwords) pairs from node + 8}), the list RunCommandBuffer emits
      // one INDIRECT_BUFFER per entry from.
      g_walk_source = uint32_t(kind);
      uint32_t node = guest::Plausible(args[0]) ? Load32(base, args[0] + 116) : 0;
      for (uint32_t n = 0; n < 64 && guest::Plausible(node); ++n) {
        const uint32_t count = Load32(base, node + 4);
        for (uint32_t s = 0; s < count && s < 256; ++s) {
          const uint32_t address = Load32(base, node + 8 + 8 * s) & 0x1FFFFFFF;
          const uint32_t dwords = Load32(base, node + 12 + 8 * s) & 0xFFFFF;
          HleNoteSegment(address, dwords);
          if (dwords <= kMaxCallDwords) ExecuteIndirect(address, dwords, 1, HleWritePhase::kAfterRecord);
        }
        node = Load32(base, node);
      }
    }
    if (g_buffer_walk) {
      g_buffer_walk = false;
      HleBufferEnd();
    }
  }
  return record;
}

template <typename Original>
void CaptureDraw(PPCContext& ctx, uint8_t* base, bool begin_variant, Original original, RecordKind kind,
                 Source source = Source::kDraw) {
  if (!g_recording.load(std::memory_order_relaxed)) {
    CallWithRecord(ctx, base, kind, original);
    return;
  }
  const uint32_t device = ctx.r3.u32;
  const uint32_t pre = Load32(base, device + kWritePointer);
  const uint32_t segment_end = Load32(base, device + kSegmentEnd);
  {
    std::lock_guard lock(g_capture.mutex);
    if (g_capture.file) WalkGap(base, device, pre, segment_end);
  }
  const DrawRecord* record = CallWithRecord(ctx, base, kind, original);
  std::lock_guard lock(g_capture.mutex);
  if (!g_capture.file) {
    return;
  }
  const uint32_t post = Load32(base, device + (begin_variant ? kBeginWritePointer : kWritePointer));
  g_source = source;
  g_pending_record = record;
  RecordCall(base, device, pre, post, Load32(base, device + kSegmentEnd) == segment_end);
  g_pending_record = nullptr;
  g_source = Source::kDraw;
  AdvanceCursor(base, device, begin_variant ? 0 : post);
}

std::filesystem::path NextPath(const std::filesystem::path& folder, uint32_t& index) {
  std::error_code ec;
  for (index = 1; index < 1000; ++index) {
    if (!std::filesystem::exists(folder / ("hle_game_" + std::to_string(index) + ".bin"), ec)) {
      break;
    }
  }
  return folder / ("hle_game_" + std::to_string(index) + ".bin");
}

}  // namespace

void HleNoteLiteralPackets(uint8_t* base, uint32_t device, uint32_t pre, uint32_t segment_end) {
  if (!HleRecordsEnabled()) {
    return;
  }
  const uint32_t post = Load32(base, device + kWritePointer);
  if (Load32(base, device + kSegmentEnd) != segment_end || post <= pre || (post - pre) / 4 > kMaxCallDwords) {
    return;
  }
  const uint32_t physical = PhysicalAddress(pre + 4);
  const uint32_t dwords = (post - pre) / 4;
  const uint8_t* memory = rex::system::kernel_state()->memory()->physical_membase();
  for (uint32_t i = 0; i < dwords;) {
    const uint32_t header = ReadPhysical(physical + 4 * i);
    if ((header >> 30) == 2) {
      ++i;
      continue;
    }
    const uint32_t count = ((header >> 16) & 0x3FFF) + 1;
    // LOAD_ALU_CONSTANT: address, offset and type (type 0: ALU constants, offset in dwords), size in dwords.
    if ((header >> 30) == 3 && ((header >> 8) & 0x7F) == 0x2F && i + 3 < dwords) {
      const uint32_t address = ReadPhysical(physical + 4 * (i + 1)) & 0x1FFFFFFF;
      const uint32_t offset_type = ReadPhysical(physical + 4 * (i + 2));
      const uint32_t size = ReadPhysical(physical + 4 * (i + 3)) & 0xFFF;
      if (((offset_type >> 16) & 0xFF) == 0) {
        HleNoteStateWrite(HleStateSpace::kFloat, offset_type & 0x7FF, size, memory + address,
                          HleWritePhase::kDuringCall);
      }
    }
    i += 1 + count;
  }
}

bool HleCaptureEnabled() {
  return REXCVAR_GET(crackdown_hle_capture);
}

bool StartHleCapture() {
  if (g_recording.load() || g_armed.exchange(true)) {
    return false;
  }
  REXLOG_WARN("[HLE] capture armed for the next frame");
  return true;
}

namespace {

void AutoCapture() {
  using Clock = std::chrono::steady_clock;
  static Clock::time_point busy_since;
  static Clock::time_point last_capture;
  static bool busy = false;
  static int32_t taken = 0;
  const uint32_t calls = g_frame_calls.exchange(0, std::memory_order_relaxed);
  if (taken >= REXCVAR_GET(crackdown_hle_auto_capture) || !REXCVAR_GET(crackdown_hle_capture)) return;
  const Clock::time_point now = Clock::now();
  if (calls < 1500) {
    busy = false;
    return;
  }
  if (!busy) {
    busy = true;
    busy_since = now;
  }
  if (now - busy_since >= std::chrono::seconds(20) &&
      (taken == 0 || now - last_capture >= std::chrono::seconds(30)) && StartHleCapture()) {
    ++taken;
    last_capture = now;
    REXLOG_WARN("[HLE] automatic capture {} of {}", taken, REXCVAR_GET(crackdown_hle_auto_capture));
  }
}

}  // namespace

void HleCaptureFrame() {
  HleRecordsFrame();
  AutoCapture();
  Capture& capture = g_capture;
  if (g_recording.load(std::memory_order_relaxed)) {
    std::lock_guard lock(capture.mutex);
    WriteU32(capture.file, 3);
    WriteU32(capture.file, capture.frame);
    WriteU32(capture.file, capture.sequence);
    ++capture.frame;
    capture.sequence = 0;
    if (--capture.frames_left == 0) {
      g_recording = false;
      std::fclose(capture.file);
      capture.file = nullptr;
      if (capture.records_file) {
        std::fclose(capture.records_file);
        capture.records_file = nullptr;
      }
      REXLOG_WARN("[HLE] game side: {} draws in {} frames ({} calls without a parsed packet; {} dwords and {} draws "
                  "between hooked calls) written to {}",
                  capture.draws, capture.frame, capture.unparsed_calls, capture.gap_dwords, capture.gap_draws,
                  capture.path.string());
    }
    return;
  }
  if (!g_armed.exchange(false)) {
    return;
  }
  // The session's logs folder (the hang watchdog's), or the port folder's logs before it starts.
  const std::filesystem::path folder =
      DiagnosticsLogsFolder().empty() ? PortFolder() / "logs" : DiagnosticsLogsFolder();
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  uint32_t index = 0;
  std::lock_guard lock(capture.mutex);
  capture.path = NextPath(folder, index);
#if defined(_WIN32)
  if (_wfopen_s(&capture.file, capture.path.c_str(), L"wb") != 0) {
    capture.file = nullptr;
  }
#else
  capture.file = std::fopen(capture.path.c_str(), "wb");
#endif
  if (!capture.file) {
    REXLOG_ERROR("[HLE] cannot open {}", capture.path.string());
    return;
  }
  auto write_header = [](std::FILE* file) {
    std::setvbuf(file, nullptr, _IOFBF, 1 << 20);
    std::fwrite("GDTR", 4, 1, file);
    WriteU32(file, kTraceVersion);
    WriteU32(file, uint32_t(std::size(kBlocks)));
    for (const Block& block : kBlocks) {
      WriteU32(file, block.first);
      WriteU32(file, block.count);
    }
  };
  write_header(capture.file);
  if (HleRecordsEnabled()) {
    const std::filesystem::path records_path = folder / ("hle_rec_" + std::to_string(index) + ".bin");
#if defined(_WIN32)
    if (_wfopen_s(&capture.records_file, records_path.c_str(), L"wb") != 0) capture.records_file = nullptr;
#else
    capture.records_file = std::fopen(records_path.c_str(), "wb");
#endif
    if (capture.records_file) write_header(capture.records_file);
  }
  const int gpu_frames = std::max(1, REXCVAR_GET(crackdown_hle_capture_frames));
  // The game submits ahead of the GPU: record two more frames on this side so every GPU frame has its draws.
  capture.frames_left = uint32_t(gpu_frames + 2);
  capture.frame = 0;
  capture.sequence = 0;
  capture.draws = 0;
  capture.unparsed_calls = 0;
  capture.cursor = 0;
  capture.gap_dwords = 0;
  capture.gap_draws = 0;
  g_gpu_constants.Clear();
  const std::filesystem::path gpu_path = folder / ("hle_gpu_" + std::to_string(index) + ".bin");
  rex::cvar::SetFlagByName("gpu_draw_trace_frames", std::to_string(gpu_frames));
  rex::cvar::SetFlagByName("gpu_draw_trace", gpu_path.string());
  g_recording = true;
  REXLOG_WARN("[HLE] capturing {} game frames to {} and {} GPU frames to {}", capture.frames_left,
              capture.path.string(), gpu_frames, gpu_path.string());
}

}  // namespace crackdown

// The four D3D draw entry points (r3 = device): DrawIndexedVertices, DrawVertices, BeginVertices,
// BeginIndexedVertices.
void crackdown::hooks::D3dDrawIndexedVertices(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  if (crackdown::SingleTileSuppressed()) return;
  crackdown::CaptureDraw(ctx, base, false, [&] { original(ctx, base); }, crackdown::RecordKind::kDrawIndexed);
}

void crackdown::hooks::D3dDrawVertices(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  if (crackdown::SingleTileSuppressed()) return;
  crackdown::CaptureDraw(ctx, base, false, [&] { original(ctx, base); }, crackdown::RecordKind::kDraw);
}

void crackdown::hooks::D3dBeginVertices(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  // BeginVertices: every caller skips the vertex writes when it returns 0.
  if (crackdown::SingleTileSuppressed()) {
    ctx.r3.u64 = 0;
    return;
  }
  crackdown::CaptureDraw(ctx, base, true, [&] { original(ctx, base); }, crackdown::RecordKind::kBeginVertices);
}

void crackdown::hooks::D3dBeginIndexedVertices(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  if (crackdown::SingleTileSuppressed()) return;
  crackdown::CaptureDraw(ctx, base, true, [&] { original(ctx, base); }, crackdown::RecordKind::kBeginIndexedVertices);
}

// D3D calls that draw with their own state written straight into the ring (the walk applies it): the clear
// (r3 = device, its rect draws are DRAW_INDX_2), the resolve (an EDRAM copy draw), and SetShaderGPRAllocation (three
// INDIRECT_BUFFERs into the device's static block: setup, SQ_GPR_MANAGEMENT, 24 invisible points).
void crackdown::hooks::D3dClear(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  if (crackdown::SingleTileSuppressed()) return;
  crackdown::CaptureDraw(ctx, base, false, [&] { original(ctx, base); }, crackdown::RecordKind::kClear,
                         crackdown::Source::kClear);
}

void crackdown::hooks::D3dResolve(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  if (crackdown::SingleTileSuppressed()) return;
  crackdown::CaptureDraw(ctx, base, false, [&] { original(ctx, base); }, crackdown::RecordKind::kResolve,
                         crackdown::Source::kResolve);
}

void crackdown::hooks::D3dSetShaderGprAllocation(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  crackdown::CaptureDraw(ctx, base, false, [&] { original(ctx, base); }, crackdown::RecordKind::kGprFlush,
                         crackdown::Source::kGprFlush);
}

// The ring segment switch (sub_826BB408, r3 = device): its old end and new start, for the H1a ring walks.
void crackdown::hooks::D3dRingSegmentSwitch(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  const uint32_t device = ctx.r3.u32;
  const uint32_t old_end = crackdown::guest::Load32(base, device + crackdown::kWritePointer);
  original(ctx, base);
  crackdown::g_switch_old_end = old_end;
  crackdown::g_switch_new_start = crackdown::guest::Load32(base, device + crackdown::kWritePointer);
  ++crackdown::g_ring_switches;
}

// The scene's render passes: each record made while a pass function runs is tagged with that pass.
#define CRACKDOWN_DEFINE_RENDER_PASS(n)                                                             \
  void crackdown::hooks::RenderPass##n(PPCContext& ctx, uint8_t* base, GuestFunction original) {    \
    const uint32_t previous = crackdown::HleEnterRenderPass(n);                                    \
    crackdown::WriteHleMarker(base, n, 1);                                                         \
    original(ctx, base);                                                                           \
    crackdown::WriteHleMarker(base, n, 2);                                                         \
    crackdown::HleLeaveRenderPass(previous);                                                       \
  }
CRACKDOWN_RENDER_PASSES(CRACKDOWN_DEFINE_RENDER_PASS)
#undef CRACKDOWN_DEFINE_RENDER_PASS

// The generic object drawing the scene bracket sub_82326A90 also calls directly, outside any pass function: twice
// sub_823B4F20 (returning to 82326FCC and 8232727C) and once sub_823BA308 (82327848). Inside a pass, the pass keeps
// its tag.
void crackdown::hooks::RenderObjects(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  uint32_t pass = 0;
  if (crackdown::HleCurrentRenderPass() == 0) {
    switch (uint32_t(ctx.lr)) {
      case 0x82326FCC: pass = 26; break;
      case 0x8232727C: pass = 27; break;
      case 0x82327848: pass = 28; break;
      default: break;
    }
  }
  if (!pass) {
    original(ctx, base);
    return;
  }
  const uint32_t previous = crackdown::HleEnterRenderPass(pass);
  crackdown::WriteHleMarker(base, pass, 1);
  original(ctx, base);
  crackdown::WriteHleMarker(base, pass, 2);
  crackdown::HleLeaveRenderPass(previous);
}

// The HUD and interface drawing (reached through function pointers, outside the scene bracket).
void crackdown::hooks::RenderHud(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  if (crackdown::HleCurrentRenderPass() != 0) {
    original(ctx, base);
    return;
  }
  const uint32_t previous = crackdown::HleEnterRenderPass(29);
  crackdown::WriteHleMarker(base, 29, 1);
  original(ctx, base);
  crackdown::WriteHleMarker(base, 29, 2);
  crackdown::HleLeaveRenderPass(previous);
}

// RunCommandBuffer(r3 = device, r4 = command buffer, r5 = predicate select).
void crackdown::hooks::D3dRunCommandBuffer(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  const uint32_t device = ctx.r3.u32;
  const uint32_t pre = crackdown::guest::Load32(base, device + crackdown::kWritePointer);
  const uint32_t segment_end = crackdown::guest::Load32(base, device + crackdown::kSegmentEnd);
  if (!crackdown::g_recording.load(std::memory_order_relaxed)) {
    crackdown::CallWithRecord(ctx, base, crackdown::RecordKind::kCommandBuffer, [&] { original(ctx, base); });
    crackdown::SingleTileCommandBuffer(base, device, pre, segment_end);
    return;
  }
  const uint32_t command_buffer = ctx.r4.u32;
  {
    std::lock_guard lock(crackdown::g_capture.mutex);
    if (crackdown::g_capture.file) crackdown::WalkGap(base, device, pre, segment_end);
  }
  const crackdown::DrawRecord* record =
      crackdown::CallWithRecord(ctx, base, crackdown::RecordKind::kCommandBuffer, [&] { original(ctx, base); });
  crackdown::SingleTileCommandBuffer(base, device, pre, segment_end);
  std::lock_guard lock(crackdown::g_capture.mutex);
  if (crackdown::g_capture.file) {
    crackdown::g_source = crackdown::Source::kCommandBuffer;
    crackdown::g_pending_record = record;
    const uint32_t post = crackdown::guest::Load32(base, device + crackdown::kWritePointer);
    const bool same_segment = crackdown::guest::Load32(base, device + crackdown::kSegmentEnd) == segment_end;
    if (!crackdown::RecordCall(base, device, pre, post, same_segment, false)) {
      crackdown::RecordCommandBuffer(base, device, command_buffer);
    }
    crackdown::AdvanceCursor(base, device, post);
    crackdown::g_pending_record = nullptr;
    crackdown::g_source = crackdown::Source::kDraw;
  }
}
