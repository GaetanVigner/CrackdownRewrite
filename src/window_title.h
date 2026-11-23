#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "aspect_ratio.h"

namespace crackdown {

// The game, then the render resolution when it is shown. The frame rate is Any360's frame counter
// (--any360_fps_overlay), drawn over the picture for every title.
inline std::string ComposeWindowTitle(std::string_view game, std::optional<WindowSize> render_resolution) {
  std::string title(game);
  if (render_resolution) {
    title += " | " + std::to_string(render_resolution->width) + "x" + std::to_string(render_resolution->height);
  }
  return title;
}

}  // namespace crackdown
