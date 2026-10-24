#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

#include "content_catalog.h"
#include "content_fs.h"
#include "content_install.h"

namespace crackdown {

// What a supported update is. The real one is Title Update 6; tests inject small ones.
struct TitleUpdateDescriptor {
  uint64_t xexp_size = 0;
  std::array<uint8_t, 32> xexp_sha256{};
  uint32_t version = 0;       // the patch's target version
  uint32_t base_version = 0;  // the executable version the patch applies to
};

const TitleUpdateDescriptor& TitleUpdate6();

// <user data>/title_update: the update's work folders and, in <title ID>, the installed update that the runtime
// mounts as update:. It holds Default.xexp and Local/<language>/GameStrings_DLC.str, never the game folder.
std::filesystem::path TitleUpdateRoot(const std::filesystem::path& user_data_root);
std::filesystem::path TitleUpdateFolder(const std::filesystem::path& user_data_root);

enum class TitleUpdateState {
  kNotInstalled,
  kInstalled,  // every file the install recorded is there and Default.xexp is the supported one
  kDamaged,    // a folder is there but it is not a complete supported update: the update is not used
};

struct TitleUpdateCheck {
  TitleUpdateState state = TitleUpdateState::kNotInstalled;
  std::string detail;  // for the log
};

// Cheap enough to run at every start: it hashes Default.xexp (about 1.8 MB) and stats the other files.
TitleUpdateCheck CheckTitleUpdate(const std::filesystem::path& user_data_root,
                                  const TitleUpdateDescriptor& update = TitleUpdate6());

// Installs a title update package. The package is validated and extracted to a staging folder first; only then is it
// swapped in, keeping the previous update until the swap succeeds. On any failure or cancellation the previous
// update is as it was. The game folder is never touched.
ContentResult InstallTitleUpdate(const std::filesystem::path& package, const std::filesystem::path& user_data_root,
                                 const InstallHooks& hooks = {}, const TitleUpdateDescriptor& update = TitleUpdate6(),
                                 const FileOps& ops = {});

// Refused with kTitleUpdateInUse while a Free-For-All Pack is installed. `game_dir` must not be, or be inside, the
// removed folder.
ContentResult RemoveTitleUpdate(const std::filesystem::path& user_data_root, const std::filesystem::path& game_dir,
                                const FileOps& ops = {});

}  // namespace crackdown
