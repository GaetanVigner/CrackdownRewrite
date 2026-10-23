#include "content_state.h"

#include <algorithm>

#include "save_generation.h"

namespace crackdown {

content::ContentState ContentSnapshot::State() const {
  content::ContentState state;
  state.title_update_installed = title_update.state == TitleUpdateState::kInstalled;
  state.title_update_usable = state.title_update_installed && title_update_build_available;
  state.free_for_all_installed = Installed(content::DlcPack::kFreeForAll);
  state.save = saves.combined;
  state.save_switch_failed = save_switch_failed;
  return state;
}

const DlcInfo* ContentSnapshot::Find(content::DlcPack pack) const {
  const auto it = std::find_if(dlc.begin(), dlc.end(),
                               [&](const DlcInfo& info) { return info.verified && info.pack == pack; });
  return it == dlc.end() ? nullptr : &*it;
}

bool ContentSnapshot::Installed(content::DlcPack pack) const {
  return std::any_of(dlc.begin(), dlc.end(), [&](const DlcInfo& info) { return info.pack == pack; });
}

ContentSnapshot ReadContentSnapshot(const std::filesystem::path& user_data_root, bool title_update_build_available,
                                    const TitleUpdateDescriptor& update) {
  ContentSnapshot snapshot;
  snapshot.title_update = CheckTitleUpdate(user_data_root, update);
  snapshot.dlc = ListInstalledDlc(user_data_root);
  snapshot.title_update_build_available = title_update_build_available;
  snapshot.saves = CheckSaves(user_data_root);
  return snapshot;
}

const char* GuestBuildName(content::GuestBuild build) {
  return build == content::GuestBuild::kTitleUpdate6 ? "Title Update 6" : "base game";
}

}  // namespace crackdown
