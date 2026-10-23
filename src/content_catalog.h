#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace crackdown::content {

inline constexpr uint32_t kTitleId = 0x4D5307DC;
inline constexpr uint32_t kMarketplaceContentType = 0x00000002;
inline constexpr uint32_t kTitleUpdateContentType = 0x000B0000;
inline constexpr uint32_t kMediaId = 0x596F9615;

// The disc build the port is recompiled from is 0.0.0.1; Title Update 6 takes it to 0.0.6.1.
inline constexpr uint32_t kBaseVersion = 0x00000001;
inline constexpr uint32_t kTitleUpdate6Version = 0x00000601;
inline constexpr uint64_t kTitleUpdate6XexpSize = 1853440;
inline constexpr std::array<uint8_t, 32> kTitleUpdate6XexpSha256 = {
    0xA0, 0xD4, 0xFE, 0x06, 0xD6, 0xA4, 0xA2, 0xFD, 0x96, 0xD1, 0x8F, 0x5D, 0xE4, 0x26, 0xF7, 0x61,
    0xFE, 0x1B, 0x8C, 0xCE, 0x5D, 0x5B, 0xC7, 0xF9, 0x81, 0xD7, 0x38, 0xC3, 0x51, 0x9C, 0x81, 0x9E,
};

// These names are case-sensitive: Linux file systems are.
inline constexpr char kBaseXexName[] = "default.xex";
inline constexpr char kUpdateXexpName[] = "Default.xexp";
inline constexpr char kUpdateStringsName[] = "GameStrings_DLC.str";
inline constexpr char kUpdateLocalFolder[] = "Local";
inline constexpr std::string_view kUpdateLanguages[] = {"cs-CZ", "de-DE", "en-US", "es-ES", "es-LA", "fr-FR", "hu-HU",
                                                        "it-IT", "ja-JP", "ko-KR", "pl-PL", "sk-SK", "zh-TW"};
inline constexpr char kRequiredUpdateLanguage[] = "en-US";

enum class DlcPack { kUnknown, kFourPlay, kFreeForAll };
inline constexpr DlcPack kDlcPacks[] = {DlcPack::kFourPlay, DlcPack::kFreeForAll};

struct DlcIdentity {
  DlcPack pack;
  const char* name;
  const char* content_id;     // upper-case hex of the package's content ID, also its folder name
  const char* marker_file;    // a package path only this pack carries
};
inline constexpr DlcIdentity kDlcIdentities[] = {
    {DlcPack::kFourPlay, "Four-Play Pack", "E4DDBC100F97B6954B84E82ED807E79B5A4314C0", "Streaming/FullCity_DLCAgents.resblock"},
    {DlcPack::kFreeForAll, "Free-For-All Pack", "AA10729E62721B4EAF674486684E31CD9B94AC4F", "Digest2.txt"},
};

inline const DlcIdentity* IdentityOf(DlcPack pack) {
  for (const DlcIdentity& identity : kDlcIdentities) {
    if (identity.pack == pack) {
      return &identity;
    }
  }
  return nullptr;
}

inline const char* DlcName(DlcPack pack) {
  const DlcIdentity* identity = IdentityOf(pack);
  return identity ? identity->name : "";
}

// Free-For-All's Digest2.txt, Cheat Mode and Street Racing exist only in the Title Update 6 guest code.
inline constexpr bool DlcNeedsTitleUpdate(DlcPack pack) {
  return pack == DlcPack::kFreeForAll;
}

enum class GuestBuild { kBase, kTitleUpdate6 };

// What the save and profile files in the user data folder need to be read; see save_compat.h.
enum class SaveCompatibility { kNoSave, kBaseCompatible, kRequiresTitleUpdate6, kInvalidOrUnknown };

struct ContentState {
  bool title_update_usable = false;  // installed, verified, and this release carries the Title Update 6 build
  bool title_update_installed = false;
  bool free_for_all_installed = false;
  SaveCompatibility save = SaveCompatibility::kNoSave;
  bool save_switch_failed = false;    // the saves could not be switched to the selected game version
};

inline constexpr bool CanInstallDlc(DlcPack pack, const ContentState& state) {
  return !DlcNeedsTitleUpdate(pack) || state.title_update_usable;
}

// Removing the update under an installed Free-For-All Pack would leave the pack mounted against the base game.
inline constexpr bool CanRemoveTitleUpdate(const ContentState& state) {
  return !state.free_for_all_installed;
}

inline constexpr GuestBuild SelectGuestBuild(const ContentState& state) {
  return state.title_update_usable ? GuestBuild::kTitleUpdate6 : GuestBuild::kBase;
}

// Removing the update puts the live Title Update 6 saves aside and the base game gets its own saves.
inline constexpr bool RemovalSwitchesSaves(const ContentState& state) {
  return state.save == SaveCompatibility::kRequiresTitleUpdate6;
}

// Content the selected build cannot use safely: Play must be refused and the launcher shown.
inline constexpr bool UnsafeContent(const ContentState& state) {
  return (state.free_for_all_installed && !state.title_update_usable) || state.save_switch_failed;
}

}  // namespace crackdown::content
