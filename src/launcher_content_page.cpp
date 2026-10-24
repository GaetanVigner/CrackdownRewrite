#include "launcher_content_page.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <utility>
#include <vector>

#include <imgui.h>

#include <rex/logging.h>

#include "platform.h"

namespace crackdown {
namespace {

using content::DlcPack;

constexpr const char* kTitleUpdateName = "Title Update 6";

enum class Tone { kNeutral, kGood, kWarn };

struct CardAction {
  const char* label;
  bool enabled = true;
  bool danger = false;
};

struct CardView {
  std::string title;
  std::string status;
  Tone tone = Tone::kNeutral;
  std::string detail;
  std::vector<CardAction> actions;  // drawn right to left in the order given
};

// The theme's only alert colours; destructive actions use them instead of the accent.
void PushDangerButton(const trg::Theme& t) {
  ImGui::PushStyleColor(ImGuiCol_Button, t.warn);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(t.warn.x + 0.12f, t.warn.y + 0.10f, t.warn.z + 0.05f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(t.warn.x - 0.05f, t.warn.y - 0.04f, t.warn.z - 0.02f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Text, t.text);
}

// Returns the index of the clicked action, or -1.
int DrawCard(trg::Ui& ui, const CardView& card, bool busy) {
  const trg::Theme& t = ui.theme();
  const float s = ui.scale();
  const float height = (card.detail.empty() ? 66.0f : 90.0f) * s;
  const float pad = 18.0f * s;
  const float width = ImGui::GetContentRegionAvail().x;
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImGui::Dummy(ImVec2(width, height));
  const ImVec2 next = ImGui::GetCursorScreenPos();
  dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                    ImGui::GetColorU32(ImVec4(t.frame.x, t.frame.y, t.frame.z, 0.5f)), 10 * s);

  ImGui::PushFont(ui.fonts().semibold, 0.0f);
  const float title_h = ImGui::GetTextLineHeight();
  dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(origin.x + pad, origin.y + 12 * s),
              ImGui::GetColorU32(t.text), card.title.c_str());
  ImGui::PopFont();

  ImGui::PushFont(ui.fonts().regular, ui.fonts().size * 0.84f);
  const float status_y = origin.y + 12 * s + title_h + 2 * s;
  const float line = ImGui::GetFontSize();
  if (card.tone == Tone::kGood) {
    const ImU32 green = ImGui::GetColorU32(t.good_dot);
    dl->AddCircleFilled(ImVec2(origin.x + pad + 4 * s, status_y + line * 0.5f), 4 * s, green);
    dl->AddText(ImGui::GetFont(), line, ImVec2(origin.x + pad + 14 * s, status_y), green, card.status.c_str());
  } else {
    const ImU32 color = ImGui::GetColorU32(card.tone == Tone::kWarn ? t.warn_dot : t.dim);
    dl->AddText(ImGui::GetFont(), line, ImVec2(origin.x + pad, status_y), color, card.status.c_str());
  }
  if (!card.detail.empty()) {
    dl->AddText(ImGui::GetFont(), line, ImVec2(origin.x + pad, status_y + line + 4 * s), ImGui::GetColorU32(t.dim),
                card.detail.c_str());
  }
  ImGui::PopFont();

