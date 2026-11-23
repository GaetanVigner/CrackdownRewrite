#include "any360/host/rexglue.h"
#include "frame_rate.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>

#include <rex/hook.h>
#include <rex/kernel/xboxkrnl/video.h>
#include <rex/ppc.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>

#include "guest_hooks.h"
#include "guest_layout.h"
#include "platform.h"
#include "settings.h"
#include "window_title.h"

// The SDK's VdSwap body, exported by rexruntime.dll. The SDK has no header for it.
namespace rex::kernel::xboxkrnl {

void VdSwap_entry(mapped_void buffer_ptr, mapped_void fetch_ptr, mapped_void unk2, mapped_void unk3,
                  mapped_void unk4, mapped_u32 frontbuffer_ptr, mapped_u32 texture_format_ptr,
                  mapped_u32 color_space_ptr, mapped_u32 width, mapped_u32 height);

}  // namespace rex::kernel::xboxkrnl

namespace crackdown {
namespace {

std::atomic<bool> g_active{false};
rex::ui::Window* g_window = nullptr;

// UI thread only.
std::string g_shown_title;

// The guest video mode is the picture the game draws; Render resolution multiplies it.
WindowSize RenderResolution() {
  rex::system::X_VIDEO_MODE mode{};
  rex::kernel::xboxkrnl::VdQueryVideoMode(&mode);
  const int scale = std::max(1, std::atoi(rex::cvar::GetFlagByName("resolution_scale").c_str()));
  return {int(uint32_t(mode.display_width)) * scale, int(uint32_t(mode.display_height)) * scale};
}

// Rebuilds the whole title, so each update keeps the parts it does not change.
void UpdateTitle() {
  if (!g_active.load(std::memory_order_relaxed)) {
    return;
  }
  std::optional<WindowSize> render_resolution;
  if (REXCVAR_GET(crackdown_show_render_resolution)) {
    render_resolution = RenderResolution();
  }
  const std::string tag = REXCVAR_GET(crackdown_window_tag);
  const std::string game = tag.empty() ? std::string(kWindowTitle) : std::string(kWindowTitle) + " [" + tag + "]";
  const std::string title = ComposeWindowTitle(game, render_resolution);
  if (title != g_shown_title) {
    g_shown_title = title;
    g_window->SetTitle(title);
  }
}


}  // namespace

void AttachFrameRate(rex::ui::Window* window, rex::ui::WindowedAppContext& app_context) {
  g_window = window;
  g_active.store(true, std::memory_order_relaxed);
  app_context.CallInUIThread(UpdateTitle);
}

void DetachFrameRate() {
  g_active.store(false, std::memory_order_relaxed);
}

}  // namespace crackdown

// Named like the SDK's export, so REX_EXTERN would call this hook again: the original is called by its entry point.
REX_HOOK_RAW(__imp__VdSwap) {
  // Test branch any360-graphics-test: with the GPU bound to Any360, its VdSwap builds the swap packets.
  if (any360::host::rexglue::GraphicsBound()) {
    any360_VdSwap(ctx, base);
  } else {
    rex::ppc::HostToGuestFunction<rex::kernel::xboxkrnl::VdSwap_entry>(ctx, base);
  }
}
