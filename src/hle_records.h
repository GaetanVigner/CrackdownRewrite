#pragma once

#include <cstdint>

namespace crackdown {

// HLE renderer H1a (HLE_RENDERER_PLAN.md, "H1 design"): with crackdown_hle_records on, every D3D call that flushes
// state (the four draw functions, clear, resolve, RunCommandBuffer, SetShaderGPRAllocation) is turned into a draw
// record before the call runs, read from the device's shadow registers while their dirty masks still say what
// changed since the previous flush. Records reference immutable state blocks (render state, fetch constants,
// bool/loop constants, pages of 16 float constants) that consecutive records share until the game changes them.
// Observe only: the call still runs and the PM4 path renders the frame.

enum class RecordKind : uint32_t {
  kDraw = 1,
  kDrawIndexed = 2,
  kBeginVertices = 3,
  kBeginIndexedVertices = 4,
  kCommandBuffer = 5,
  kClear = 6,
  kResolve = 7,
  kGprFlush = 8,
};

struct DrawRecord {
  static constexpr uint32_t kConstantPages = 16;  // 16 pages of 16 float4 per shader stage

  RecordKind kind;
  uint32_t args[4];  // r4-r7 of the call
  uint32_t vertex_shader;
  uint32_t pixel_shader;
  // Offsets of the state blocks in the frame's block arena.
  uint32_t render_state;
  uint32_t fetch;
  uint32_t bool_loop;
  uint32_t vs_pages[kConstantPages];
  uint32_t ps_pages[kConstantPages];
};

// Whether crackdown_hle_records is on.
bool HleRecordsEnabled();

// The device's float constant dirty masks (VS, PS), read before a call flushes and clears them.
struct HleRecordMasks {
  uint64_t vs = 0;
  uint64_t ps = 0;
};
HleRecordMasks HleReadRecordMasks(uint8_t* base, uint32_t device);

// Builds the record of a call (r3 = device) right after it ran, from the state it flushed (the flush itself derives
// some registers, such as 0x2203, and some constants) and the masks read before it. Returns the record, valid until
// the next call on this thread or the next frame, or nullptr when records are off.
const DrawRecord* HleRecordCall(uint8_t* base, uint32_t device, RecordKind kind, const uint32_t args[4],
                                const HleRecordMasks& masks);

// Writes a record's state in the GDTR block layout (render state 0x2000-0x23FF, float constants 0x4000-0x47FF, fetch
// 0x4800-0x48BF, bool/loop 0x4900-0x4927); registers no shadow group covers are left as they are.
void HleExpandRecord(const DrawRecord& record, uint32_t* values);

// Called at every main-loop frame of the title: frees the frame's records and logs the counters every 10 s.
void HleRecordsFrame();

}  // namespace crackdown
