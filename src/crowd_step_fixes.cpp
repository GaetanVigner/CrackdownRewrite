// The crowd library (pedestrians and the far, low-detail traffic of sub_82343730, run once per update from the world
// pass sub_82324330) moves its agents by a step sized for the title's 1/30 s update and never reads dt: above 30 FPS
// they walked and drove up to 8x too fast, then slowed down when they came close enough to become full NPCs. These
// codegen hooks (config/overrides.toml) make the per-update quantities per 1/30 s of game time; nothing changes at 30.
//
// - The per-update step at agent+180 is the walk speed (+192) times 1/30, or the run speed (+196) times 2/30: set in
//   the steering update sub_82338220 (f21 loaded at 82338390, f22 at 82338380, both before its agent loop) and at
//   spawn (sub_82330B68: f13 at 82330CCC, f0 at 82330CE0; sub_82342470: f12 at 82342A24). Agent+180 also gives the
//   number of updates to arrival (sub_82341740, sub_82342470), which stays right in time with the scaled step.
// - The steering blends the per-update displacement toward the desired one by 0.05 per update (f20 at 823383AC).
// - When an agent becomes a full NPC, sub_82340C58 turns its per-update displacement into a velocity by multiplying by
//   30 updates a second (f0 at 82340EF4): with a step of 1/30 s * steps, that is 30 / steps updates a second.
// - The far traffic is a separate pool of crowd cars (LOD4/5 meshes, pool at 0x82FDE4B0), moved once per update by
//   sub_82333538: heading blended 0.1 per update toward the next road node (f0 at 82333580), position += heading times
//   a distance per update, base step (+116) times a random speed factor (+120) (f0 at 823335A0), and +124 updates left
//   to the node counted down. sub_82343ED8 sets that count from the same distance (f0 at 8234417C), so both scale
//   alike or the cars would cut their segments short. Crowd cars never become full vehicles (they are freed near the
//   player, where real traffic spawns), so there is no hand-off velocity.

#include <rex/ppc.h>

#include "character_step_fixes.h"
#include "frame_pacing.h"
#include "frame_timing.h"

void crackdown_crowd_step(PPCRegister& per_update) {
  per_update.f64 =
      double(crackdown::ScalePerOriginalStep(float(per_update.f64), crackdown::OriginalStepsPerUpdate()));
}

void crackdown_crowd_blend(PPCRegister& fraction) {
  fraction.f64 = double(crackdown::ScaleGroundSnapLerp(float(fraction.f64), crackdown::OriginalStepsPerUpdate()));
}

void crackdown_crowd_updates_per_second(PPCRegister& rate) {
  const double steps = crackdown::OriginalStepsPerUpdate();
  if (steps > 0.0) {
    rate.f64 = double(float(rate.f64 / steps));
  }
}
