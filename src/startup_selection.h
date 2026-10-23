#pragma once

#include "content_catalog.h"
#include "save_generation.h"
#include "title_update.h"
#include "user_data.h"

namespace crackdown {

// What a process decides before the runtime exists: the player data folder, and from what is installed in it, the
// recompiled guest to run. The same user data folder is then used for the whole run.
struct StartupSelection {
  UserDataSelection user_data;
  content::ContentState content;
  content::GuestBuild build = content::GuestBuild::kBase;
  SaveSwitchResult save_switch;
};

// Title Update 6 is selected only when it is installed, verified, and `title_update_guest_linked`. The live saves are
// then switched to the selected game version's. An unwritable user data folder selects the base game and reads
// nothing; the caller reports it.
StartupSelection SelectStartup(const UserDataRequest& request, bool title_update_guest_linked,
                               const UserDataFileOps& ops = {}, const TitleUpdateDescriptor& update = TitleUpdate6(),
                               const SaveFileOps& save_ops = {});

}  // namespace crackdown
