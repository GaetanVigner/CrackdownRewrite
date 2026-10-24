#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "guest_layout.h"

namespace crackdown::nan_guard {

struct CameraField {
  uint32_t offset;
  const char* name;
};

inline constexpr std::size_t kCameraFieldCount = 8;

// The basis and position sit at the start of the camera in every guest; the follow state after them moves.
constexpr std::array<CameraField, kCameraFieldCount> CameraFields(const GuestLayout& layout) {
  return {{{272, "basis x"},
           {288, "basis y"},
           {304, "basis z"},
           {320, "position"},
           {layout.camera_velocity, "velocity"},
           {layout.camera_target, "target"},
           {layout.camera_previous_target, "previous target"},
           {layout.camera_listener_point, "listener point"}}};
}

// The camera update writes through the target matrix and the follow state; the layout's size covers them.
inline constexpr uint32_t kMaxCameraBytes = std::max(kLayoutBase.camera_bytes, kLayoutTitleUpdate6.camera_bytes);

constexpr bool CameraFieldsFit(const GuestLayout& layout) {
  const auto fields = CameraFields(layout);
  return fields.back().offset + 12 <= layout.camera_bytes && layout.camera_bytes <= kMaxCameraBytes;
}

static_assert(CameraFieldsFit(kLayoutBase));
static_assert(CameraFieldsFit(kLayoutTitleUpdate6));

inline bool XyzFinite(const float* xyz) {
  return std::isfinite(xyz[0]) && std::isfinite(xyz[1]) && std::isfinite(xyz[2]);
}

}  // namespace crackdown::nan_guard
