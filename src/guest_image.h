#pragma once

#include <rex/image_info.h>

#include "guest_build.h"
#include "guest_hooks.h"

namespace crackdown {

// One recompiled guest linked into the executable. Each guest_bindings_<id>.cpp defines its own, next to the
// bindings of that guest's functions.
struct GuestImage {
  content::GuestBuild build;
  const char* name;
  const rex::PPCImageInfo* image;
  const hooks::GuestCalls* calls;
};

namespace guests {
extern const GuestImage kBase;
extern const GuestImage kTitleUpdate6;  // defined only when CRACKDOWN_TU6_GUEST_LINKED
}  // namespace guests

// The linked guest of `build`; null when this executable does not carry it.
const GuestImage* FindGuestImage(content::GuestBuild build);

// Makes `build` the guest this process runs and returns it. Called once, before the runtime is created; nothing of
// another guest is initialized. Returns null, selecting nothing, when `build` is not linked.
const GuestImage* SelectGuest(content::GuestBuild build);

// The selected guest. SelectGuest must have succeeded.
const GuestImage& SelectedGuest();

}  // namespace crackdown
