#include "archive.h"

#include <algorithm>
#include <fstream>
#include <vector>

#include <miniz.h>

#include "paths.h"

namespace crackdown::bundler {
namespace {

namespace fs = std::filesystem;

class ZipReader {
 public:
  ZipReader() { mz_zip_zero_struct(&zip_); }
  ~ZipReader() {
    if (open_) {
      mz_zip_reader_end(&zip_);
    }
  }
  ZipReader(const ZipReader&) = delete;
  ZipReader& operator=(const ZipReader&) = delete;
  bool Open(const fs::path& file) { return open_ = mz_zip_reader_init_file_v2(&zip_, Utf8(file).c_str(), 0, 0, 0); }
  mz_zip_archive* get() { return &zip_; }

 private:
  mz_zip_archive zip_;
  bool open_ = false;
};

struct ZipFile {
  mz_uint index;
  std::string name;  // forward slashes, validated
  bool directory;
};

size_t WriteToStream(void* opaque, mz_uint64, const void* data, size_t size) {
  auto* out = static_cast<std::ofstream*>(opaque);
  out->write(static_cast<const char*>(data), std::streamsize(size));
  return *out ? size : 0;
}

}  // namespace

bool ExtractZip(const fs::path& zip, const fs::path& dest, const std::set<std::string>& skip, std::string& error) {
  ZipReader reader;
  if (!reader.Open(zip)) {
    error = "not a readable ZIP archive: " + Utf8(zip);
    return false;
  }
  std::vector<ZipFile> files;
  const mz_uint count = mz_zip_reader_get_num_files(reader.get());
  for (mz_uint i = 0; i < count; ++i) {
    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(reader.get(), i, &stat)) {
      error = "the ZIP archive is damaged";
      return false;
    }
    std::string name = stat.m_filename;
    std::replace(name.begin(), name.end(), '\\', '/');
    while (!name.empty() && name.back() == '/') {
      name.pop_back();
    }
    if (name.empty()) {
      continue;
    }
    if (!IsSafeRelativePath(name)) {
      error = "the ZIP archive holds an unsafe name: " + name;
      return false;
    }
    if (stat.m_is_encrypted || !stat.m_is_supported) {
      error = "the ZIP archive uses a feature this tool does not read: " + name;
      return false;
    }
    files.push_back({i, name, bool(stat.m_is_directory)});
  }

  // A single top folder that holds everything is dropped.
  std::string top;
  for (const ZipFile& file : files) {
    const std::string first = file.name.substr(0, file.name.find('/'));
    const bool nested = file.name.find('/') != std::string::npos || file.directory;
    if (!nested || (!top.empty() && first != top)) {
      top.clear();
      break;
    }
    top = first;
  }
  for (ZipFile& file : files) {
    if (!top.empty()) {
      file.name = file.name.size() > top.size() ? file.name.substr(top.size() + 1) : std::string();
    }
  }

  for (const ZipFile& file : files) {
    if (file.name.empty() || skip.count(LowerAscii(file.name.substr(0, file.name.find('/'))))) {
      continue;
    }
    const fs::path target = dest / FromPortable(file.name);
    std::error_code ec;
    if (file.directory) {
      fs::create_directories(target, ec);
      continue;
    }
    fs::create_directories(target.parent_path(), ec);
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out || !mz_zip_reader_extract_to_callback(reader.get(), file.index, WriteToStream, &out, 0)) {
      error = "cannot extract " + file.name + " from the ZIP archive";
      return false;
    }
    out.close();
    if (!out) {
      error = "cannot write " + Utf8(target);
      return false;
    }
  }
  return true;
}

bool WriteZip(const fs::path& folder, const fs::path& zip, const std::string& top, const std::string& store_prefix,
              const std::function<void(const std::string&)>& log, std::string& error) {
  std::vector<std::pair<std::string, fs::path>> files;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
    if (it->is_regular_file(ec)) {
      files.emplace_back(GenericUtf8(it->path().lexically_relative(folder)), it->path());
    }
  }
  if (ec) {
    error = "cannot list " + Utf8(folder);
    return false;
  }
  std::sort(files.begin(), files.end());

  mz_zip_archive archive;
  mz_zip_zero_struct(&archive);
  if (!mz_zip_writer_init_file_v2(&archive, Utf8(zip).c_str(), 0, MZ_ZIP_FLAG_WRITE_ZIP64)) {
    error = "cannot create " + Utf8(zip);
    return false;
  }
  bool ok = true;
  size_t done = 0;
  for (const auto& [name, path] : files) {
    const bool store = !store_prefix.empty() && name.starts_with(store_prefix);
    const std::string entry = top + "/" + name;
    if (!mz_zip_writer_add_file(&archive, entry.c_str(), Utf8(path).c_str(), nullptr, 0,
                                store ? MZ_NO_COMPRESSION : MZ_DEFAULT_LEVEL)) {
      error = "cannot add " + name + " to the ZIP archive: " +
              mz_zip_get_error_string(mz_zip_get_last_error(&archive));
      ok = false;
      break;
    }
    ++done;
    if (log && (done % 20 == 0 || done == files.size())) {
      log("Writing the ZIP archive: " + std::to_string(done) + " of " + std::to_string(files.size()) + " files");
    }
  }
  if (ok && !mz_zip_writer_finalize_archive(&archive)) {
    error = "cannot finish the ZIP archive";
    ok = false;
  }
  mz_zip_writer_end(&archive);
  return ok;
}

}  // namespace crackdown::bundler
