// Compatibility guards for title audio: a null-handle bypass for one audio teardown caller, and the escape that
// lets the audio workers leave their rendezvous when the title is terminated.

#include <cstring>

#include <rex/hook.h>
#include <rex/system/kernel_state.h>

#include "guest_hooks.h"
#include "guest_layout.h"

namespace {

// The slot teardown selects its no-release path through the out byte, which also skips the slot's own idle reset
// (offsets 64, 70-73), so the reset is done here.
void SkipNullAudioObject(uint8_t* base, uint32_t out_flag, uint32_t slot) {
  *rex::memory::GuestPtr<uint8_t*>(base, out_flag) = 1;
  std::memset(rex::memory::GuestPtr<uint8_t*>(base, slot + 12), 0, sizeof(uint32_t));
  *rex::memory::GuestPtr<uint8_t*>(base, slot + 64) = 0;
  *rex::memory::GuestPtr<uint8_t*>(base, slot + 70) = 0;
  *rex::memory::GuestPtr<uint8_t*>(base, slot + 71) = 0;
  *rex::memory::GuestPtr<uint8_t*>(base, slot + 72) = 0;
  *rex::memory::GuestPtr<uint8_t*>(base, slot + 73) = 1;
}

}  // namespace

// Other callers of the handle release keep the original call.
void crackdown::hooks::AudioReleaseHandle(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  if (!ctx.r3.u32 && uint32_t(ctx.lr) == crackdown::Layout().null_audio_return) {
    SkipNullAudioObject(base, ctx.r4.u32, ctx.r31.u32);
    ctx.r3.u64 = 0;
    return;
  }
  original(ctx, base);
}

// Codegen hook at the spin of the XAudio rendezvous, also polled by the host rendezvous in audio_guest_sync.cpp. The audio workers wait there for each other without a kernel wait, so one whose partner
// already stopped for title termination would wait forever.
bool crackdown_audio_barrier_escape() {
  const auto* kernel = rex::system::kernel_state();
  return kernel && kernel->is_terminating_title();
}
