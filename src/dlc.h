#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "content_catalog.h"
#include "content_fs.h"
#include "content_install.h"

namespace crackdown {

// A DLC package installed under the user data folder, where the SDK's content manager finds it.
struct DlcInfo {
  std::string content_id;    // 40 hex digits: the package's content ID and its folder name
  std::string display_name;  // UTF-8, from the package metadata; the content ID when the header has none
  uint32_t license_mask = 0;
  content::DlcPack pack = content::DlcPack::kUnknown;
  bool verified = false;  // the header names this title, content type and content ID; only then can it be removed
};

// <user data>/0000000000000000/4D5307DC/00000002 and the Headers folder beside it: the SDK's Marketplace layout.
std::filesystem::path DlcFolder(const std::filesystem::path& user_data_root);
std::filesystem::path DlcHeaderPath(const std::filesystem::path& user_data_root, const std::string& content_id);

// The pack a package identifies: by its content ID when that is a known one, else by its English display name.
// The caller has already checked the title ID and content type.
content::DlcPack ClassifyDlc(std::string_view content_id, std::string_view display_name);

// Installs a DLC package for this title as Marketplace content under `user_data_root`, keyed by its content ID.
// Other DLC, the game files, the title update and the player's saves are not touched. A failed or cancelled install
// leaves the installed copy as it was. `title_update_usable` gates the packs that need Title Update 6.
// A known `expected` pack rejects any other recognised package with kWrongPackage before any pack rule applies.
ContentResult InstallDlc(const std::filesystem::path& package, const std::filesystem::path& user_data_root,
                         bool title_update_usable, const InstallHooks& hooks = {}, DlcInfo* installed = nullptr,
                         const FileOps& ops = {}, content::DlcPack expected = content::DlcPack::kUnknown);

content::DlcPack ClassifyDlcName(std::string_view content_id, const std::u16string& display_name);
content::DlcPack PackOfKind(ContentKind kind);
ContentKind KindOfPack(content::DlcPack pack);

std::vector<DlcInfo> ListInstalledDlc(const std::filesystem::path& user_data_root);

// Removes the content folder and the header of one installed pack and nothing else. Only packs that ListInstalledDlc
// reports as verified are removed. `game_dir` must not be, or be inside, the removed folder.
ContentResult RemoveDlc(const std::filesystem::path& user_data_root, const std::filesystem::path& game_dir,
                        const std::string& content_id, const FileOps& ops = {});

}  // namespace crackdown
