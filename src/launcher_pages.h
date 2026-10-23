#pragma once

#include <array>

namespace crackdown {

enum class LauncherPage { kPlay, kDlc, kDisplay, kGraphics, kGameplay, kCheats, kControls, kAchievements, kAbout };

inline constexpr std::array kLauncherPageOrder = {
    LauncherPage::kPlay,     LauncherPage::kDlc,      LauncherPage::kDisplay,      LauncherPage::kGraphics,
    LauncherPage::kGameplay, LauncherPage::kCheats,   LauncherPage::kControls,     LauncherPage::kAchievements,
    LauncherPage::kAbout,
};

}  // namespace crackdown
