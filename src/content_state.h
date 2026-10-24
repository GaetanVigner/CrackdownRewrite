#pragma once

#include <filesystem>
#include <vector>

#include "content_catalog.h"
#include "dlc.h"
#include "save_compat.h"
#include "title_update.h"

namespace crackdown {

// What is installed in the user data folder, and whether this executable carries the Title Update 6 guest.
struct ContentSnapshot {
  TitleUpdateCheck title_update;
  std::vector<DlcInfo> dlc;
  bool title_update_build_available = false;
  SaveCheck saves;
  bool save_switch_failed = false;

  content::ContentState State() const;
  // The pack's installed package that passed verification, or null.
  const DlcInfo* Find(content::DlcPack pack) const;
  // Any installed folder of the pack counts, verified or not.
  bool Installed(content::DlcPack pack) const;
};

ContentSnapshot ReadContentSnapshot(const std::filesystem::path& user_data_root, bool title_update_build_available,
                                    const TitleUpdateDescriptor& update = TitleUpdate6());

const char* GuestBuildName(content::GuestBuild build);

}  // namespace crackdown