  int clicked = -1;
  const float button_w = 150.0f * s;
  const float button_h = ImGui::GetFrameHeight();
  float x = origin.x + width - pad - button_w;
  for (size_t i = 0; i < card.actions.size(); ++i) {
    const CardAction& action = card.actions[i];
    ImGui::SetCursorScreenPos(ImVec2(x, origin.y + (66 * s - button_h) * 0.5f));
    ImGui::PushID(card.title.c_str());
    ImGui::PushID(action.label);
    ImGui::BeginDisabled(busy || !action.enabled);
    bool pressed = false;
    if (action.danger) {
      PushDangerButton(t);
      pressed = ImGui::Button(action.label, ImVec2(button_w, button_h));
      ImGui::PopStyleColor(4);
    } else {
      pressed = ui.AccentButton(action.label, ImVec2(button_w, button_h));
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    ImGui::PopID();
    if (pressed) {
      clicked = int(i);
    }
    x -= button_w + 10 * s;
  }
  ImGui::SetCursorScreenPos(next);
  ImGui::Dummy(ImVec2(0, 8 * s));
  return clicked;
}

void DrawFailure(trg::Ui& ui, const std::string& title, const std::string& text) {
  ui.Spacer(4);
  ImGui::PushStyleColor(ImGuiCol_Text, ui.theme().warn_dot);
  ImGui::TextUnformatted(title.c_str());
  ImGui::PopStyleColor();
  ui.Help(text.c_str());
  ui.Spacer(4);
}

}  // namespace

struct ContentPage::Outcome {
  enum class Kind { kInstallDlc, kInstallUpdate, kRemoveDlc, kRemoveUpdate };
  Kind kind = Kind::kInstallDlc;
  ContentResult result;
  DlcInfo dlc;
  std::string name;
};

PackageKind PeekPackageKind(const std::filesystem::path& file) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(file, ec)) {
    return PackageKind::kNotPackage;
  }
  std::ifstream in(file, std::ios::binary);
  unsigned char header[0x348] = {};
  in.read(reinterpret_cast<char*>(header), sizeof(header));
  if (size_t(in.gcount()) < sizeof(header) ||
      (std::memcmp(header, "LIVE", 4) != 0 && std::memcmp(header, "PIRS", 4) != 0 &&
       std::memcmp(header, "CON ", 4) != 0)) {
    return PackageKind::kNotPackage;
  }
  const uint32_t type = uint32_t(header[0x344]) << 24 | uint32_t(header[0x345]) << 16 | uint32_t(header[0x346]) << 8 |
                        header[0x347];
  if (type == content::kTitleUpdateContentType) {
    return PackageKind::kTitleUpdate;
  }
  return type == content::kMarketplaceContentType ? PackageKind::kDlc : PackageKind::kOther;
}

ContentPage::ContentPage(std::filesystem::path user_dir, std::filesystem::path game_dir, bool update_build_available,
                         std::string save_error, StatusFn status)
    : user_dir_(std::move(user_dir)),
      game_dir_(std::move(game_dir)),
      update_build_available_(update_build_available),
      save_error_(std::move(save_error)),
      status_(std::move(status)) {
  Refresh();
}

ContentPage::~ContentPage() = default;

void ContentPage::Refresh() {
  snapshot_ = ReadContentSnapshot(user_dir_, update_build_available_);
  snapshot_.save_switch_failed = !save_error_.empty();
}

bool ContentPage::Busy() const {
  return task_.running();
}

