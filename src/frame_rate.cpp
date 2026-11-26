#include "any360/host/rexglue.h"
#include "frame_rate.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#endif

#include <any360/gfx/frame_counter.h>
#include <rex/hook.h>
#include <rex/logging.h>
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

uint64_t PrivateBytes() {
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS_EX counters{};
  if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                              sizeof(counters))) {
    return counters.PrivateUsage;
  }
#endif
  return 0;
}

// The game's swaps over crackdown_frame_log_seconds: one log line with the rate, the median frame time and the
// 10% / 1% lows (Any360's percentiles), the slowest frame and the private memory. The same counter whatever renderer
// runs, so a route on ReXGlue's GPU and on Any360's compares line for line (Any360's own report counts the frames it
// presents, ReXGlue has none: docs/perf/2026-11-26-any360-vs-rexglue). Swap thread only.
std::vector<float> g_period_ms;
std::optional<std::chrono::steady_clock::time_point> g_period_start, g_period_last;

void LogFrameTimes() {
  const int seconds = REXCVAR_GET(crackdown_frame_log_seconds);
  if (seconds <= 0) return;
  const auto now = std::chrono::steady_clock::now();
  if (!g_period_start) {
    g_period_start = g_period_last = now;
    g_period_ms.reserve(4096);
    return;
  }
  g_period_ms.push_back(std::chrono::duration<float, std::milli>(now - *g_period_last).count());
  g_period_last = now;
  const double elapsed = std::chrono::duration<double>(now - *g_period_start).count();
  if (elapsed < seconds) return;
  const double fps = g_period_ms.size() / elapsed;
  const float slowest = *std::max_element(g_period_ms.begin(), g_period_ms.end());
  const any360::gfx::FrameTimeSummary f = any360::gfx::SummarizeFrameTimes(g_period_ms);
  REXLOG_INFO("Crackdown: guest frames: {:.1f} FPS over {:.1f} s; frame times: median {:.2f} ms ({:.1f} FPS), 10% low "
              "{:.1f} FPS, 1% low {:.1f} FPS; slowest frame {:.1f} ms; {} MB private",
              fps, elapsed, f.median_ms, 1e3 / f.median_ms, 1e3 / f.p90_ms, 1e3 / f.p99_ms, slowest,
              PrivateBytes() >> 20);
  g_period_ms.clear();
  g_period_start = now;
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
  crackdown::LogFrameTimes();
}
