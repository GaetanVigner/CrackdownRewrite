#include "character_step_fixes.h"

#include <rex/ppc.h>

#include "frame_pacing.h"

// Codegen hook before the `fdivs f0,f0,f20` at 822B5634 in sub_822B4D40 (config/overrides.toml): f0 holds
// clamp(h, 0, 2) * 0.4 and f20 the step's dt; the quotient, negated, is the snap-down velocity.
void crackdown_ledge_snap_down(PPCRegister& height_pull) {
  height_pull.f64 =
      double(crackdown::ScaleLedgeSnapDown(float(height_pull.f64), crackdown::OriginalStepsPerUpdate()));
}

// Codegen hook before the `fmuls f0,f0,f13` at 822A2DB8 in sub_822A2C90: f13 is the speed decay of the blend at +4224.
void crackdown_ground_snap_decay(PPCRegister& decay) {
  decay.f64 = double(crackdown::ScaleGroundSnapDecay(float(decay.f64), crackdown::OriginalStepsPerUpdate()));
}

// Codegen hook after the `fmuls f13,f13,f0` at 822A2E28 in sub_822A2C90: f13 is the fraction of the gap to the
// smoothed height covered this step.
void crackdown_ground_snap_lerp(PPCRegister& fraction) {
  fraction.f64 = double(crackdown::ScaleGroundSnapLerp(float(fraction.f64), crackdown::OriginalStepsPerUpdate()));
}

// Codegen hooks after `addi r11,r11,10` at 822E191C (sub_822E1908, hit-reaction merge window) and `addi r10,r11,15` at
// 822BA48C (sub_822BA470, ragdoll threshold): the register holds frame counter + ticks.
void crackdown_hit_merge_window(PPCRegister& deadline) {
  constexpr uint32_t kTicks = 10;
  const uint32_t ticks = crackdown::ScaleTickWindow(kTicks, crackdown::OriginalStepsPerUpdate());
  deadline.u64 = uint32_t(deadline.u32 - kTicks + ticks);
}

void crackdown_ragdoll_tick_window(PPCRegister& deadline) {
  constexpr uint32_t kTicks = 15;
  const uint32_t ticks = crackdown::ScaleTickWindow(kTicks, crackdown::OriginalStepsPerUpdate());
  deadline.u64 = uint32_t(deadline.u32 - kTicks + ticks);
}

// Codegen hook after `fmuls f0,f0,f23` at 822AA8F4 in sub_822A9E78: f0 = gravity * dt, f13 = the vertical speed
// accumulator (+532) loaded at 822AA8E0, 0 after a grounded step.
void crackdown_first_gravity_step(PPCRegister& gravity_step, PPCRegister& accumulated) {
  gravity_step.f64 = double(crackdown::ScaleFirstGravityStep(float(gravity_step.f64), float(accumulated.f64),
                                                             crackdown::OriginalStepsPerUpdate()));
}
