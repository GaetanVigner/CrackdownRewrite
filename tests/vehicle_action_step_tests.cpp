#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "check.h"
#include "vehicle_action_step.h"

using namespace crackdown;

namespace {

// The physics dt the title's frame-ms site gives at a frame rate: whole milliseconds, the remainder carried.
std::vector<float> PhysicsSteps(int rate, int frames) {
  std::vector<float> steps;
  double carry = 0.0;
  for (int i = 0; i < frames; ++i) {
    carry += 1000.0 / rate;
    const double ms = std::floor(carry);
    carry -= ms;
    steps.push_back(float(ms) * 0.001f);
  }
  return steps;
}

void TestThirtyFpsUnchanged() {
  VehicleActionClock clock;
  for (int i = 0; i < 100; ++i) {
    CHECK(clock.Step(1, 0.033f) == 0.033f);
  }
}

// Each run is the whole number of physics steps closest to 1/30 s, and the action is given all of the physics time.
void TestRunsAtThirtyHz() {
  for (const int rate : {60, 90, 120, 144, 165, 240}) {
    VehicleActionClock clock;
    const std::vector<float> steps = PhysicsSteps(rate, rate * 60);
    double physics = 0.0;
    double action = 0.0;
    for (const float dt : steps) {
      physics += dt;
      const float step = clock.Step(1, dt);
      if (step != 0.0f) {
        action += step;
        CHECK(step < VehicleActionClock::kPeriod + dt);
        CHECK(step >= VehicleActionClock::kPeriod - dt * 0.5f - 1e-6f);
      }
    }
    CHECK(physics - action > -1e-4 && physics - action < VehicleActionClock::kPeriod);
  }
}

void TestSixtyFpsRunsEveryOtherStep() {
  VehicleActionClock clock;
  const std::vector<float> steps = PhysicsSteps(60, 60);
  for (size_t i = 0; i < steps.size(); ++i) {
    const float step = clock.Step(1, steps[i]);
    CHECK((step != 0.0f) == (i % 2 == 1));
  }
}

// A long step runs as it is and leaves the time owed to the action for its next run.
void TestLongStepKeepsPendingTime() {
  VehicleActionClock clock;
  CHECK(clock.Step(1, 0.016f) == 0.0f);
  CHECK(clock.Step(1, 0.05f) == 0.05f);
  CHECK(std::fabs(clock.Step(1, 0.017f) - 0.033f) < 1e-6f);
}

void TestVehiclesAreIndependent() {
  VehicleActionClock clock;
  CHECK(clock.Step(1, 0.016f) == 0.0f);
  CHECK(clock.Step(2, 0.016f) == 0.0f);
  CHECK(std::fabs(clock.Step(1, 0.017f) - 0.033f) < 1e-6f);
  CHECK(clock.Step(3, 0.017f) == 0.0f);
  CHECK(std::fabs(clock.Step(2, 0.017f) - 0.033f) < 1e-6f);
}

void TestDegenerateStepsPassThrough() {
  VehicleActionClock clock;
  CHECK(clock.Step(1, 0.0f) == 0.0f);
  CHECK(clock.Step(1, -0.01f) == -0.01f);
  CHECK(std::isnan(clock.Step(1, std::numeric_limits<float>::quiet_NaN())));
  CHECK(std::isinf(clock.Step(1, std::numeric_limits<float>::infinity())));
  CHECK(clock.Step(1, 0.016f) == 0.0f);
}

}  // namespace

int main() {
  TestThirtyFpsUnchanged();
  TestRunsAtThirtyHz();
  TestSixtyFpsRunsEveryOtherStep();
  TestLongStepKeepsPendingTime();
  TestVehiclesAreIndependent();
  TestDegenerateStepsPassThrough();
  return check::Finish("vehicle_action_step_tests");
}
