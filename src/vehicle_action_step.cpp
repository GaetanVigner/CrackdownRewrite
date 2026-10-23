// The once-per-1/30 s vehicle action is adapted from Unowa001/crackdown-recomp (BSD 3-Clause, see
// THIRD-PARTY-LICENSES.txt).

#include "vehicle_action_step.h"

#include <bit>

#include <rex/hook.h>
#include <rex/ppc.h>

#include "guest_hooks.h"
#include "guest_memory.h"

namespace {

crackdown::VehicleActionClock g_clock;

}  // namespace

// Havok vehicle action (slot +8 of the vehicle vtable): r3 is the vehicle instance, r4 the hkStepInfo with deltaTime at
// +8 and invDeltaTime at +12. The callees read the same hkStepInfo, so it carries the chosen step for the call and gets
// its own words back after.
void crackdown::hooks::VehicleAction(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  constexpr uint32_t kDeltaTime = 8;
  constexpr uint32_t kInvDeltaTime = 12;
  const uint32_t info = ctx.r4.u32;
  const uint32_t dt_bits = crackdown::guest::Load32(base, info + kDeltaTime);
  const float dt = std::bit_cast<float>(dt_bits);
  const float step = g_clock.Step(ctx.r3.u32, dt);
  if (step == 0.0f) {
    return;
  }
  if (step == dt) {
    original(ctx, base);
    return;
  }
  const uint32_t inv_bits = crackdown::guest::Load32(base, info + kInvDeltaTime);
  crackdown::guest::StoreFloat(base, info + kDeltaTime, step);
  crackdown::guest::StoreFloat(base, info + kInvDeltaTime, 1.0f / step);
  original(ctx, base);
  crackdown::guest::Store32(base, info + kDeltaTime, dt_bits);
  crackdown::guest::Store32(base, info + kInvDeltaTime, inv_bits);
}
