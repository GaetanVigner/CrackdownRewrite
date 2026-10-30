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
  uint32_t pass;
  uint32_t args[4];  // r4-r7 of the call
  uint32_t vertex_shader;
  uint32_t pixel_shader;
  // Offsets of the state blocks in the frame's block arena.
  uint32_t render_state;
  uint32_t fetch;
  uint32_t bool_loop;
  uint32_t vs_pages[kConstantPages];
  uint32_t ps_pages[kConstantPages];
  // BeginVertices: the vertices the game writes into the ring after the call (guest physical address, dwords).
  uint32_t up_vertices;
  uint32_t up_dwords;
  // RunCommandBuffer: the buffer's segments the call replays (INDIRECT_BUFFER guest physical address, dwords).
  static constexpr uint32_t kMaxSegments = 8;
  uint32_t segment_count;
  uint32_t segments[kMaxSegments][2];
  // The bound shaders' microcode as the runtime's IM_LOAD sends it (sub_826C6C48): guest physical address and
  // dwords, vertex then pixel; 0 when no shader is bound.
  uint32_t ucode[4];
  // The draw packet the call wrote last to the ring (DRAW_INDX or DRAW_INDX_2): VGT_DRAW_INITIATOR, then VGT_DMA_BASE
  // and VGT_DMA_SIZE when its indices come by DMA (the index buffer, which no state group carries); all 0 when the
  // call wrote none (BeginVertices draws at EndVertices).
  uint32_t draw[3];
};

// The render pass each record belongs to (0: none of the tagged passes), set by the pass hooks for the duration of the
// pass function; nested passes tag their own records.
inline constexpr uint32_t kRenderPassCount = 29;
const char* RenderPassName(uint32_t pass);
uint32_t HleEnterRenderPass(uint32_t pass);  // returns the pass to restore
uint32_t HleCurrentRenderPass();
void HleLeaveRenderPass(uint32_t previous);

// Whether crackdown_hle_records is on.
bool HleRecordsEnabled();

// The device's float constant dirty masks (VS, PS), read before a call flushes and clears them.
struct HleRecordMasks {
  uint64_t vs = 0;
  uint64_t ps = 0;
  uint32_t fetch = 0;  // slot 0 the most significant bit
  uint64_t state = 0;  // +32: render state 0x2300 + k at bit 37 - k, ...
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

// A constant write the device's flush does not make (big-endian values; float constants in dwords, VS c0 = 0, PS
// c0 = 1024; fetch constants in dwords), by when it reaches the GPU: PM4 written between hooked calls (before the
// next call's flush), a shader's literal loads during a call (after its flush, before its record), or a replayed
// command buffer's own writes (after the call's record: they apply at once).
enum class HleStateSpace { kFloat, kFetch, kRegister };  // kRegister: from 0x2000
enum class HleWritePhase { kBeforeFlush, kDuringCall, kAfterRecord };
void HleNoteStateWrite(HleStateSpace space, uint32_t first, uint32_t dwords, const uint8_t* values,
                       HleWritePhase phase);

// Adds to the record just built: BeginVertices' vertex range, and a replayed command buffer segment.
void HleNoteUpVertices(uint8_t* base, uint32_t device, uint32_t physical, uint32_t dwords);
void HleNoteSegment(uint32_t physical, uint32_t dwords);

// Diagnostics: a predicated packet the walk ran (passes) or skipped, with the bin mask and select it evaluated.
void HleNotePredicated(bool passes, uint64_t bin_mask, uint64_t bin_select);

// Diagnostics: an indirect buffer the walk took from its decoded cache (hit) or decoded.
void HleNoteDecodedBuffer(bool hit);

// Diagnostics: the guest return address of a record no render pass tags (logged, the most frequent first).
void HleNoteUntaggedCaller(uint32_t caller);

// Called at every main-loop frame of the title: frees the frame's records and logs the counters every 10 s.
void HleRecordsFrame();

}  // namespace crackdown
