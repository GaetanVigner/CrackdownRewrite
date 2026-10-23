#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <trg/launcher.h>
#include <trg/platform.h>

#include "content_state.h"

namespace crackdown {

enum class PackageKind { kNotPackage, kTitleUpdate, kDlc, kOther };

// What a file claims to be, from its STFS header alone: nothing is verified here.
PackageKind PeekPackageKind(const std::filesystem::path& file);

// The launcher's DLC page: Four-Play Pack, Title Update 6 and Free-For-All Pack, and what installs, removes and
// plays depends on each other. All file work runs on a worker thread.
class ContentPage {
 public:
  using StatusFn = std::function<void(const std::string& text, double seconds)>;

  ContentPage(std::filesystem::path user_dir, std::filesystem::path game_dir, bool update_build_available,
              std::string save_error, StatusFn status);
  ~ContentPage();

  void Draw(trg::Ui& ui);
  // Runs every frame so a finished install or removal is picked up whichever page is open.
  void Poll();
  bool Busy() const;
  // A package dropped on the launcher: it goes to the title update or DLC install by what its header says.
  void InstallPackage(const std::filesystem::path& package);

  content::ContentState State() const { return snapshot_.State(); }
  const std::string& SaveError() const { return save_error_; }
  // Free-For-All, or save data only Title Update 6 can read, is present but Title Update 6 cannot run: Play is refused.
  bool NeedsAttention() const { return content::UnsafeContent(State()); }

 private:
  struct Outcome;

  void Refresh();
  void BrowseAndInstall(const char* what, ContentKind expected);
  void StartInstall(const std::filesystem::path& package, bool update, ContentKind expected = ContentKind::kNone);
  void StartRemoveDlc(const DlcInfo& dlc);
  void StartRemoveUpdate();
  void Finish(const Outcome& outcome);
  void DrawRemoveDialog(trg::Ui& ui);

  std::filesystem::path user_dir_;
  std::filesystem::path game_dir_;
  bool update_build_available_;
  std::string save_error_;
  StatusFn status_;
  ContentSnapshot snapshot_;
  trg::Task task_;
  std::shared_ptr<Outcome> outcome_;
  std::string failure_;
  std::string failure_text_;
  std::string removing_;

  struct RemoveTarget {
    bool update = false;
    DlcInfo dlc;
    std::string name;
  };
  std::optional<RemoveTarget> remove_target_;
  bool remove_dialog_open_ = false;
};

}  // namespace crackdown
