// Dependency rules between Title Update 6 and the DLC packs, and which guest build Play selects.

#include "check.h"
#include "content_catalog.h"
#include "content_state.h"

using namespace crackdown::content;

namespace {

void TestDependencies() {
  std::printf("Four-Play needs nothing, Free-For-All needs a usable Title Update 6\n");
  ContentState none;
  CHECK(CanInstallDlc(DlcPack::kFourPlay, none));
  CHECK(!CanInstallDlc(DlcPack::kFreeForAll, none));
  ContentState installed_only{.title_update_usable = false, .title_update_installed = true};
  CHECK(CanInstallDlc(DlcPack::kFourPlay, installed_only));
  CHECK(!CanInstallDlc(DlcPack::kFreeForAll, installed_only));
  ContentState usable{.title_update_usable = true, .title_update_installed = true};
  CHECK(CanInstallDlc(DlcPack::kFourPlay, usable));
  CHECK(CanInstallDlc(DlcPack::kFreeForAll, usable));
  CHECK(!DlcNeedsTitleUpdate(DlcPack::kFourPlay));
  CHECK(DlcNeedsTitleUpdate(DlcPack::kFreeForAll));
}

void TestRemovalRule() {
  std::printf("Title Update 6 cannot be removed under Free-For-All\n");
  CHECK(CanRemoveTitleUpdate({.title_update_usable = true, .title_update_installed = true}));
  CHECK(CanRemoveTitleUpdate({.title_update_usable = true, .title_update_installed = true}));
  CHECK(!CanRemoveTitleUpdate(
      {.title_update_usable = true, .title_update_installed = true, .free_for_all_installed = true}));
  CHECK(!CanRemoveTitleUpdate({.free_for_all_installed = true}));
}

void TestSelection() {
  std::printf("Play selects the guest build deterministically\n");
  CHECK(SelectGuestBuild({}) == GuestBuild::kBase);
  CHECK(SelectGuestBuild({.title_update_installed = true}) == GuestBuild::kBase);
  CHECK(SelectGuestBuild({.title_update_usable = true, .title_update_installed = true}) == GuestBuild::kTitleUpdate6);
  CHECK(SelectGuestBuild({.title_update_usable = true, .title_update_installed = true,
                          .free_for_all_installed = true}) == GuestBuild::kTitleUpdate6);
  CHECK(SelectGuestBuild({.title_update_installed = true, .free_for_all_installed = true}) == GuestBuild::kBase);
}

void TestUnsafeContent() {
  std::printf("Free-For-All against the base build is the one state Play refuses\n");
  CHECK(!UnsafeContent({}));
  CHECK(!UnsafeContent({.title_update_usable = true, .title_update_installed = true}));
  CHECK(!UnsafeContent({.title_update_usable = true, .title_update_installed = true, .free_for_all_installed = true}));
  CHECK(UnsafeContent({.free_for_all_installed = true}));
  CHECK(UnsafeContent({.title_update_installed = true, .free_for_all_installed = true}));
}

void TestIdentities() {
  std::printf("the supported packs\n");
  CHECK(std::string(DlcName(DlcPack::kFourPlay)) == "Four-Play Pack");
  CHECK(std::string(DlcName(DlcPack::kFreeForAll)) == "Free-For-All Pack");
  CHECK(std::string(DlcName(DlcPack::kUnknown)).empty());
  CHECK(kDlcPacks[0] == DlcPack::kFourPlay && kDlcPacks[1] == DlcPack::kFreeForAll);
  for (const DlcIdentity& identity : kDlcIdentities) {
    CHECK(std::string(identity.content_id).size() == 40);
    CHECK(IdentityOf(identity.pack) == &identity);
  }
}

void TestInstallAndRemovalChangeTheGuest() {
  std::printf("installing or removing the update changes the guest the next process selects\n");
  const ContentState none;
  const ContentState usable{.title_update_usable = true, .title_update_installed = true};
  CHECK(SelectGuestBuild(none) == GuestBuild::kBase);
  CHECK(SelectGuestBuild(usable) == GuestBuild::kTitleUpdate6);
  // A base process that installed the update, and a Title Update 6 process that removed it, both differ from what
  // they run: that is when the launcher starts a new process of the same program.
  CHECK(SelectGuestBuild(usable) != GuestBuild::kBase);
  CHECK(SelectGuestBuild(none) != GuestBuild::kTitleUpdate6);
}

}  // namespace

int main() {
  TestDependencies();
  TestRemovalRule();
  TestSelection();
  TestUnsafeContent();
  TestIdentities();
  TestInstallAndRemovalChangeTheGuest();
  return check::Finish("content_policy_tests");
}
