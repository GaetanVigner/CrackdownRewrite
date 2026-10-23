#include "character_step_fixes.h"

#include <rex/ppc.h>
#include <rex/system/kernel_state.h>

#include "frame_pacing.h"
#include "guest_layout.h"
#include "guest_memory.h"

namespace {

uint8_t* GuestBase() {
  return rex::system::kernel_state()->memory()->virtual_membase();
}

}  // namespace

// Codegen hook before the `fdivs` of the ledge snap: f0 holds clamp(h, 0, 2) * 0.4 and the divisor is the step's dt; the
// negated quotient is the snap-down velocity.
void crackdown_ledge_snap_down(PPCRegister& height_pull) {
  height_pull.f64 =
      double(crackdown::ScaleLedgeSnapDown(float(height_pull.f64), crackdown::OriginalStepsPerUpdate()));
}

// Codegen hooks in the ground snap: before the `fmuls f0,f0,f13` f13 is the blend's speed decay, after the
// `fmuls f13,f13,f0` f13 is the fraction of the gap closed this step.
void crackdown_ground_snap_decay(PPCRegister& decay) {
  decay.f64 = double(crackdown::ScaleGroundSnapDecay(float(decay.f64), crackdown::OriginalStepsPerUpdate()));
}

void crackdown_ground_snap_lerp(PPCRegister& fraction) {
  fraction.f64 = double(crackdown::ScaleGroundSnapLerp(float(fraction.f64), crackdown::OriginalStepsPerUpdate()));
}

// Codegen hooks after the `addi` that adds the hit-reaction merge window (the layout's ticks) and the one that adds the
// ragdoll threshold (15 ticks): the register holds frame counter + ticks.
void crackdown_hit_merge_window(PPCRegister& deadline) {
  const uint32_t kTicks = crackdown::Layout().hit_merge_ticks;
  const uint32_t ticks = crackdown::ScaleTickWindow(kTicks, crackdown::OriginalStepsPerUpdate());
  deadline.u64 = uint32_t(deadline.u32 - kTicks + ticks);
}

void crackdown_ragdoll_tick_window(PPCRegister& deadline) {
  constexpr uint32_t kTicks = 15;
  const uint32_t ticks = crackdown::ScaleTickWindow(kTicks, crackdown::OriginalStepsPerUpdate());
  deadline.u64 = uint32_t(deadline.u32 - kTicks + ticks);
}

// Codegen hook after the `fmuls` that scales the gravity by dt: f0 = gravity * dt, f13 = the vertical speed accumulator,
// 0 after a grounded step.
void crackdown_first_gravity_step(PPCRegister& gravity_step, PPCRegister& accumulated) {
  gravity_step.f64 = double(crackdown::ScaleFirstGravityStep(float(gravity_step.f64), float(accumulated.f64),
                                                             crackdown::OriginalStepsPerUpdate()));
}

// Codegen hook before the pose call of start ragdoll: f1 is the step's dt, and the character has a flag set while it is
// ragdolled.
void crackdown_ragdoll_pose_dt(PPCRegister& dt, PPCRegister& character) {
  const crackdown::GuestLayout& layout = crackdown::Layout();
  const bool ragdolled =
      (crackdown::guest::Load8(GuestBase(), character.u32 + layout.character_ragdoll_flags) &
       layout.character_ragdoll_mask) != 0;
  dt.f64 = double(float(crackdown::ScaleRagdollPoseDt(dt.f64, ragdolled, crackdown::OriginalStepsPerUpdate())));
}

// Codegen hook before the impact ray of the projectile update: the float at r1+476, stored earlier and read only by the
// ray, is the step's dt.
void crackdown_projectile_probe_dt(PPCRegister& stack) {
  constexpr uint32_t kProbeDtOffset = 476;
  uint8_t* base = GuestBase();
  const uint32_t slot = stack.u32 + kProbeDtOffset;
  crackdown::guest::StoreFloat(base, slot,
                               crackdown::ScaleProjectileProbeDt(crackdown::guest::LoadFloat(base, slot),
                                                                 crackdown::OriginalStepsPerUpdate()));
}

// Codegen hook before the `fdivs` of the camera lift: the divisor is the camera forward axis's y.
void crackdown_camera_lift_divisor(PPCRegister& forward_y) {
  forward_y.f64 = double(crackdown::GuardCameraLiftDivisor(float(forward_y.f64)));
}
