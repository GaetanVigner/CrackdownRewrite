#pragma once

#include <cstdint>
#include <initializer_list>

namespace crackdown {

// sub_82252150 (audio engine, AudioCore) reads a bit grid of 360 rows of 32 bytes (256 bits) at 0x82D33F98. The rows
// come from `fctiwz` of an angle, then `% 360`; the columns from `fctiwz` of a second value, then `>> 3` for the byte.
// The title itself reads a little past a row (column 256 is a normal last column), so values near the table are kept
// as they are. A NaN angle converts to 0x80000000, which gives row -128 and byte 0xF0000000, and the read lands 256 MB
// below the table.
inline constexpr int32_t kVisibilityGridRows = 360;
inline constexpr int32_t kVisibilityGridColumns = 256;
inline constexpr int32_t kVisibilityGridRowBytes = kVisibilityGridColumns / 8;
inline constexpr int64_t kVisibilityGridBytes = int64_t{kVisibilityGridRows} * kVisibilityGridRowBytes;
// Reads this close to the table stay in the title's own data, as on the console.
inline constexpr int64_t kVisibilityGridSlackBytes = 0x10000;

struct VisibilityGridSpan {
  int32_t first_row;
  int32_t last_row;
  int32_t first_column;
  int32_t last_column;
};

constexpr int64_t VisibilityGridByteOffset(int32_t row, int32_t column) {
  return int64_t{row} * kVisibilityGridRowBytes + (column >> 3);
}

// True when every corner of the span reads within the slack around the table: the title's own behavior is kept.
constexpr bool VisibilityGridSpanNearTable(const VisibilityGridSpan& span) {
  for (const int32_t row : {span.first_row, span.last_row}) {
    for (const int32_t column : {span.first_column, span.last_column}) {
      const int64_t offset = VisibilityGridByteOffset(row, column);
      if (offset < -kVisibilityGridSlackBytes || offset >= kVisibilityGridBytes + kVisibilityGridSlackBytes) {
        return false;
      }
    }
  }
  return true;
}

// For a span that would read far from the table: a row outside the grid becomes 0 and a column is clamped into it.
constexpr VisibilityGridSpan ClampVisibilityGridSpan(VisibilityGridSpan span) {
  auto row = [](int32_t v) { return v >= 0 && v < kVisibilityGridRows ? v : 0; };
  auto column = [](int32_t v) { return v < 0 ? 0 : v >= kVisibilityGridColumns ? kVisibilityGridColumns - 1 : v; };
  return {row(span.first_row), row(span.last_row), column(span.first_column), column(span.last_column)};
}

}  // namespace crackdown