void ContentPage::Draw(trg::Ui& ui) {
  ui.Paragraph(
      "Optional downloadable content is not included with this port. Install it from your own Xbox 360 packages.");
  ui.Spacer(8);
  const bool busy = task_.running();
  const content::ContentState state = State();

  if (!save_error_.empty()) {
    DrawFailure(ui, "Your saves could not be switched", save_error_);
  }
  if (busy) {
    ui.TaskProgress(task_);
    ui.Spacer(6);
  } else if (!removing_.empty()) {
    ui.Help(("Removing " + removing_ + "...").c_str());
  }

  {
    const DlcInfo* installed = snapshot_.Find(DlcPack::kFourPlay);
    CardView card;
    card.title = content::DlcName(DlcPack::kFourPlay);
    card.status = installed ? "Installed" : "Not installed";
    card.tone = installed ? Tone::kGood : Tone::kNeutral;
    card.actions.push_back({installed ? "Remove" : "Install DLC...", true, installed != nullptr});
    const int clicked = DrawCard(ui, card, busy);
    if (clicked == 0) {
      if (installed) {
        remove_target_ = RemoveTarget{false, *installed, card.title};
        remove_dialog_open_ = true;
      } else {
        BrowseAndInstall("Select your Four-Play Pack package", ContentKind::kFourPlay);
      }
    }
  }

  {
    const bool installed = snapshot_.title_update.state == TitleUpdateState::kInstalled;
    const bool damaged = snapshot_.title_update.state == TitleUpdateState::kDamaged;
    const bool blocked = !content::CanRemoveTitleUpdate(state);
    CardView card;
    card.title = kTitleUpdateName;
    if (!update_build_available_) {
      card.status = "Not available in this build";
    } else if (installed) {
      card.status = "Installed";
      card.tone = Tone::kGood;
    } else if (damaged) {
      card.status = "Needs reinstalling";
      card.tone = Tone::kWarn;
    } else {
      card.status = "Not installed";
    }
    if (blocked && (installed || damaged)) {
      card.detail = "Remove Free-For-All Pack before removing Title Update 6.";
    } else if (state.title_update_usable) {
      card.detail = "The Jump Height cheat is not available with it.";
    }
    if (installed) {
      card.actions.push_back({"Remove", !blocked, true});
    } else {
      card.actions.push_back({"Install Update...", update_build_available_});
      if (damaged) {
        card.actions.push_back({"Remove", !blocked, true});
      }
    }
    const int clicked = DrawCard(ui, card, busy);
    if (clicked >= 0) {
      const std::string label = card.actions[size_t(clicked)].label;
      if (label == "Remove") {
        remove_target_ = RemoveTarget{true, {}, card.title};
        remove_dialog_open_ = true;
      } else {
        BrowseAndInstall("Select your Crackdown Title Update 6 package", ContentKind::kTitleUpdate);
      }
    }
  }

  {
    const DlcInfo* installed = snapshot_.Find(DlcPack::kFreeForAll);
    const bool any = snapshot_.Installed(DlcPack::kFreeForAll);
    const bool can_install = content::CanInstallDlc(DlcPack::kFreeForAll, state);
    CardView card;
    card.title = content::DlcName(DlcPack::kFreeForAll);
    if (any) {
      card.status = "Installed";
      card.tone = state.title_update_usable ? Tone::kGood : Tone::kWarn;
      if (!state.title_update_usable) {
        card.detail = "Requires Title Update 6. Play stays unavailable until it is installed or this pack is removed.";
      }
      card.actions.push_back({"Remove", true, true});
    } else if (!can_install) {
      card.status = "Requires Title Update 6";
      card.tone = Tone::kWarn;
      card.actions.push_back({"Install DLC...", false});
    } else {
      card.status = "Not installed";
      card.actions.push_back({"Install DLC...", true});
    }
    const int clicked = DrawCard(ui, card, busy);
    if (clicked == 0) {
      if (any) {
        RemoveTarget target{false, installed ? *installed : DlcInfo{}, card.title};
        if (!installed) {
          for (const DlcInfo& dlc : snapshot_.dlc) {
            if (dlc.pack == DlcPack::kFreeForAll) {
              target.dlc = dlc;
            }
          }
        }
        remove_target_ = std::move(target);
        remove_dialog_open_ = true;
      } else {
        BrowseAndInstall("Select your Free-For-All Pack package", ContentKind::kFreeForAll);
      }
    }
  }

  if (!failure_.empty()) {
    DrawFailure(ui, failure_, failure_text_);
  }
  DrawRemoveDialog(ui);
}

void ContentPage::BrowseAndInstall(const char* what, ContentKind expected) {
  Poll();
  if (task_.running()) {
    return;
  }
  const std::string picked = trg::BrowseForFile(what, {{"Xbox 360 package", "*"}});
  if (!picked.empty()) {
    StartInstall(PathFromUtf8(picked), expected == ContentKind::kTitleUpdate, expected);
  }
}

