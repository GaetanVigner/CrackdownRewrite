#include <bit>
#include <cstdint>
#include <cstring>
#include <optional>

#include <rex/hook.h>
#include <rex/ppc.h>

#include "aspect_ratio.h"
#include "guest_hooks.h"
#include "guest_layout.h"
#include "settings.h"

namespace crackdown {
namespace {

void StoreBEFloat(uint8_t* base, uint32_t addr, float value) {
  const uint32_t bits = std::byteswap(std::bit_cast<uint32_t>(value));
  std::memcpy(rex::memory::GuestPtr<uint8_t*>(base, addr), &bits, sizeof(bits));
}

}  // namespace
}  // namespace crackdown

// The camera constructor is the only writer of the camera aspect the view and projection builders read. Replaces the
// 16:9 it stores; the title's vertical field of view stays, so a wider format shows more of the scene to the sides.
void crackdown::hooks::CameraConstructed(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  original(ctx, base);
  const std::optional<double> aspect =
      crackdown::CameraAspectOverride(crackdown::ParseAspectRatio(REXCVAR_GET(crackdown_aspect_ratio)));
  if (aspect) {
    crackdown::StoreBEFloat(base, crackdown::Layout().camera_aspect, float(*aspect));
  }
}
