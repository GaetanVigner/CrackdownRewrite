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
  uint32_t reserved;
};
inline constexpr uint32_t kHleFrameAbiVersion = 1;

}  // namespace crackdown
