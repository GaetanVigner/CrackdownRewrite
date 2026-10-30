#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// HLE renderer H2: the emulated draws of a render pass, in command stream order, matched one for one to the pass's
// draw records. Each emulated draw takes the next record from the cursor with the same index buffer (VGT_DMA_BASE)
// and draw initiator (primitive type, index source and count); records passed over on the way (draws the emulation
// did not issue, such as resolves in copy mode) are counted. Shared by the GPU plugin (patch 0129 holds the same
// code) and its tests.
namespace crackdown::hle {

struct DrawKey {
  uint32_t dma_base = 0;
  uint32_t initiator = 0;
  // A hash of the vertex shader float constants (c0-c255, as the register file holds them): one mesh drawn for two
  // characters has the same index buffer and initiator but its own bone matrices.
  uint32_t constants = 0;
  bool operator==(const DrawKey& other) const {
    return dma_base == other.dma_base && initiator == other.initiator && constants == other.constants;
  }
};

// FNV-1a over dwords (the vertex shader constants of DrawKey).
inline uint32_t HashDwords(const uint32_t* values, size_t count) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < count; ++i) {
    hash = (hash ^ values[i]) * 16777619u;
  }
  return hash;
}

class DrawMatcher {
 public:
  static constexpr size_t kNoRecord = SIZE_MAX;
  // How far ahead of the cursor a draw is looked for before it counts as unmatched.
  static constexpr size_t kLookahead = 64;

  void Reset(std::vector<DrawKey> records, std::vector<size_t> indices) {
    keys_ = std::move(records);
    indices_ = std::move(indices);
    cursor_ = 0;
    matched_ = skipped_ = unmatched_ = 0;
  }
  // New records for the same pass (a later partial frame): the cursor stays where it is.
  void Extend(std::vector<DrawKey> records, std::vector<size_t> indices) {
    keys_ = std::move(records);
    indices_ = std::move(indices);
  }
  // The record (its index in the frame) an emulated draw corresponds to, or kNoRecord.
  size_t Match(const DrawKey& draw) {
    const size_t end = keys_.size() < cursor_ + kLookahead ? keys_.size() : cursor_ + kLookahead;
    for (size_t i = cursor_; i < end; ++i) {
      if (keys_[i] == draw) {
        skipped_ += uint32_t(i - cursor_);
        cursor_ = i + 1;
        ++matched_;
        return indices_[i];
      }
    }
    ++unmatched_;
    return kNoRecord;
  }
  uint32_t matched() const { return matched_; }
  uint32_t skipped() const { return skipped_; }
  uint32_t unmatched() const { return unmatched_; }
  size_t cursor() const { return cursor_; }

 private:
  std::vector<DrawKey> keys_;
  std::vector<size_t> indices_;
  size_t cursor_ = 0;
  uint32_t matched_ = 0, skipped_ = 0, unmatched_ = 0;
};

}  // namespace crackdown::hle
