#include <optional>

#include "check.h"
#include "window_title.h"

using namespace crackdown;

namespace {

constexpr std::string_view kGame = "Crackdown Recompiled";

void TestSettingCombinations() {
  const WindowSize picture{3360, 1440};
  CHECK(ComposeWindowTitle(kGame, std::nullopt) == "Crackdown Recompiled");
  CHECK(ComposeWindowTitle(kGame, picture) == "Crackdown Recompiled | 3360x1440");
}

}  // namespace

int main() {
  TestSettingCombinations();
  return check::Finish("window_title_tests");
}