void ContentPage::InstallPackage(const std::filesystem::path& package) {
  switch (PeekPackageKind(package)) {
    case PackageKind::kTitleUpdate:
      StartInstall(package, true);
      return;
    case PackageKind::kDlc:
      StartInstall(package, false);
      return;
    case PackageKind::kOther:
    case PackageKind::kNotPackage:
      break;
  }
  const ContentMessage message = DescribeContent(ContentStatus::kNotDlcPackage);
  failure_ = message.title;
  failure_text_ = message.text;
  status_("Install failed: " + failure_, 8.0);
}

void ContentPage::StartInstall(const std::filesystem::path& package, bool update, ContentKind expected) {
  Poll();
  if (task_.running()) {
    return;
  }
  failure_.clear();
  failure_text_.clear();
  outcome_ = std::make_shared<Outcome>();
  outcome_->kind = update ? Outcome::Kind::kInstallUpdate : Outcome::Kind::kInstallDlc;
  const bool usable = State().title_update_usable;
  const bool started = task_.Start(
      "Reading the package...",
      [outcome = outcome_, package, user_dir = user_dir_, usable, update, expected](trg::Task& task) {
        InstallHooks hooks;
        hooks.progress = [&task](const InstallProgress& progress) {
          task.SetLabel(progress.phase);
          task.Progress(static_cast<long long>(progress.done_bytes), static_cast<long long>(progress.total_bytes));
          return !task.cancelled();
        };
        outcome->result = update ? InstallTitleUpdate(package, user_dir, hooks)
                                 : InstallDlc(package, user_dir, usable, hooks, &outcome->dlc, {}, PackOfKind(expected));
        return outcome->result.ok() ? std::string() : DescribeContent(outcome->result).title;
      });
  if (started) {
    REXLOG_INFO("Installing {} from {}", update ? "the title update" : "DLC", package.filename().string());
  } else {
    outcome_.reset();
  }
}

void ContentPage::StartRemoveDlc(const DlcInfo& dlc) {
  Poll();
  if (task_.running()) {
    return;
  }
  failure_.clear();
  failure_text_.clear();
  outcome_ = std::make_shared<Outcome>();
  outcome_->kind = Outcome::Kind::kRemoveDlc;
  outcome_->name = dlc.pack != DlcPack::kUnknown ? content::DlcName(dlc.pack) : dlc.display_name;
  const bool started = task_.Start(
      "Removing...", [outcome = outcome_, user_dir = user_dir_, game_dir = game_dir_, id = dlc.content_id](trg::Task&) {
        outcome->result = RemoveDlc(user_dir, game_dir, id);
        return outcome->result.ok() ? std::string() : DescribeContent(outcome->result.status).title;
      });
  if (started) {
    removing_ = outcome_->name;
    REXLOG_INFO("Removing DLC {}", dlc.content_id);
  } else {
    outcome_.reset();
  }
}

void ContentPage::StartRemoveUpdate() {
  Poll();
  if (task_.running()) {
    return;
  }
  failure_.clear();
  failure_text_.clear();
  outcome_ = std::make_shared<Outcome>();
  outcome_->kind = Outcome::Kind::kRemoveUpdate;
  outcome_->name = kTitleUpdateName;
  const bool started = task_.Start(
      "Removing...", [outcome = outcome_, user_dir = user_dir_, game_dir = game_dir_](trg::Task&) {
        outcome->result = RemoveTitleUpdate(user_dir, game_dir);
        return outcome->result.ok() ? std::string() : DescribeContent(outcome->result.status).title;
      });
  if (started) {
    removing_ = outcome_->name;
    REXLOG_INFO("Removing the title update");
  } else {
    outcome_.reset();
  }
}

void ContentPage::Poll() {
  std::string error;
  if (task_.Finish(&error) == trg::Task::State::kIdle) {
    return;
  }
  removing_.clear();
  Refresh();
  const std::shared_ptr<Outcome> outcome = std::move(outcome_);
  if (outcome) {
    Finish(*outcome);
  }
}

