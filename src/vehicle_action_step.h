#pragma once

#include <cmath>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace crackdown {

// The title's Havok vehicle action loses speed per step, so above 30 FPS cars pull away slower. Each vehicle's action
// runs once per 1/30 s of physics time with the time accumulated since its last run; Havok still integrates the
// chassis every step.
class VehicleActionClock {
 public:
  static constexpr float kPeriod = 1.0f / 30.0f;
  // Steps of 25 ms or more (40 FPS and below, and caught-up steps) run as they are.
  static constexpr float kPassThroughStep = kPeriod * 0.75f;

  // The dt to run the vehicle's action with on this physics step, or 0 to skip it.
  float Step(uint32_t vehicle, float dt) {
    if (!(dt > 0.0f) || !std::isfinite(dt) || dt >= kPassThroughStep) {
      return dt;
    }
    std::lock_guard lock(mutex_);
    float& pending = pending_[vehicle];
    pending += dt;
    // The step that brings the total closest to one period runs: two 16.7 ms steps, not three.
    if (pending + dt * 0.5f < kPeriod) {
      return 0.0f;
    }
    const float step = pending;
    pending_.erase(vehicle);
    return step;
  }

 private:
  std::mutex mutex_;
  std::unordered_map<uint32_t, float> pending_;
};

}  // namespace crackdown
