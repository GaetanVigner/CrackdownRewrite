#include "startup_selection.h"

#include "content_state.h"

namespace crackdown {

StartupSelection SelectStartup(const UserDataRequest& request, bool title_update_guest_linked,
                               const UserDataFileOps& ops, const TitleUpdateDescriptor& update,
                               const SaveFileOps& save_ops) {
  StartupSelection selection;
  selection.user_data = SelectUserData(request, ops);
  if (selection.user_data.outcome == UserDataOutcome::kNotWritable) {
    return selection;
  }
  selection.content = ReadContentSnapshot(selection.user_data.root, title_update_guest_linked, update).State();
  selection.build = content::SelectGuestBuild(selection.content);
  selection.save_switch =
      PrepareSaveGeneration(selection.user_data.root, GenerationFor(selection.build), save_ops);
  ContentSnapshot snapshot = ReadContentSnapshot(selection.user_data.root, title_update_guest_linked, update);
  snapshot.save_switch_failed = selection.save_switch.blocks_play();
  selection.content = snapshot.State();
  return selection;
}

}  // namespace crackdown
