#pragma once

#include "content_catalog.h"

namespace crackdown {

// The recompiled guest this process runs. It is chosen once, before the runtime is created (SelectGuest in
// guest_image.h); the other guest linked into the executable is never initialized.
inline content::GuestBuild& RunningBuildStorage() {
  static content::GuestBuild build = content::GuestBuild::kBase;
  return build;
}

inline content::GuestBuild RunningBuild() {
  return RunningBuildStorage();
}

}  // namespace crackdown
