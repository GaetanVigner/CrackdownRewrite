#pragma once

// Turns a published Crackdown Recompiled release and the player's own disc image into one package that starts the game
// without the launcher's install step. Everything happens on this machine; nothing is uploaded or shared.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "game_files.h"

namespace crackdown::bundler {

enum class Target { kWindows, kLinux, kAndroid };

enum class Format {
  kFolder,    // windows: crackdown.exe and game\ in one folder; linux: the AppImage and game/ in one folder
  kZip,       // windows: that folder as a ZIP archive
  kAppImage,  // linux: one AppImage with game/ inside its file system
};

struct Options {
  std::filesystem::path iso;      // the player's disc image, or a folder with its extracted files
  std::filesystem::path release;  // windows: release .zip or folder; linux: release .AppImage or a folder with one
  Target target = Target::kWindows;
  std::filesystem::path out;  // the format follows the name: *.zip, *.AppImage, anything else is a folder
  const GameDescriptor* game = nullptr;  // tests use a small synthetic game; the real one when null
  std::function<void(const std::string&)> log;
};

struct Result {
  bool ok = false;
  std::string error;  // player-facing, with the technical detail after it
};

bool ParseTarget(std::string_view text, Target& target);
// False when the output name asks for a format the target cannot have (a .AppImage for Windows, an .exe, ...).
bool FormatFor(Target target, const std::filesystem::path& out, Format& format, std::string& error);

Result Bundle(const Options& options);

// Shown before and after a run and written into every package.
extern const char kPersonalUseNotice[];
extern const char kNoAndroidBuild[];

// The AppImage's own AppRun, kept as AppRun.release, is started by this one with the bundled game folder.
extern const char kAppRunWrapper[];

// Where the SquashFS image of an AppImage starts: right after the ELF runtime's section headers.
bool FindSquashfsOffset(const std::filesystem::path& appimage, uint64_t& offset, std::string& error);

}  // namespace crackdown::bundler
