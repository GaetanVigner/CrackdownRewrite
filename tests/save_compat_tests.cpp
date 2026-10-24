// Which game version can read the profile and campaign save files, decided from chunk headers only.

#include <cstdint>
#include <string>
#include <vector>

#include "check.h"
#include "save_compat.h"

namespace fs = std::filesystem;
using namespace crackdown;
using content::SaveCompatibility;

namespace {

using Bytes = std::vector<unsigned char>;

void PutBe32(Bytes& out, uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8) {
    out.push_back(static_cast<unsigned char>(value >> shift));
  }
}

Bytes Chunk(uint32_t version, uint32_t type, const Bytes& payload = {}, uint32_t magic = 0x4352414B) {
  Bytes out;
  PutBe32(out, magic);
  PutBe32(out, version);
  PutBe32(out, type);
  uint32_t checksum = 0;
  for (const unsigned char byte : payload) {
    checksum ^= byte;
  }
  PutBe32(out, checksum);
  PutBe32(out, uint32_t(payload.size()));
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

Bytes Concat(std::initializer_list<Bytes> parts) {
  Bytes out;
  for (const Bytes& part : parts) {
    out.insert(out.end(), part.begin(), part.end());
  }
  return out;
}

struct Files {
  fs::path Write(const char* name, const Bytes& bytes) const {
    const fs::path file = dir.path() / name;
    check::WriteText(file, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    return file;
  }
  check::TempDir dir;
};

SaveCompatibility Campaign(const Bytes& bytes) {
  Files files;
  return ClassifyCampaignSave(files.Write("save", bytes));
}

SaveCompatibility Profile(const Bytes& bytes) {
  Files files;
  return ClassifyProfileBlob(files.Write("profile", bytes));
}

const Bytes kPayload = {1, 2, 3, 4, 5};

void TestProfile() {
  std::printf("the profile blob is classified from its magic and version\n");
  Files files;
  CHECK(ClassifyProfileBlob(files.dir.path() / "absent") == SaveCompatibility::kNoSave);
  CHECK(Profile(Concat({Chunk(4, 21, kPayload), Chunk(4, 1)})) == SaveCompatibility::kBaseCompatible);
  CHECK(Profile(Concat({Chunk(5, 21, kPayload), Chunk(5, 1)})) == SaveCompatibility::kRequiresTitleUpdate6);
  CHECK(Profile({'C', 'R', 'A', 'K', 0, 0, 0}) == SaveCompatibility::kInvalidOrUnknown);
  CHECK(Profile({}) == SaveCompatibility::kInvalidOrUnknown);
  CHECK(Profile(Concat({Chunk(5, 21, kPayload, 0x4E4F5045)})) == SaveCompatibility::kInvalidOrUnknown);
  fs::create_directories(files.dir.path() / "folder");
  CHECK(ClassifyProfileBlob(files.dir.path() / "folder") == SaveCompatibility::kInvalidOrUnknown);
}

void TestCampaign() {
  std::printf("the campaign save needs Title Update 6 for a newer version or chunk type\n");
  const Bytes terminator = Chunk(4, 1);
  CHECK(ClassifyCampaignSave(Files().dir.path() / "absent") == SaveCompatibility::kNoSave);
  CHECK(Campaign(Concat({Chunk(4, 4, {1, 2}), Chunk(4, 23, kPayload), terminator})) ==
        SaveCompatibility::kBaseCompatible);
  CHECK(Campaign(Concat({Chunk(4, 4), Chunk(5, 5, kPayload), terminator})) == SaveCompatibility::kRequiresTitleUpdate6);
  CHECK(Campaign(Concat({Chunk(6, 4, kPayload), Chunk(6, 1)})) == SaveCompatibility::kRequiresTitleUpdate6);
  CHECK(Campaign(Concat({Chunk(4, 24, kPayload), terminator})) == SaveCompatibility::kRequiresTitleUpdate6);
  CHECK(Campaign(Concat({Chunk(4, 4, kPayload), terminator})) == SaveCompatibility::kBaseCompatible);
}

void TestMalformedCampaign() {
  std::printf("a malformed campaign save is never reported as Title Update 6 data\n");
  Bytes truncated = Concat({Chunk(6, 4, kPayload), Chunk(6, 1)});
  truncated.resize(truncated.size() - 3);
  CHECK(Campaign(truncated) == SaveCompatibility::kInvalidOrUnknown);

  Bytes payload_cut = Chunk(6, 4, kPayload);
  payload_cut.resize(payload_cut.size() - 2);
  CHECK(Campaign(payload_cut) == SaveCompatibility::kInvalidOrUnknown);

  Bytes overflow = Concat({Chunk(6, 4, kPayload), Chunk(6, 1)});
  overflow[16] = 0xFF;
  overflow[17] = 0xFF;
  overflow[18] = 0xFF;
  overflow[19] = 0xFF;
  CHECK(Campaign(overflow) == SaveCompatibility::kInvalidOrUnknown);

  CHECK(Campaign({}) == SaveCompatibility::kInvalidOrUnknown);
  CHECK(Campaign(Concat({Chunk(6, 4, kPayload), Chunk(6, 1, {}, 0x4E4F5045)})) == SaveCompatibility::kInvalidOrUnknown);
  CHECK(Campaign(Chunk(6, 4, kPayload)) == SaveCompatibility::kInvalidOrUnknown);
  CHECK(Campaign(Concat({Chunk(6, 1, kPayload)})) == SaveCompatibility::kInvalidOrUnknown);
  CHECK(Campaign(Concat({Chunk(4, 4, kPayload), Chunk(4, 1), Bytes{0}})) == SaveCompatibility::kInvalidOrUnknown);
}

void TestCombined() {
  std::printf("a file that needs Title Update 6 decides, then a malformed one, then a compatible one\n");
  using C = SaveCompatibility;
  CHECK(CombineSaveCompatibility(C::kNoSave, C::kNoSave) == C::kNoSave);
  CHECK(CombineSaveCompatibility(C::kBaseCompatible, C::kNoSave) == C::kBaseCompatible);
  CHECK(CombineSaveCompatibility(C::kNoSave, C::kRequiresTitleUpdate6) == C::kRequiresTitleUpdate6);
  CHECK(CombineSaveCompatibility(C::kRequiresTitleUpdate6, C::kInvalidOrUnknown) == C::kRequiresTitleUpdate6);
  CHECK(CombineSaveCompatibility(C::kBaseCompatible, C::kInvalidOrUnknown) == C::kInvalidOrUnknown);
  CHECK(CombineSaveCompatibility(C::kInvalidOrUnknown, C::kInvalidOrUnknown) == C::kInvalidOrUnknown);
}

void TestPathsAndCheck() {
  std::printf("the files are read from where the runtime writes them, and nothing is written\n");
  Files files;
  const fs::path root = files.dir.path() / "user_data";
  CHECK(ProfileBlobPath(root) == root / "4D5307DC" / "profile" / "User" / "63E83FFF");
  CHECK(CampaignSavePath(root) ==
        root / "B13EBABEBABEBABE" / "4D5307DC" / "00000001" / "Crackdown.sav" / "Crackdown.sav");
  CHECK(CheckSaves(root).combined == SaveCompatibility::kNoSave);

  const Bytes profile = Concat({Chunk(5, 21, kPayload), Chunk(5, 1)});
  const Bytes save = Concat({Chunk(6, 4, kPayload), Chunk(6, 1)});
  check::WriteText(ProfileBlobPath(root), std::string_view(reinterpret_cast<const char*>(profile.data()), profile.size()));
  check::WriteText(CampaignSavePath(root), std::string_view(reinterpret_cast<const char*>(save.data()), save.size()));
  const std::string profile_before = check::ReadText(ProfileBlobPath(root));
  const std::string save_before = check::ReadText(CampaignSavePath(root));
  const SaveCheck check = CheckSaves(root);
  CHECK(check.profile == SaveCompatibility::kRequiresTitleUpdate6);
  CHECK(check.campaign == SaveCompatibility::kRequiresTitleUpdate6);
  CHECK(check.combined == SaveCompatibility::kRequiresTitleUpdate6);
  CHECK(check::ReadText(ProfileBlobPath(root)) == profile_before);
  CHECK(check::ReadText(CampaignSavePath(root)) == save_before);
  CHECK(std::string(SaveCompatibilityName(check.combined)) == "requires Title Update 6");
}

}  // namespace

int main() {
  TestProfile();
  TestCampaign();
  TestMalformedCampaign();
  TestCombined();
  TestPathsAndCheck();
  return check::Finish("save_compat_tests");
}
