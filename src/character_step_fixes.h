#pragma once

#include <cmath>
#include <cstdint>

namespace crackdown {

// Character movement code tuned for the shipped 1/30 s step. Above 30 FPS the title runs a shorter step through the
// same code; these turn its per-step terms into per-second ones. steps is the update's length in 1/30 s steps (1.0 at
// 30 FPS, where every function returns its input unchanged).

// sub_822B4D40, the ledge and slope follower, pulls the character down onto the ground ahead with a vertical velocity
// of -(clamp(h, 0, 2) * 0.4) / dt: 40% of the probed height h closed per step. With a shorter step the same height
// gives a proportionally larger velocity, the Havok character proxy turns it into speed along the slope, and that
// speed is the next step's base: above 30 FPS it runs away (about 1.64x per step at 120 FPS, the launches on slopes and
// off ledges). Scaling the pull by steps divides it by the title's 1/30 s step instead, the pull per second of the
// original; the title's time scale is already in dt and stays in.
constexpr float ScaleLedgeSnapDown(float height_pull, double steps) {
  return steps > 0.0 && steps != 1.0 ? height_pull * float(steps) : height_pull;
}

// sub_822A2C90, the character's vertical visual smoothing toward its recent motion height, multiplies its blend by a
// speed factor c in [0.01, 1] and moves by a fraction a in [0, 1] of the gap, both once per step. Per second at any
// step: c^steps and 1 - (1 - a)^steps.
inline float ScaleGroundSnapDecay(float decay, double steps) {
  if (!(steps > 0.0) || steps == 1.0 || !(decay > 0.0f)) {
    return decay;
  }
  return std::pow(decay, float(steps));
}

inline float ScaleGroundSnapLerp(float fraction, double steps) {
  if (!(steps > 0.0) || steps == 1.0 || !(fraction > 0.0f) || fraction >= 1.0f) {
    return fraction;
  }
  return 1.0f - std::pow(1.0f - fraction, float(steps));
}

// Some character windows count updates of the frame counter at 0x8307107C (one tick per update): the hit-reaction
// merge window of 10 and a ragdoll threshold of 15 ticks, 1/3 s and 1/2 s at 30 FPS. The same number of real 1/30 s
// steps at any rate: the tick count divided by the update's length in 1/30 s steps.
inline uint32_t ScaleTickWindow(uint32_t ticks, double steps) {
  if (!(steps > 0.0) || steps == 1.0) {
    return ticks;
  }
  return uint32_t(std::lround(double(ticks) / steps));
}

// sub_822A9E78 adds gravity * dt to the vertical speed accumulator (+532 of the motion object) each step, and a
// grounded step leaves it at 0, so on the ground every step starts from one step of gravity: settling onto the floor
// after a landing or a get-up, and sliding off steep faces, run at g * dt, four times slower at 120 FPS than at 30.
// The character stays in that contact zone four times longer, where the ledge follower's pull stops its horizontal
// motion and its speed ramp restarts from rest each step (the stuck-in-walls reports). The first increment after a
// grounded step is the 30 FPS one, g / 30; later ones, in the air, stay g * dt.
constexpr float ScaleFirstGravityStep(float gravity_step, float accumulated, double steps) {
  return accumulated == 0.0f && steps > 0.0 && steps < 1.0 ? gravity_step / float(steps) : gravity_step;
}

}  // namespace crackdown
