#include "content_fs.h"

#include <algorithm>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace crackdown::content_fs {

std::string PathToUtf8(const fs::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

fs::path::string_type LowerNative(fs::path::string_type text) {
  for (auto& c : text) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<fs::path::value_type>(c - 'A' + 'a');
    }
  }
  return text;
}

bool IsLink(const fs::path& path) {
#if defined(_WIN32)
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
    return false;
  }
  WIN32_FIND_DATAW data;
  const HANDLE found = FindFirstFileW(path.c_str(), &data);
  if (found == INVALID_HANDLE_VALUE) {
    return true;
  }
  FindClose(found);
  return (data.dwReserved0 & 0x20000000u) != 0;
#else
  std::error_code ec;
  return fs::is_symlink(path, ec);
#endif
}

bool IsAbsent(const fs::path& path) {
  std::error_code ec;
  return fs::symlink_status(path, ec).type() == fs::file_type::not_found;
}

bool IsPlainFile(const fs::path& path) {
  std::error_code ec;
  return !IsLink(path) && fs::is_regular_file(fs::symlink_status(path, ec));
}

bool ContainsLink(const fs::path& dir) {
  std::error_code ec;
  for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    if (IsLink(it->path())) {
      return true;
    }
  }
  return static_cast<bool>(ec);
}

bool IsPlainFolder(const fs::path& path) {
  std::error_code ec;
  return !IsLink(path) && fs::is_directory(fs::symlink_status(path, ec)) && !ContainsLink(path);
}

bool IsAbsentOrPlainFolder(const fs::path& path) {
  return IsAbsent(path) || IsPlainFolder(path);
}

bool IsAbsentOrPlainFile(const fs::path& path) {
  return IsAbsent(path) || IsPlainFile(path);
}

namespace {

bool SameComponent(const fs::path& a, const fs::path& b) {
#if defined(_WIN32)
  return LowerNative(a.native()) == LowerNative(b.native());
#else
  return a == b;
#endif
}

}  // namespace

fs::path Resolved(const fs::path& path) {
  std::error_code ec;
  fs::path absolute = fs::absolute(path, ec);
  fs::path out = fs::weakly_canonical(absolute, ec);
  if (ec) {
    out = absolute;
  }
  out = out.lexically_normal();
  if (!out.has_filename() && out.has_relative_path()) {
    out = out.parent_path();
  }
  return out;
}

bool IsSameOrInside(const fs::path& outer, const fs::path& inner) {
  auto o = outer.begin();
  auto i = inner.begin();
  for (; o != outer.end(); ++o, ++i) {
    if (i == inner.end() || !SameComponent(*o, *i)) {
      return false;
    }
  }
  return true;
}

bool IsInside(const fs::path& root, const fs::path& path) {
  const fs::path relative = path.lexically_relative(root);
  return !relative.empty() && relative != "." && *relative.begin() != "..";
}

bool IsPlainChildDirectory(const fs::path& parent, const fs::path& child) {
  std::error_code ec;
  const fs::path real_parent = fs::canonical(parent, ec);
  if (ec) {
    return false;
  }
  const fs::path real_child = fs::canonical(child, ec);
  if (ec) {
    return false;
  }
  return real_child == real_parent / child.filename() && !IsLink(child) && fs::is_directory(child, ec);
}

bool GameFolderWouldBeRemoved(const fs::path& folder, const fs::path& game_dir) {
  std::error_code ec;
  const fs::path real_folder = fs::canonical(folder, ec);
  if (ec) {
    return true;
  }
  const fs::path real_game = fs::canonical(game_dir, ec);
  return !ec && IsSameOrInside(real_folder, real_game);
}

bool RenameWithRetry(const fs::path& from, const fs::path& to, const FileOps& ops, std::error_code& last) {
  const int attempts = std::max(1, ops.rename_attempts);
  for (int attempt = 0; attempt < attempts; ++attempt) {
    last.clear();
    if (ops.rename) {
      ops.rename(from, to, last);
    } else {
      fs::rename(from, to, last);
    }
    if (!last) {
      return true;
    }
    if (attempt + 1 < attempts && ops.retry_delay.count() > 0) {
      std::this_thread::sleep_for(ops.retry_delay);
    }
  }
  return false;
}

Removal RemoveWorkFolder(const fs::path& dir, const FileOps& ops) {
  if (IsAbsent(dir)) {
    return Removal::kGone;
  }
  if (!IsPlainFolder(dir)) {
    return Removal::kUnsafe;
  }
  std::error_code ec;
  if (ops.remove_all) {
    ops.remove_all(dir, ec);
  } else {
    fs::remove_all(dir, ec);
  }
  return !ec && IsAbsent(dir) ? Removal::kGone : Removal::kFailed;
}

SwapResult SwapIn(const fs::path& staged, const fs::path& live, const fs::path& backup, const FileOps& ops,
                  bool& had_previous) {
  std::error_code ec;
  had_previous = !IsAbsent(live);
  if (had_previous && !RenameWithRetry(live, backup, ops, ec)) {
    return SwapResult::kUnchanged;
  }
  fs::create_directories(live.parent_path(), ec);
  if (!RenameWithRetry(staged, live, ops, ec)) {
    return had_previous && !RenameWithRetry(backup, live, ops, ec) ? SwapResult::kStranded : SwapResult::kUnchanged;
  }
  return SwapResult::kActivated;
}

bool RevertSwap(const fs::path& live, const fs::path& backup, bool had_previous, const FileOps& ops) {
  if (RemoveWorkFolder(live, ops) != Removal::kGone) {
    return false;
  }
  std::error_code ec;
  return !had_previous || RenameWithRetry(backup, live, ops, ec);
}

}  // namespace crackdown::content_fs
