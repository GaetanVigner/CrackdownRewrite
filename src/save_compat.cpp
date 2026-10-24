#include "save_compat.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>

namespace crackdown {

namespace {

using content::SaveCompatibility;

constexpr uint32_t kChunkMagic = 0x4352414B;  // "CRAK"
constexpr uint32_t kBaseVersion = 4;
constexpr uint32_t kBaseMaxChunkType = 23;
constexpr uint32_t kTerminatorType = 1;
constexpr uint64_t kChunkHeaderSize = 20;

// The user the runtime signs in, and the setting and content folder names the title writes under it.
constexpr char kProfileXuid[] = "B13EBABEBABEBABE";
constexpr char kProfileUserName[] = "User";
constexpr char kSavegameContentType[] = "00000001";

struct ChunkHeader {
  uint32_t magic = 0;
  uint32_t version = 0;
  uint32_t type = 0;
  uint32_t checksum = 0;
  uint32_t size = 0;
};

uint32_t ReadBigEndian32(const unsigned char* bytes) {
  return uint32_t(bytes[0]) << 24 | uint32_t(bytes[1]) << 16 | uint32_t(bytes[2]) << 8 | uint32_t(bytes[3]);
}

bool ReadChunkHeader(std::istream& in, ChunkHeader& header) {
  std::array<unsigned char, kChunkHeaderSize> bytes{};
  in.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
  if (in.gcount() != std::streamsize(bytes.size())) {
    return false;
  }
  header = {ReadBigEndian32(&bytes[0]), ReadBigEndian32(&bytes[4]), ReadBigEndian32(&bytes[8]),
            ReadBigEndian32(&bytes[12]), ReadBigEndian32(&bytes[16])};
  return true;
}

bool NeedsTitleUpdate(const ChunkHeader& header) {
  return header.version > kBaseVersion || header.type > kBaseMaxChunkType;
}

std::string Hex8(uint32_t value) {
  char text[9];
  std::snprintf(text, sizeof(text), "%08X", value);
  return text;
}

}  // namespace

std::filesystem::path ProfileDirPath(const std::filesystem::path& user_data_root) {
  return user_data_root / Hex8(content::kTitleId) / "profile" / kProfileUserName;
}

std::filesystem::path ProfileBlobPath(const std::filesystem::path& user_data_root) {
  return ProfileDirPath(user_data_root) / kProfileBlobName;
}

std::filesystem::path CampaignContainerPath(const std::filesystem::path& user_data_root) {
  return user_data_root / kProfileXuid / Hex8(content::kTitleId) / kSavegameContentType / kCampaignSaveName;
}

std::filesystem::path CampaignSavePath(const std::filesystem::path& user_data_root) {
  return CampaignContainerPath(user_data_root) / kCampaignSaveName;
}

std::filesystem::path CampaignHeaderPath(const std::filesystem::path& user_data_root) {
  return user_data_root / kProfileXuid / Hex8(content::kTitleId) / "Headers" / kSavegameContentType /
         (std::string(kCampaignSaveName) + ".header");
}

std::filesystem::path SaveVersionsPath(const std::filesystem::path& user_data_root) {
  return user_data_root / Hex8(content::kTitleId) / "save-versions";
}

SaveCompatibility ClassifyProfileBlob(const std::filesystem::path& file) {
  std::error_code ec;
  if (!std::filesystem::exists(file, ec)) {
    return SaveCompatibility::kNoSave;
  }
  if (!std::filesystem::is_regular_file(file, ec)) {
    return SaveCompatibility::kInvalidOrUnknown;
  }
  std::ifstream in(file, std::ios::binary);
  std::array<unsigned char, 8> bytes{};
  in.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
  if (in.gcount() != std::streamsize(bytes.size()) || ReadBigEndian32(&bytes[0]) != kChunkMagic) {
    return SaveCompatibility::kInvalidOrUnknown;
  }
  return ReadBigEndian32(&bytes[4]) > kBaseVersion ? SaveCompatibility::kRequiresTitleUpdate6
                                                   : SaveCompatibility::kBaseCompatible;
}

SaveCompatibility ClassifyCampaignSave(const std::filesystem::path& file) {
  std::error_code ec;
  if (!std::filesystem::exists(file, ec)) {
    return SaveCompatibility::kNoSave;
  }
  const uint64_t length = std::filesystem::is_regular_file(file, ec) ? std::filesystem::file_size(file, ec) : 0;
  std::ifstream in(file, std::ios::binary);
  if (ec || !in || length == 0) {
    return SaveCompatibility::kInvalidOrUnknown;
  }
  bool needs_title_update = false;
  ChunkHeader header;
  uint64_t offset = 0;
  while (offset < length) {
    if (length - offset < kChunkHeaderSize || !ReadChunkHeader(in, header) || header.magic != kChunkMagic ||
        header.size > length - offset - kChunkHeaderSize) {
      return SaveCompatibility::kInvalidOrUnknown;
    }
    needs_title_update |= NeedsTitleUpdate(header);
    offset += kChunkHeaderSize + header.size;
    if (offset == length) {
      break;
    }
    in.seekg(std::streamoff(offset), std::ios::beg);
  }
  if (offset != length || header.type != kTerminatorType || header.size != 0) {
    return SaveCompatibility::kInvalidOrUnknown;
  }
  return needs_title_update ? SaveCompatibility::kRequiresTitleUpdate6 : SaveCompatibility::kBaseCompatible;
}

SaveCompatibility CombineSaveCompatibility(SaveCompatibility profile, SaveCompatibility campaign) {
  const std::array<SaveCompatibility, 2> files = {profile, campaign};
  for (const SaveCompatibility priority : {SaveCompatibility::kRequiresTitleUpdate6, SaveCompatibility::kInvalidOrUnknown,
                                           SaveCompatibility::kBaseCompatible}) {
    for (const SaveCompatibility file : files) {
      if (file == priority) {
        return priority;
      }
    }
  }
  return SaveCompatibility::kNoSave;
}

SaveCheck CheckSaves(const std::filesystem::path& user_data_root) {
  SaveCheck check;
  check.profile = ClassifyProfileBlob(ProfileBlobPath(user_data_root));
  check.campaign = ClassifyCampaignSave(CampaignSavePath(user_data_root));
  check.combined = CombineSaveCompatibility(check.profile, check.campaign);
  return check;
}

const char* SaveCompatibilityName(SaveCompatibility compatibility) {
  switch (compatibility) {
    case SaveCompatibility::kNoSave:
      return "no save";
    case SaveCompatibility::kBaseCompatible:
      return "base-compatible";
    case SaveCompatibility::kRequiresTitleUpdate6:
      return "requires Title Update 6";
    case SaveCompatibility::kInvalidOrUnknown:
      return "unknown";
  }
  return "unknown";
}

}  // namespace crackdown
