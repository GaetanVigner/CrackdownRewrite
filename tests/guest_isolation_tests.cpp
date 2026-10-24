// Both recompiled guests are linked into crackdown, so a process must select one before the runtime exists and must
// never reach the other's functions, tables or bindings. Checked from the sources and, where it exists, the generated
// declarations.

#include <filesystem>
#include <regex>
#include <set>
#include <string>

#include "check.h"

namespace fs = std::filesystem;

namespace {

fs::path Root() {
  return fs::path(CRACKDOWN_SRC_DIR) / "..";
}

std::string Read(const char* relative) {
  return check::ReadText(Root() / relative);
}

bool Has(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

std::set<std::string> BoundAddresses(const std::string& bindings) {
  std::set<std::string> addresses;
  const std::regex bind(R"(CRACKDOWN_(?:BIND|BIND_REPLACEMENT|GUEST_CALL)\(([0-9A-F]{8}))");
  for (std::sregex_iterator it(bindings.begin(), bindings.end(), bind), end; it != end; ++it) {
    addresses.insert((*it)[1]);
  }
  return addresses;
}

std::set<std::string> DeclaredFunctions(const std::string& funcs) {
  std::set<std::string> names;
  const std::regex declare(R"(DECLARE_REX_FUNC\(sub_([0-9A-F]{8})\))");
  for (std::sregex_iterator it(funcs.begin(), funcs.end(), declare), end; it != end; ++it) {
    names.insert((*it)[1]);
  }
  return names;
}

void TestEachGuestHasItsOwnDescriptor() {
  std::printf("each linked guest has one descriptor, defined with its own bindings\n");
  const std::string base = Read("src/guest_bindings_base.cpp");
  const std::string tu6 = Read("src/guest_bindings_tu6.cpp");
  CHECK(Has(base, "const crackdown::GuestImage crackdown::guests::kBase = {"));
  CHECK(Has(base, "crackdown::content::GuestBuild::kBase,"));
  CHECK(Has(tu6, "const crackdown::GuestImage crackdown::guests::kTitleUpdate6 = {"));
  CHECK(Has(tu6, "crackdown::content::GuestBuild::kTitleUpdate6,"));
  for (const std::string* bindings : {&base, &tu6}) {
    CHECK(Has(*bindings, "&PPCImageConfig,"));
    CHECK(Has(*bindings, "&kCalls,"));
    CHECK(Has(*bindings, "#include \"guest_binding.h\""));
  }
  CHECK(!Has(base, "kTitleUpdate6") && !Has(tu6, "guests::kBase"));
  const std::string binding = Read("src/guest_binding.h");
  CHECK(Has(binding, "#include \"crackdown_guest_symbols.h\""));
  CHECK(Has(binding, "extern const rex::PPCImageInfo PPCImageConfig;"));
}

void TestOnlyTheSelectedDescriptorReachesTheRuntime() {
  std::printf("only the selected descriptor's image reaches the runtime, and a process selects once\n");
  const std::string app = Read("src/crackdown_app.h");
  CHECK(Has(app, "crackdown::SelectGuest(startup.build)"));
  CHECK(Has(app, "new CrackdownApp(ctx, \"crackdown\", *guest->image, std::move(startup))"));
  CHECK(!Has(app, "PPCImageConfig"));
  CHECK(!Has(app, "kRunningBuild"));
  CHECK(!Has(app, "SwitchGuestBuild"));
  const std::string create = app.substr(app.find("static std::unique_ptr<rex::ui::WindowedApp> Create"));
  CHECK(create.find("SelectStartup") < create.find("SelectGuest(") && create.find("SelectGuest(") < create.find("new CrackdownApp"));

  const std::string image = Read("src/guest_image.cpp");
  CHECK(Has(image, "compare_exchange_strong(expected, guest) && expected != guest"));
  CHECK(Has(image, "std::abort();"));
  CHECK(Has(image, "RunningBuildStorage() = guest->build;"));
  const std::string header = Read("src/guest_image.h");
  CHECK(Has(header, "const GuestImage* FindGuestImage(content::GuestBuild build);"));
  CHECK(!Has(header, "PPCFuncMappings"));
  CHECK(!Has(Read("src/main.cpp"), "crackdown_init.h"));

  // No host source outside the bindings names a guest's image or function table.
  for (const auto& entry : fs::directory_iterator(Root() / "src")) {
    const std::string name = entry.path().filename().string();
    if (name.rfind("guest_binding", 0) == 0 || (entry.path().extension() != ".cpp" && entry.path().extension() != ".h")) {
      continue;
    }
    const std::string text = check::ReadText(entry.path());
    CHECK(!Has(text, "PPCImageConfig") || name == "guest_image.h" || name == "guest_symbol_macros.h");
    CHECK(!Has(text, "PPCFuncMappings"));
  }
}

void TestSymbolsAreRenamedPerGuest() {
  std::printf("the guests' symbols are renamed by macro per guest, without relying on link order\n");
  const std::string cmake = Read("cmake/crackdown_guests.cmake");
  CHECK(Has(cmake, "crackdown_add_guest(base guest_base_"));
  CHECK(Has(cmake, "crackdown_add_guest(tu6 guest_tu6_"));
  CHECK(Has(cmake, "list(FILTER _decls EXCLUDE REGEX \"^__imp__\")"));
  CHECK(Has(cmake, "list(APPEND _decls PPCImageConfig PPCFuncMappings crackdown_RegisterFunctions)"));
  CHECK(Has(cmake, "\"${CMAKE_CURRENT_SOURCE_DIR}/src/crackdown_guest_pch.h\""));
  CHECK(!Has(cmake, "allow-multiple-definition") && !Has(cmake, "FORCE_MULTIPLE") && !Has(cmake, "/FORCE"));

  const std::string pch = Read("src/crackdown_guest_pch.h");
  CHECK(pch.find("crackdown_guest_symbols.h") < pch.find("crackdown_pch.h"));
  const std::string macros = Read("src/guest_symbol_macros.h");
  CHECK(Has(macros, "#define DECLARE_REX_FUNC(name)"));
  CHECK(Has(macros, "#define DEFINE_REX_FUNC(name)"));
  CHECK(Has(macros, "REX_EXTERN(CRACKDOWN_GUEST_JOIN(__imp__, name))"));
  CHECK(Has(macros, "alias(\"__imp__\" CRACKDOWN_GUEST_STR(name))"));
  CHECK(!Has(macros, "weak_import"));
}

void TestBindingsNameOnlyTheirOwnGuest() {
  std::printf("every binding names a function its own guest declares, never the other guest's\n");
  const std::set<std::string> base = BoundAddresses(Read("src/guest_bindings_base.cpp"));
  const std::set<std::string> tu6 = BoundAddresses(Read("src/guest_bindings_tu6.cpp"));
  CHECK(!base.empty() && !tu6.empty());
  for (const std::string& address : tu6) {
    CHECK(base.count(address) == 0);
  }
  const fs::path default_funcs = Root() / "generated/default/crackdown_funcs.h";
  const fs::path tu6_funcs = Root() / "generated/tu6/crackdown_funcs.h";
  if (!fs::exists(default_funcs) || !fs::exists(tu6_funcs)) {
    std::printf("  (generated declarations not present: skipped)\n");
    return;
  }
  const std::set<std::string> base_declared = DeclaredFunctions(check::ReadText(default_funcs));
  const std::set<std::string> tu6_declared = DeclaredFunctions(check::ReadText(tu6_funcs));
  CHECK(base_declared.size() > 1000 && tu6_declared.size() > 1000);
  for (const std::string& address : base) {
    CHECK(base_declared.count(address) == 1);
  }
  for (const std::string& address : tu6) {
    CHECK(tu6_declared.count(address) == 1);
  }
}

}  // namespace

int main() {
  TestEachGuestHasItsOwnDescriptor();
  TestOnlyTheSelectedDescriptorReachesTheRuntime();
  TestSymbolsAreRenamedPerGuest();
  TestBindingsNameOnlyTheirOwnGuest();
  return check::Finish("guest_isolation_tests");
}
