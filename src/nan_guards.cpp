// After a ragdoll the character's head look-at target (+0x13B0, written somewhere in the look and head path) can
// become NaN and stay NaN; the head controller and the audio listener, built from the same head data, then carry it,
// and later the whole character state goes NaN. These hooks keep finite values flowing; with finite data they change
// nothing. Each repair is logged, at most once per kLogGapMs per kind with the count since the last line.

#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/system/kernel_state.h>

#include "guest_memory.h"
#include "physics_telemetry.h"

namespace {

using crackdown::guest::LoadFloat;
using crackdown::guest::StoreFloat;

constexpr uint32_t kLookTargetOffset = 0x13B0;
constexpr uint32_t kPositionOffset = 0x140;
constexpr int64_t kLogGapMs = 2000;
constexpr int64_t kDumpGapMs = 20000;

uint8_t* GuestBase() {
  return rex::system::kernel_state()->memory()->virtual_membase();
}

// Vectors here are four floats whose fourth (w) lane the title leaves as garbage, even a NaN bit pattern, at any frame
// rate (the character position's w is 0xFFFFFFFF); only x, y and z are checked and repaired.
bool XyzFinite(uint8_t* base, uint32_t address) {
  for (uint32_t i = 0; i < 3; ++i) {
    if (!std::isfinite(LoadFloat(base, address + 4 * i))) {
      return false;
    }
  }
  return true;
}

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

struct RepairLog {
  const char* what;
  std::atomic<int64_t> last_ms{0};
  std::atomic<uint32_t> pending{0};
  std::atomic<int64_t> last_dump_ms{-kDumpGapMs};

  void Note() {
    const uint32_t count = pending.fetch_add(1, std::memory_order_relaxed) + 1;
    const int64_t now = NowMs();
    int64_t last = last_ms.load(std::memory_order_relaxed);
    if (now - last >= kLogGapMs && last_ms.compare_exchange_strong(last, now)) {
      pending.store(0, std::memory_order_relaxed);
      REXLOG_WARN("[NAN_GUARD] {} was not finite ({} repair(s) since the last line)", what, count);
    }
    // A repair keeps the seconds before it, which hold whatever produced the NaN; one dump per kDumpGapMs per kind.
    int64_t dumped = last_dump_ms.load(std::memory_order_relaxed);
    if (now - dumped >= kDumpGapMs && last_dump_ms.compare_exchange_strong(dumped, now)) {
      crackdown::RequestTelemetryDump();
    }
  }
};

RepairLog g_listener_log{"The audio listener"};
RepairLog g_target_log{"The character's look-at target"};
RepairLog g_aim_log{"The player's aim point"};
RepairLog g_camera_log{"The camera"};

// Only the audio thread builds the listener: the four xyz rows of its matrix and its velocity.
float g_listener_matrix[4][3];
float g_listener_vector[3];
bool g_listener_valid = false;

// The game thread updates the character; its last finite look-at target stands in for a NaN one.
float g_last_target[3];
bool g_target_valid = false;

void GuardLookTarget(uint32_t character) {
  uint8_t* base = GuestBase();
  const uint32_t target = character + kLookTargetOffset;
  if (XyzFinite(base, target)) {
    for (uint32_t i = 0; i < 3; ++i) g_last_target[i] = LoadFloat(base, target + 4 * i);
    g_target_valid = true;
    return;
  }
  g_target_log.Note();
  for (uint32_t i = 0; i < 3; ++i) {
    const float fallback = LoadFloat(base, character + kPositionOffset + 4 * i);
    StoreFloat(base, target + 4 * i, g_target_valid ? g_last_target[i] : (std::isfinite(fallback) ? fallback : 0.0f));
  }
}

}  // namespace

// Codegen hook before the `bl 0x8225DD68` at 82252134 in sub_82252068 (config/overrides.toml): r3 is the listener's
// 4x4 matrix (row 3 the position, from the character's head data), r4 its velocity vector.
void crackdown_audio_listener_guard(PPCRegister& matrix, PPCRegister& vector) {
  uint8_t* base = GuestBase();
  bool finite = XyzFinite(base, vector.u32);
  for (uint32_t row = 0; row < 4; ++row) {
    finite = finite && XyzFinite(base, matrix.u32 + 16 * row);
  }
  if (finite) {
    for (uint32_t row = 0; row < 4; ++row) {
      for (uint32_t i = 0; i < 3; ++i) g_listener_matrix[row][i] = LoadFloat(base, matrix.u32 + 16 * row + 4 * i);
    }
    for (uint32_t i = 0; i < 3; ++i) g_listener_vector[i] = LoadFloat(base, vector.u32 + 4 * i);
    g_listener_valid = true;
    return;
  }
  g_listener_log.Note();
  for (uint32_t row = 0; row < 4; ++row) {
    for (uint32_t i = 0; i < 3; ++i) {
      // Without a finite listener yet, an identity basis at the origin.
      const float identity = row == i ? 1.0f : 0.0f;
      StoreFloat(base, matrix.u32 + 16 * row + 4 * i, g_listener_valid ? g_listener_matrix[row][i] : identity);
    }
  }
  for (uint32_t i = 0; i < 3; ++i) {
    StoreFloat(base, vector.u32 + 4 * i, g_listener_valid ? g_listener_vector[i] : 0.0f);
  }
}

