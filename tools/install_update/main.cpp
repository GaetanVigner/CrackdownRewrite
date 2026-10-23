// Installs a Title Update package into a user data folder with the port's own installer, for tools\codegen.ps1:
//   crackdown_install_update <package> <user data folder>
// The update is left in <user data folder>/title_update/4D5307DC. Exit status: 0 installed, 1 refused, 2 usage.
#include <cstdio>
#include <filesystem>

#include "title_update.h"

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: crackdown_install_update <package> <user data folder>\n");
    return 2;
  }
  const crackdown::ContentResult result =
      crackdown::InstallTitleUpdate(std::filesystem::path(argv[1]), std::filesystem::path(argv[2]));
  if (!result.ok()) {
    const crackdown::ContentMessage message = crackdown::DescribeContent(result.status);
    std::fprintf(stderr, "%s: %s (%s)\n", message.title.c_str(), message.text.c_str(), result.detail.c_str());
    return 1;
  }
  std::printf("Title Update 6 package accepted.\n");
  return 0;
}
