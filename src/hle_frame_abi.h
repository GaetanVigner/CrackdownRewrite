#pragma once

#include <cstdint>

namespace crackdown {

// What the game passes to the GPU plugin's rex_gpu_hle_submit_frame once a frame (SDK patch 0129,
// rex/graphics/hle_frame.h, which holds the same struct; tests/vulkan_per_draw_tests checks they match): the frame's
// draw records and the block arena they reference. The plugin copies both before returning.
struct HleFrameAbi {
  uint32_t version;
  uint32_t struct_size;
  uint64_t frame;
  const void* records;
  uint32_t record_count;
  uint32_t record_size;
  const uint32_t* arena;
  uint32_t arena_dwords;
  uint32_t ucode_offset;  // byte offset in each record of {VS physical, VS dwords, PS physical, PS dwords}
  uint32_t state_offset;  // byte offset of {render state, fetch, bool/loop, VS pages[16], PS pages[16]} arena offsets
  uint32_t group_count;
  const uint32_t* groups;  // {first register, dwords} pairs: the render state block's layout
  const char* const* pass_names;  // pass_count + 1 names, index 0 the untagged records
  uint32_t pass_count;
  uint32_t draw_offset;  // byte offset in each record of {VGT_DRAW_INITIATOR, VGT_DMA_BASE, VGT_DMA_SIZE}
};
inline constexpr uint32_t kHleFrameAbiVersion = 4;

}  // namespace crackdown