// Codegen hooks before the two reads of the character's look-at target (+0x13B0): `addi r6,r31,5040` at 822A026C in
// sub_822A0170 (head controller) and `addi r5,r31,5040` at 822A2C50 in sub_822A28D0 (look matrix +0x490, which reads
// it first). r31 is the character. A NaN target becomes the last finite position of the character (no head turn).
void crackdown_look_target_guard(PPCRegister& character) {
  GuardLookTarget(character.u32);
}

REX_EXTERN(__imp__sub_824FD840);

// The player controller's aim update (r3 = controller) writes the aim point to +5152 (copied to the character's
// look-at target) and +5104, from the camera aim ray (sub_825765A8) or the locked target's aim point (sub_825798D8).
// A target whose body went NaN (a ragdoll that blew up) hands it a NaN point every update; the previous point is kept.
REX_HOOK_RAW(sub_824FD840) {
  constexpr uint32_t kAimOffsets[] = {5152, 5104};
  constexpr uint32_t kCharacterOffsetInController = 292;
  constexpr uint32_t kCharacterBase = 32;
  const uint32_t controller = ctx.r3.u32;
  uint8_t saved[2][16];
  for (int i = 0; i < 2; ++i) {
    std::memcpy(saved[i], rex::memory::GuestPtr<const uint8_t*>(base, controller + kAimOffsets[i]), 16);
  }
  __imp__sub_824FD840(ctx, base);
  for (int i = 0; i < 2; ++i) {
    const uint32_t field = controller + kAimOffsets[i];
    if (XyzFinite(base, field)) {
      continue;
    }
    g_aim_log.Note();
    uint8_t* out = rex::memory::GuestPtr<uint8_t*>(base, field);
    bool saved_finite = true;
    for (int lane = 0; lane < 3; ++lane) {
      uint32_t raw = 0;
      std::memcpy(&raw, saved[i] + 4 * lane, 4);
      saved_finite = saved_finite && std::isfinite(std::bit_cast<float>(std::byteswap(raw)));
    }
    if (saved_finite) {
      std::memcpy(out, saved[i], 12);
      continue;
    }
    const uint32_t subobject = crackdown::guest::Load32(base, controller + kCharacterOffsetInController);
    const uint32_t character = crackdown::guest::Plausible(subobject) ? subobject - kCharacterBase : 0;
    for (uint32_t lane = 0; lane < 3; ++lane) {
      const float position = character ? LoadFloat(base, character + kPositionOffset + 4 * lane) : 0.0f;
      StoreFloat(base, field + 4 * lane, std::isfinite(position) ? position : 0.0f);
    }
  }
}

REX_EXTERN(__imp__sub_82287130);

// The camera's per-frame update (r3 = camera). After a landing against a wall at high frame rates its state went NaN
// and stayed NaN: the audio listener and the aim ray are built from it, and the stick, turned by the camera's
// orientation into a walking direction, became unusable (the player "stuck"). A camera that comes out of its update
// with a non-finite basis or position is put back to its state before the update.
REX_HOOK_RAW(sub_82287130) {
  // The basis rows and position, then the target-derived points the listener uses.
  struct CameraField {
    uint32_t offset;
    const char* name;
  };
  constexpr CameraField kFields[] = {{272, "basis x"},       {288, "basis y"},       {304, "basis z"},
                                     {320, "position"},      {6336, "velocity"},     {6352, "target"},
                                     {6368, "previous target"}, {6384, "listener point"}};
  // Through the target matrix (+6640) and the follow state up to +7140, which the update also writes.
  constexpr uint32_t kCameraBytes = 7152;
  static std::atomic_flag logged_fields;
  thread_local std::vector<uint8_t> saved(kCameraBytes);
  const uint32_t camera = ctx.r3.u32;
  const bool plausible = crackdown::guest::Plausible(camera);
  if (plausible) {
    std::memcpy(saved.data(), rex::memory::GuestPtr<const uint8_t*>(base, camera), kCameraBytes);
  }
  __imp__sub_82287130(ctx, base);
  if (!plausible) {
    return;
  }
  std::string broken;
  for (const CameraField& field : kFields) {
    if (!XyzFinite(base, camera + field.offset)) {
      broken += broken.empty() ? field.name : std::string(", ") + field.name;
    }
  }
  if (broken.empty()) {
    return;
  }
  g_camera_log.Note();
  if (!logged_fields.test_and_set()) {
    REXLOG_WARN("[NAN_GUARD] camera fields not finite after its update: {}", broken);
  }
  std::memcpy(rex::memory::GuestPtr<uint8_t*>(base, camera), saved.data(), kCameraBytes);
}
