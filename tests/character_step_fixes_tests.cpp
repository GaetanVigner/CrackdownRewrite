#include <cmath>

#include "character_step_fixes.h"
#include "check.h"

using namespace crackdown;

namespace {

void TestLedgeSnapDown() {
  // 30 FPS (one original step per update) is untouched, bit for bit.
  for (const float pull : {0.0f, 0.1f, 0.4f, 0.8f, 0.123456f}) {
    CHECK(ScaleLedgeSnapDown(pull, 1.0) == pull);
  }
  // At 120 and 60 FPS, dividing the scaled pull by dt gives the pull over the 1/30 s step.
  const float pull = 0.8f;
  CHECK(std::fabs(ScaleLedgeSnapDown(pull, 0.25) / (1.0 / 120.0) - pull * 30.0) < 1e-4);
  CHECK(std::fabs(ScaleLedgeSnapDown(pull, 0.5) / (1.0 / 60.0) - pull * 30.0) < 1e-4);
  // A caught-up frame of two original steps keeps the per-second pull too.
  CHECK(std::fabs(ScaleLedgeSnapDown(pull, 2.0) / (2.0 / 30.0) - pull * 30.0) < 1e-4);
  // A degenerate step count leaves the title's value alone.
  CHECK(ScaleLedgeSnapDown(pull, 0.0) == pull);
  CHECK(ScaleLedgeSnapDown(pull, -1.0) == pull);
}

void TestGroundSnapDecay() {
  for (const float decay : {0.01f, 0.5f, 0.9f, 1.0f}) {
    CHECK(ScaleGroundSnapDecay(decay, 1.0) == decay);
  }
  // Four 120 FPS steps decay as much as one 30 FPS step.
  float blend = 1.0f;
  for (int i = 0; i < 4; ++i) blend *= ScaleGroundSnapDecay(0.6f, 0.25);
  CHECK(std::fabs(blend - 0.6f) < 1e-5);
  CHECK(ScaleGroundSnapDecay(1.0f, 0.25) == 1.0f);
  CHECK(ScaleGroundSnapDecay(0.0f, 0.25) == 0.0f);
  CHECK(std::isnan(ScaleGroundSnapDecay(NAN, 0.25)));
}

void TestGroundSnapLerp() {
  for (const float fraction : {0.0f, 0.2f, 0.5f, 1.0f}) {
    CHECK(ScaleGroundSnapLerp(fraction, 1.0) == fraction);
  }
  // Four 120 FPS steps close the same share of the gap as one 30 FPS step.
  float gap = 1.0f;
  for (int i = 0; i < 4; ++i) gap *= 1.0f - ScaleGroundSnapLerp(0.4f, 0.25);
  CHECK(std::fabs(gap - 0.6f) < 1e-5);
  // A full snap stays a full snap; nothing moves stays still.
  CHECK(ScaleGroundSnapLerp(1.0f, 0.25) == 1.0f);
  CHECK(ScaleGroundSnapLerp(0.0f, 0.25) == 0.0f);
}

void TestTickWindows() {
  CHECK(ScaleTickWindow(10, 1.0) == 10);
  CHECK(ScaleTickWindow(15, 1.0) == 15);
  CHECK(ScaleTickWindow(10, 0.25) == 40);  // 120 FPS
  CHECK(ScaleTickWindow(15, 0.5) == 30);   // 60 FPS
  CHECK(ScaleTickWindow(10, 2.0) == 5);    // a caught-up double step
  CHECK(ScaleTickWindow(10, 0.0) == 10);
}

void TestFirstGravityStep() {
  const float g30 = -14.0f / 30.0f;
  // 30 FPS: untouched.
  CHECK(ScaleFirstGravityStep(g30, 0.0f, 1.0) == g30);
  // 120 FPS from the ground: the 30 FPS increment; in the air: the real one.
  const float g120 = -14.0f / 120.0f;
  CHECK(ScaleFirstGravityStep(g120, 0.0f, 0.25) == g120 / 0.25f);
  CHECK(ScaleFirstGravityStep(g120, -2.5f, 0.25) == g120);
  // A caught-up double step keeps its own increment.
  CHECK(ScaleFirstGravityStep(2 * g30, 0.0f, 2.0) == 2 * g30);
}

void TestRagdollPoseDt() {
  // 30 FPS, or a first start: untouched.
  CHECK(ScaleRagdollPoseDt(1.0 / 30.0, true, 1.0) == 1.0 / 30.0);
  CHECK(ScaleRagdollPoseDt(1.0 / 120.0, false, 0.25) == 1.0 / 120.0);
  // A restart at 120 FPS differences over the 30 FPS step.
  CHECK(ScaleRagdollPoseDt(1.0 / 120.0, true, 0.25) == (1.0 / 120.0) / 0.25);
}

void TestCameraLiftDivisor() {
  CHECK(GuardCameraLiftDivisor(0.5f) == 0.5f);
  CHECK(GuardCameraLiftDivisor(-0.2f) == -0.2f);
  CHECK(GuardCameraLiftDivisor(0.0f) == kMinCameraLift);
  CHECK(GuardCameraLiftDivisor(-0.0f) == -kMinCameraLift);
  CHECK(GuardCameraLiftDivisor(1e-6f) == kMinCameraLift);
}

}  // namespace

int main() {
  TestLedgeSnapDown();
  TestGroundSnapDecay();
  TestGroundSnapLerp();
  TestTickWindows();
  TestFirstGravityStep();
  TestRagdollPoseDt();
  TestCameraLiftDivisor();
  return check::Finish("character_step_fixes_tests");
}
