#pragma once

#include <filesystem>

#include "content_catalog.h"

namespace crackdown {

struct SaveCheck {
  content::SaveCompatibility profile = content::SaveCompatibility::kNoSave;
  content::SaveCompatibility campaign = content::SaveCompatibility::kNoSave;
  content::SaveCompatibility combined = content::SaveCompatibility::kNoSave;
};

inline constexpr char kProfileBlobName[] = "63E83FFF";
inline constexpr char kCampaignSaveName[] = "Crackdown.sav";

std::filesystem::path ProfileDirPath(const std::filesystem::path& user_data_root);
std::filesystem::path ProfileBlobPath(const std::filesystem::path& user_data_root);
std::filesystem::path CampaignContainerPath(const std::filesystem::path& user_data_root);
std::filesystem::path CampaignSavePath(const std::filesystem::path& user_data_root);
std::filesystem::path CampaignHeaderPath(const std::filesystem::path& user_data_root);
std::filesystem::path SaveVersionsPath(const std::filesystem::path& user_data_root);

// Read-only: only chunk headers are read, and nothing is written or repaired.
content::SaveCompatibility ClassifyProfileBlob(const std::filesystem::path& file);
content::SaveCompatibility ClassifyCampaignSave(const std::filesystem::path& file);

// A file that needs Title Update 6 decides the result even when the other one is malformed.
content::SaveCompatibility CombineSaveCompatibility(content::SaveCompatibility profile,
                                                    content::SaveCompatibility campaign);

SaveCheck CheckSaves(const std::filesystem::path& user_data_root);

const char* SaveCompatibilityName(content::SaveCompatibility compatibility);

}  // namespace crackdown
