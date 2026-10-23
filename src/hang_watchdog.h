#pragma once

#include <filesystem>

#include <rex/cvar.h>

#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

namespace crackdown {

// Counts a main-loop frame of the title (called from the frame hook).
void NoteGuestFrame();

// Starts a thread that watches the window (native handle) and the title's frames. When the window stops answering
// messages, or the title stops producing frames, for several seconds, it writes the call stack of every thread to
// <logs_folder>/hang_<n>.txt and logs the path. Windows only; a no-op elsewhere.
void StartHangWatchdog(void* native_window, const std::filesystem::path& logs_folder);

// The logs folder StartHangWatchdog was given, where the developer diagnostics write their files (empty before).
const std::filesystem::path& DiagnosticsLogsFolder();

// Developer profiling: samples the call stacks of every thread using at least 5% of a core, about 1000 times a second
// for `seconds`, then writes the functions they spent their time in to profile_<n>.txt in the logs folder. False when
// a profile is already running (or off Windows).
bool StartThreadProfile(double seconds);

// Whether crackdown_debug_profiler is on.
bool ThreadProfilerEnabled();

// With crackdown_debug_profiler on, F6 starts a 10 s profile (F10 is the cheat menu). A held key is one press, and the
// game never sees it.
class ProfileKey final : public rex::ui::WindowInputListener {
 public:
  static constexpr size_t kZOrder = 1000;
  static constexpr double kSeconds = 10.0;

  explicit ProfileKey(rex::ui::Window* window) : window_(window) { window_->AddInputListener(this, kZOrder); }
  ~ProfileKey() override { window_->RemoveInputListener(this); }

  ProfileKey(const ProfileKey&) = delete;
  ProfileKey& operator=(const ProfileKey&) = delete;

  void OnKeyDown(rex::ui::KeyEvent& e) override {
    if (!IsF6(e)) {
      return;
    }
    e.set_handled(true);
    if (!e.prev_state()) {
      StartThreadProfile(kSeconds);
    }
  }

  void OnKeyUp(rex::ui::KeyEvent& e) override {
    if (IsF6(e)) {
      e.set_handled(true);
    }
  }

 private:
  bool IsF6(const rex::ui::KeyEvent& e) const {
    return ThreadProfilerEnabled() && e.virtual_key() == rex::ui::VirtualKey::kF6 && e.target() == window_;
  }

  rex::ui::Window* window_;
};

// With crackdown_debug_profiler on, F5 cycles the HLE renderer's milestone 3 switches for an A/B/C at one spot: off,
// gpu_cached_indirect_buffers (SDK patch 0107), then also gpu_fetch_constant_caches (0109). The GPU log notes the
// indirect buffer cache's switches. The game never sees the key.
class IndirectBufferCacheKey final : public rex::ui::WindowInputListener {
 public:
  static constexpr size_t kZOrder = 1000;

  explicit IndirectBufferCacheKey(rex::ui::Window* window) : window_(window) {
    window_->AddInputListener(this, kZOrder);
  }
  ~IndirectBufferCacheKey() override { window_->RemoveInputListener(this); }

  IndirectBufferCacheKey(const IndirectBufferCacheKey&) = delete;
  IndirectBufferCacheKey& operator=(const IndirectBufferCacheKey&) = delete;

  void OnKeyDown(rex::ui::KeyEvent& e) override {
    if (!IsF5(e)) {
      return;
    }
    e.set_handled(true);
    if (!e.prev_state()) {
      const bool cache = rex::cvar::GetFlagByName("gpu_cached_indirect_buffers") == "true";
      const bool fetch = rex::cvar::GetFlagByName("gpu_fetch_constant_caches") == "true";
      const bool next_cache = !cache || !fetch;
      const bool next_fetch = cache && !fetch;
      rex::cvar::SetFlagByName("gpu_cached_indirect_buffers", next_cache ? "true" : "false");
      rex::cvar::SetFlagByName("gpu_fetch_constant_caches", next_fetch ? "true" : "false");
    }
  }

  void OnKeyUp(rex::ui::KeyEvent& e) override {
    if (IsF5(e)) {
      e.set_handled(true);
    }
  }

 private:
  bool IsF5(const rex::ui::KeyEvent& e) const {
    return ThreadProfilerEnabled() && e.virtual_key() == rex::ui::VirtualKey::kF5 && e.target() == window_;
  }

  rex::ui::Window* window_;
};

}  // namespace crackdown
