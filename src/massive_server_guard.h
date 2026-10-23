#pragma once

#include <cstdint>

namespace crackdown {

// The Massive server query asks XTitleServerCreateEnumerator for this many servers and enumerates them into a stack
// array of that many 208-byte records, then walks as many records as XamEnumerate reports.
inline constexpr uint32_t kMassiveServerRecords = 10;

constexpr uint32_t ClampMassiveServerCount(uint32_t count) {
  return count > kMassiveServerRecords ? kMassiveServerRecords : count;
}

}  // namespace crackdown
