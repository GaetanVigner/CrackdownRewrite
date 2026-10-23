#include "guest_image.h"

#include <atomic>
#include <cstdlib>

namespace crackdown {
namespace {
std::atomic<const GuestImage*> g_selected{nullptr};
}

const GuestImage* FindGuestImage(content::GuestBuild build) {
  switch (build) {
    case content::GuestBuild::kBase:
      return &guests::kBase;
    case content::GuestBuild::kTitleUpdate6:
#if defined(CRACKDOWN_TU6_GUEST_LINKED)
      return &guests::kTitleUpdate6;
#else
      return nullptr;
#endif
  }
  return nullptr;
}

const GuestImage* SelectGuest(content::GuestBuild build) {
  const GuestImage* guest = FindGuestImage(build);
  const GuestImage* expected = nullptr;
  if (guest && !g_selected.compare_exchange_strong(expected, guest) && expected != guest) {
    std::abort();
  }
  if (guest) {
    RunningBuildStorage() = guest->build;
  }
  return guest;
}

const GuestImage& SelectedGuest() {
  return *g_selected.load();
}

}  // namespace crackdown
