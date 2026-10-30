// The one-for-one match of a pass's emulated draws to its draw records (src/hle_draw_match.h), which the GPU plugin
// uses to draw each record natively at its emulated draw: identical sequences, records the emulation skips (resolves
// in copy mode), repeated index buffers told apart by their draw initiator, draws with no record, a later partial
// frame extending the records, and the patch holding the same code.
// Run: hle_draw_match_tests

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "check.h"
#include "hle_draw_match.h"

namespace {

using crackdown::hle::DrawKey;
using crackdown::hle::DrawMatcher;

std::vector<size_t> Indices(size_t count, size_t first = 100) {
  std::vector<size_t> out(count);
  for (size_t i = 0; i < count; ++i) out[i] = first + i;
  return out;
}

void IdenticalSequence() {
  std::mt19937 rng(1);
  std::vector<DrawKey> keys(300);
  for (DrawKey& key : keys) key = {rng() & 0x1FFFFFFC, 4 | (rng() & 0xFFFF0000)};
  DrawMatcher matcher;
  matcher.Reset(keys, Indices(keys.size()));
  for (size_t i = 0; i < keys.size(); ++i) CHECK(matcher.Match(keys[i]) == 100 + i);
  CHECK(matcher.matched() == 300 && matcher.skipped() == 0 && matcher.unmatched() == 0);
}

void SkipsRecordsNotDrawn() {
  // Records 2 and 5 are resolves the emulation turns into copies, not draws.
  std::vector<DrawKey> keys = {{0x1000, 4}, {0x2000, 4}, {0x0, 0x86}, {0x3000, 4}, {0x4000, 4}, {0x0, 0x86}, {0x5000, 4}};
  DrawMatcher matcher;
  matcher.Reset(keys, Indices(keys.size()));
  for (size_t i : {0, 1, 3, 4, 6}) CHECK(matcher.Match(keys[i]) == 100 + i);
  CHECK(matcher.skipped() == 2 && matcher.unmatched() == 0);
}

void SameIndexBufferTwice() {
  // One mesh drawn twice with different index counts (initiator bits 16-31): each draw finds its own record.
  std::vector<DrawKey> keys = {{0x8000, 4 | (30u << 16)}, {0x8000, 4 | (12u << 16)}, {0x8000, 4 | (30u << 16)}};
  DrawMatcher matcher;
  matcher.Reset(keys, Indices(keys.size()));
  CHECK(matcher.Match(keys[1]) == 101);  // the first record is passed over
  CHECK(matcher.Match(keys[2]) == 102);
  CHECK(matcher.skipped() == 1);
}

void TwinsToldApartByConstants() {
  // Two characters with one mesh: same index buffer and initiator, their own bone constants. The first one has no
  // record (a draw the records miss): its draw must not take the second one's record.
  const uint32_t bones_a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  const uint32_t bones_b[8] = {1, 2, 3, 4, 5, 6, 7, 9};
  const DrawKey a = {0x8000, 4 | (90u << 16), crackdown::hle::HashDwords(bones_a, 8)};
  const DrawKey b = {0x8000, 4 | (90u << 16), crackdown::hle::HashDwords(bones_b, 8)};
  CHECK(a.constants != b.constants);
  DrawMatcher matcher;
  matcher.Reset({b}, Indices(1));
  CHECK(matcher.Match(a) == DrawMatcher::kNoRecord);
  CHECK(matcher.Match(b) == 100);
}

void DrawWithoutRecord() {
  std::vector<DrawKey> keys = {{0x1000, 4}, {0x2000, 4}};
  DrawMatcher matcher;
  matcher.Reset(keys, Indices(keys.size()));
  CHECK(matcher.Match({0x9999000, 4}) == DrawMatcher::kNoRecord);
  CHECK(matcher.cursor() == 0);  // an unmatched draw leaves the cursor
  CHECK(matcher.Match(keys[0]) == 100);
  CHECK(matcher.unmatched() == 1 && matcher.matched() == 1);
}

void BeyondLookahead() {
  std::vector<DrawKey> keys(DrawMatcher::kLookahead + 10, DrawKey{0x1000, 4});
  keys.back() = {0x7000, 4};
  DrawMatcher matcher;
  matcher.Reset(keys, Indices(keys.size()));
  CHECK(matcher.Match(keys.back()) == DrawMatcher::kNoRecord);
}

void ExtendedByPartialFrame() {
  // The first partial frame holds the first cascade; the next one (the pass called again) adds the second.
  std::vector<DrawKey> first = {{0x1000, 4}, {0x2000, 4}};
  std::vector<DrawKey> both = {{0x1000, 4}, {0x2000, 4}, {0x0, 0x86}, {0x3000, 4}};
  DrawMatcher matcher;
  matcher.Reset(first, Indices(first.size()));
  CHECK(matcher.Match(first[0]) == 100 && matcher.Match(first[1]) == 101);
  matcher.Extend(both, Indices(both.size()));
  CHECK(matcher.Match(both[3]) == 103);
  CHECK(matcher.skipped() == 1);
}

void PatchHoldsTheSameMatcher() {
  // The plugin's copy (patch 0129) must keep the matching rule: same key, lookahead and cursor moves.
  for (const auto& entry : std::filesystem::directory_iterator(CRACKDOWN_PATCH_DIR)) {
    const std::string name = entry.path().filename().string();
    if (name.rfind("0129-", 0) != 0) continue;
    const std::string patch = check::ReadText(entry.path());
    CHECK(patch.find("+  static constexpr size_t kLookahead = 64;") != std::string::npos);
    CHECK(patch.find("+      if (keys_[i] == draw) {") != std::string::npos);
    CHECK(patch.find("+    return dma_base == other.dma_base && initiator == other.initiator && constants == other.constants;") !=
          std::string::npos);
    CHECK(patch.find("+        cursor_ = i + 1;") != std::string::npos);
    return;
  }
  CHECK(false);
}

}  // namespace

int main() {
  IdenticalSequence();
  SkipsRecordsNotDrawn();
  SameIndexBufferTwice();
  TwinsToldApartByConstants();
  DrawWithoutRecord();
  BeyondLookahead();
  ExtendedByPartialFrame();
  PatchHoldsTheSameMatcher();
  return check::Finish("hle_draw_match_tests");
}
