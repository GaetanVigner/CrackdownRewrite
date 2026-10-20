// crackdown-bundler: packages a Crackdown Recompiled release with the player's own game files, for personal use.
//
//   crackdown-bundler --iso <disc image or folder> --release <zip|AppImage|folder> --target windows|linux --out <path>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include "bundle.h"
#include "paths.h"

namespace {

using namespace crackdown::bundler;

constexpr char kUsage[] =
    "Usage: crackdown-bundler --iso <disc image or folder> --release <release> --target windows|linux --out <path>\n"
    "\n"
    "  --iso      your own Crackdown disc image (.iso), or a folder with the files extracted from it\n"
    "  --release  a published Crackdown Recompiled release:\n"
    "               windows: Crackdown-v<version>-windows-x64.zip, or the folder extracted from it\n"
    "               linux:   CrackdownRecompiled-v<version>-x86_64.AppImage, or a folder that holds it\n"
    "  --target   windows, linux (also for the Steam Deck) or android\n"
    "  --out      what to write; it must not exist yet. The name chooses the format:\n"
    "               windows: <name>.zip for a ZIP archive, any other name for a folder\n"
    "               linux:   <name>.AppImage for one AppImage with the game inside, any other name for a folder\n"
    "                        with the AppImage and its game folder\n"
    "\n"
    "Everything happens on this computer. Nothing is downloaded, uploaded or shared.\n";

void Print(const std::string& text) {
  std::fputs(text.c_str(), stdout);
  std::fflush(stdout);
}

std::vector<std::string> Arguments(int argc, char** argv) {
  std::vector<std::string> args;
#if defined(_WIN32)
  // The narrow argv is in the system code page; paths are carried as UTF-8.
  (void)argv;
  int count = 0;
  wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
  for (int i = 1; wide && i < count; ++i) {
    args.push_back(Utf8(std::filesystem::path(wide[i])));
  }
  LocalFree(wide);
#else
  for (int i = 1; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }
#endif
  (void)argc;
  return args;
}

}  // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
  SetConsoleOutputCP(CP_UTF8);
#endif
  const std::vector<std::string> args = Arguments(argc, argv);
  Options options;
  bool have_target = false;
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string_view arg = args[i];
    if (arg == "--help" || arg == "-h") {
      Print(kUsage);
      return 0;
    }
    if (arg == "--version") {
      Print("crackdown-bundler " CRACKDOWN_BUNDLER_VERSION "\n");
      return 0;
    }
    if (i + 1 >= args.size()) {
      Print("Missing value for " + args[i] + "\n\n" + kUsage);
      return 2;
    }
    const std::string& value = args[++i];
    if (arg == "--iso") {
      options.iso = PathFromUtf8(value);
    } else if (arg == "--release") {
      options.release = PathFromUtf8(value);
    } else if (arg == "--out") {
      options.out = PathFromUtf8(value);
    } else if (arg == "--target") {
      if (!ParseTarget(value, options.target)) {
        Print("Unknown target: " + value + "\n\n" + kUsage);
        return 2;
      }
      have_target = true;
    } else {
      Print("Unknown option: " + args[i - 1] + "\n\n" + kUsage);
      return 2;
    }
  }
  if (have_target && options.target == Target::kAndroid) {
    Print(kNoAndroidBuild);
    return 2;
  }
  if (!have_target || options.iso.empty() || options.release.empty() || options.out.empty()) {
    Print(kUsage);
    return 2;
  }

  Print(std::string("Crackdown Recompiled bundler\n\n") + kPersonalUseNotice + "\n");
  options.log = [](const std::string& line) { Print(line + "\n"); };
  const Result result = Bundle(options);
  if (!result.ok) {
    Print("\nERROR: " + result.error + "\n");
    return 1;
  }
  Print("\nDone: " + Utf8(options.out) + "\n\n" + kPersonalUseNotice);
  return 0;
}
