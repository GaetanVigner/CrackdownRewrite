// STFS reader: metadata, hash chain, path safety and extraction, on packages built here.
// Run: stfs_tests [--package FILE]  (with --package, the real package is also read and extracted)

#include <cstring>
#include <string>

#include "check.h"
#include "stfs.h"
#include "stfs_builder.h"

namespace fs = std::filesystem;
using namespace stfs_test;
using crackdown::stfs::Package;
using crackdown::stfs::Status;

namespace {

std::vector<Item> SampleItems() {
  return {
      Dir("Streaming", -1),                         // 0
      File("Agents.txt", -1, Pattern(228, 1)),      // 1
      File("big.bin", 0, Pattern(200 * 4096 + 17, 2)),  // 2: spans two hash table groups
      File("empty.txt", 0, {}),                     // 3
      File("Street Racing.dff", -1, Pattern(5000, 3)),  // 4
  };
}

Status Load(const Bytes& image, const fs::path& dir, Package& pkg, bool load_files = true) {
  const fs::path file = dir / "package.bin";
  WriteBytes(file, image);
  auto result = pkg.Open(file);
  if (!result.ok() || !load_files) {
    return result.status;
  }
  return pkg.LoadFiles().status;
}

void TestMetadata() {
  std::printf("metadata, licence mask and file list\n");
  check::TempDir dir;
  BuildOptions options;
  options.media_id = 0x596F9615;
  options.display_name = u"Four-Play Pack";
  options.license_bits = 0x5;
  options.license_flags = 1;
  Package pkg;
  CHECK(Load(BuildPackage(SampleItems(), options), dir.path(), pkg) == Status::kOk);
  CHECK(pkg.title_id() == 0x4D5307DC);
  CHECK(pkg.content_type() == 2);
  CHECK(pkg.media_id() == 0x596F9615);
  CHECK(pkg.display_name() == u"Four-Play Pack");
  CHECK(pkg.license_mask() == 5);
  CHECK(pkg.content_id_hex().size() == 40);
  CHECK(pkg.file_count() == 4);
  CHECK(pkg.HasFile("Streaming/big.bin"));
  CHECK(pkg.HasFile("Street Racing.dff"));
  CHECK(!pkg.HasFile("street racing.dff"));
  CHECK(!pkg.HasFile("Streaming"));
  CHECK(pkg.total_bytes() == 228 + 200 * 4096 + 17 + 5000);

  BuildOptions unflagged;
  unflagged.license_bits = 7;
  Package other;
  CHECK(Load(BuildPackage(SampleItems(), unflagged), dir.path(), other) == Status::kOk);
  CHECK(other.license_mask() == 0);
}

void TestExtractionAcrossHashGroups() {
  std::printf("extraction verifies every block and reproduces the files\n");
  check::TempDir dir;
  const std::vector<Item> items = SampleItems();
  Package pkg;
  CHECK(Load(BuildPackage(items), dir.path(), pkg) == Status::kOk);
  const fs::path out = dir.path() / "out";
  fs::create_directories(out);
  uint64_t last_done = 0;
  bool monotonic = true;
  const auto result = pkg.ExtractTo(out, [&](uint64_t done, uint64_t total) {
    monotonic &= done >= last_done && total == pkg.total_bytes();
    last_done = done;
    return true;
  });
  CHECK(result.ok());
  CHECK(monotonic && last_done == pkg.total_bytes());
  CHECK(ReadBytes(out / "Agents.txt") == items[1].data);
  CHECK(ReadBytes(out / "Streaming" / "big.bin") == items[2].data);
  CHECK(ReadBytes(out / "Street Racing.dff") == items[4].data);
  CHECK(fs::exists(out / "Streaming" / "empty.txt") && fs::file_size(out / "Streaming" / "empty.txt") == 0);
}

void TestCancel() {
  std::printf("cancelling stops the extraction\n");
  check::TempDir dir;
  Package pkg;
  CHECK(Load(BuildPackage(SampleItems()), dir.path(), pkg) == Status::kOk);
  const fs::path out = dir.path() / "out";
  fs::create_directories(out);
  int calls = 0;
  const auto result = pkg.ExtractTo(out, [&](uint64_t, uint64_t) { return ++calls < 3; });
  CHECK(result.status == Status::kCancelled);
}

void TestNotPackage() {
  std::printf("files that are not packages\n");
  check::TempDir dir;
  Package pkg;
  CHECK(pkg.Open(dir.path() / "missing.bin").status == Status::kOpenFailed);
  CHECK(pkg.Open(dir.path()).status == Status::kOpenFailed);
  WriteBytes(dir.path() / "short.bin", Bytes(100, 1));
  CHECK(pkg.Open(dir.path() / "short.bin").status == Status::kNotPackage);
  WriteBytes(dir.path() / "junk.bin", Pattern(0x20000, 9));
  CHECK(pkg.Open(dir.path() / "junk.bin").status == Status::kNotPackage);
  for (const char* magic : {"LIVE", "PIRS", "CON "}) {
    BuildOptions options;
    options.magic = magic;
    Package accepted;
    CHECK(Load(BuildPackage(SampleItems(), options), dir.path(), accepted) == Status::kOk);
  }
}

void TestCorruption() {
  std::printf("a damaged package is rejected before or during extraction\n");
  check::TempDir dir;
  {
    BuildOptions options;
    options.corrupt_header = true;
    Package pkg;
    CHECK(Load(BuildPackage(SampleItems(), options), dir.path(), pkg, false) == Status::kCorrupt);
  }
  for (const bool top : {false, true}) {
    BuildOptions options;
    options.corrupt_level0_table = !top;
    options.corrupt_top_hash = top;
    Package pkg;
    CHECK(Load(BuildPackage(SampleItems(), options), dir.path(), pkg) == Status::kCorrupt);
  }
  {
    BuildOptions options;
    options.truncate = 3 * 4096;
    Package pkg;
    CHECK(Load(BuildPackage(SampleItems(), options), dir.path(), pkg) == Status::kCorrupt);
  }
  {
    BuildOptions options;
    options.corrupt_data_byte_of_item = 2;
    Package pkg;
    CHECK(Load(BuildPackage(SampleItems(), options), dir.path(), pkg) == Status::kOk);
    const fs::path out = dir.path() / "out";
    fs::create_directories(out);
    CHECK(pkg.ExtractTo(out, nullptr).status == Status::kCorrupt);
  }
}

void TestLayouts() {
  std::printf("layouts the reader does not support\n");
  check::TempDir dir;
  {
    BuildOptions options;
    options.read_only = false;
    Package pkg;
    CHECK(Load(BuildPackage(SampleItems(), options), dir.path(), pkg) == Status::kUnsupported);
  }
  {
    BuildOptions options;
    options.consecutive = false;
    Package pkg;
    CHECK(Load(BuildPackage(SampleItems(), options), dir.path(), pkg) == Status::kUnsupported);
  }
}

void TestUnsafeNames() {
  std::printf("entry names that could leave the extraction folder\n");
  check::TempDir dir;
  const char* names[] = {"..", ".", "a\\b", "a:b", "C:", "con", "CON.txt", "nul", "com1", "LPT9.dat", "trail.", "trail ",
                         "a|b", "a*b", "a?b", "a<b", "q\"q", "with/slash"};
  for (const char* name : names) {
    Package pkg;
    const Status status = Load(BuildPackage({File(name, -1, Pattern(10, 1))}), dir.path(), pkg);
    CHECK(status == Status::kUnsafePath);
  }
  Package nested;
  CHECK(Load(BuildPackage({Dir("Local", -1), Dir("..", 0), File("x.str", 1, Pattern(10, 1))}), dir.path(), nested) ==
        Status::kUnsafePath);
  Package control;
  CHECK(Load(BuildPackage({File(std::string("bell\a"), -1, Pattern(10, 1))}), dir.path(), control) ==
        Status::kUnsafePath);
  Package ok;
  CHECK(Load(BuildPackage({File("Street Racing.dff", -1, Pattern(10, 1)), File("Cheat Mode.sav", -1, Pattern(10, 2))}),
             dir.path(), ok) == Status::kOk);
}

void TestStructure() {
  std::printf("broken file tables\n");
  check::TempDir dir;
  Package orphan;
  CHECK(Load(BuildPackage({File("a.txt", 7, Pattern(10, 1))}), dir.path(), orphan) == Status::kCorrupt);
  Package parent_is_file;
  CHECK(Load(BuildPackage({File("a.txt", -1, Pattern(10, 1)), File("b.txt", 0, Pattern(10, 2))}), dir.path(),
             parent_is_file) == Status::kCorrupt);
  Package duplicate;
  CHECK(Load(BuildPackage({File("a.txt", -1, Pattern(10, 1)), File("A.TXT", -1, Pattern(10, 2))}), dir.path(),
             duplicate) == Status::kCorrupt);
  Package loop;
  CHECK(Load(BuildPackage({Dir("a", 1), Dir("b", 0)}), dir.path(), loop) == Status::kCorrupt);
}

void TestRealPackage(const fs::path& path) {
  std::printf("real package %s\n", path.filename().string().c_str());
  check::TempDir dir;
  Package pkg;
  CHECK(pkg.Open(path).ok());
  CHECK(pkg.LoadFiles().ok());
  std::printf("  title %08X type %08X media %08X id %s files %zu bytes %llu licence %08X name %d chars\n",
              pkg.title_id(), pkg.content_type(), pkg.media_id(), pkg.content_id_hex().c_str(), pkg.file_count(),
              static_cast<unsigned long long>(pkg.total_bytes()), pkg.license_mask(), int(pkg.display_name().size()));
  const fs::path out = dir.path() / "out";
  fs::create_directories(out);
  CHECK(pkg.ExtractTo(out, nullptr).ok());
  uint64_t bytes = 0;
  for (const auto& e : fs::recursive_directory_iterator(out)) {
    if (e.is_regular_file()) {
      bytes += e.file_size();
    }
  }
  CHECK(bytes == pkg.total_bytes());
}

}  // namespace

int main(int argc, char** argv) {
  TestMetadata();
  TestExtractionAcrossHashGroups();
  TestCancel();
  TestNotPackage();
  TestCorruption();
  TestLayouts();
  TestUnsafeNames();
  TestStructure();
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--package") == 0) {
      TestRealPackage(argv[i + 1]);
    }
  }
  return check::Finish("stfs_tests");
}