void ContentPage::Finish(const Outcome& outcome) {
  const bool removal = outcome.kind == Outcome::Kind::kRemoveDlc || outcome.kind == Outcome::Kind::kRemoveUpdate;
  const ContentResult& result = outcome.result;
  if (result.ok()) {
    std::string name = outcome.name;
    if (outcome.kind == Outcome::Kind::kInstallDlc) {
      name = outcome.dlc.pack != DlcPack::kUnknown ? content::DlcName(outcome.dlc.pack) : outcome.dlc.display_name;
    } else if (outcome.kind == Outcome::Kind::kInstallUpdate) {
      name = kTitleUpdateName;
    }
    REXLOG_INFO("{} {}", name, removal ? "removed" : "installed");
    std::string text = name + (removal ? " removed." : " installed.");
    if (outcome.kind == Outcome::Kind::kInstallUpdate) {
      text += " Free-For-All Pack can be installed now.";
    }
    status_(text, 6.0);
    return;
  }
  const ContentMessage message = DescribeContent(result);
  if (result.status == ContentStatus::kCancelled) {
    REXLOG_INFO("Install cancelled");
    status_(message.title + ". " + message.text, 6.0);
    return;
  }
  REXLOG_WARN("{} failed: {} ({})", removal ? "Removal" : "Install", message.title, result.detail);
  failure_ = message.title;
  failure_text_ = message.text;
  status_(std::string(removal ? "Removal failed: " : "Install failed: ") + failure_, 8.0);
}

void ContentPage::DrawRemoveDialog(trg::Ui& ui) {
  if (!remove_target_) {
    return;
  }
  constexpr const char* kId = "##content_remove";
  if (remove_dialog_open_) {
    ImGui::OpenPopup(kId);
    remove_dialog_open_ = false;
  }
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float s = ui.scale();
  const trg::Theme& t = ui.theme();
  ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(std::min(520 * s, vp->Size.x - 40 * s), 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24 * s, 20 * s));
  ImGui::PushStyleColor(ImGuiCol_PopupBg, t.panel);
  ImGui::PushStyleColor(ImGuiCol_Border, t.warn);
  const bool open = ImGui::BeginPopupModal(kId, nullptr,
                                           ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar();
  if (!open) {
    remove_target_.reset();
    return;
  }

  const RemoveTarget target = *remove_target_;
  const bool saves_switch = target.update && content::RemovalSwitchesSaves(snapshot_.State());
  ImGui::PushFont(ui.fonts().semibold, ui.fonts().size * 1.1f);
  ImGui::TextUnformatted(("Remove " + target.name).c_str());
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, 6 * s));
  ImGui::PushStyleColor(ImGuiCol_Text, t.dim);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(saves_switch
                             ? "Your Title Update 6 progress will be preserved.\n"
                               "If no base-game save exists, the base game will start with a new save.\n"
                               "You can install the package again later."
                         : target.update
                             ? "This removes the installed update from this PC. Your saves are not touched.\n"
                               "You can install the package again later."
                             : "This removes the installed files from this PC. Your saves are not touched.\n"
                               "You can install the package again later.");
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
  ImGui::Dummy(ImVec2(0, 14 * s));

  bool cancel = ImGui::Button("Cancel", ImVec2(150 * s, 0));
  ImGui::SetItemDefaultFocus();
  ImGui::SameLine();
  PushDangerButton(t);
  const bool remove = ImGui::Button("Remove", ImVec2(150 * s, 0));
  ImGui::PopStyleColor(4);

  cancel |= ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
  if (remove) {
    remove_target_.reset();
    ImGui::CloseCurrentPopup();
    if (target.update) {
      StartRemoveUpdate();
    } else {
      StartRemoveDlc(target.dlc);
    }
  } else if (cancel) {
    remove_target_.reset();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

}  // namespace crackdown
