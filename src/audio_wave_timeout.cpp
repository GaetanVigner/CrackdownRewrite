#include "audio_wave_timeout.h"

#include <rex/hook.h>
#include <rex/ppc.h>

#include "frame_pacing.h"
#include "guest_hooks.h"

namespace {

crackdown::WaveTimeoutClock g_clock;

}  // namespace

// Wave instance setup: r3 is the pooled instance.
void crackdown::hooks::WaveInstanceSetup(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  g_clock.Reset(ctx.r3.u32);
  original(ctx, base);
}

// Codegen hook after the `addi r11,r11,1` that advances a wave instance's elapsed count: r11 is the count and r30 the
// instance. The compare against the deadline and the stop stay as shipped.
void crackdown_audio_wave_timeout_tick(PPCRegister& elapsed, PPCRegister& instance) {
  elapsed.u64 = elapsed.u32 - 1u + g_clock.Tick(instance.u32, crackdown::OriginalStepsPerUpdate());
}
