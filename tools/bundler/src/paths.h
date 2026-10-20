#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace crackdown::bundler {

// Messages and archive names are UTF-8 whatever the system code page.
inline std::string Utf8(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

// Forward slashes on every system.
inline std::string GenericUtf8(const std::filesystem::path& path) {
  const std::u8string text = path.generic_u8string();
  return std::string(text.begin(), text.end());
}

inline std::filesystem::path PathFromUtf8(std::string_view text) {
  return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

inline std::string LowerAscii(std::string text) {
  for (char& c : text) {
    if (c >= 'A' && c <= 'Z') {
      c = char(c - 'A' + 'a');
    }
  }
  return text;
}

// A relative forward-slash path whose every part is a plain name: no root, drive, "." or "..", no empty part.
inline bool IsSafeRelativePath(std::string_view path) {
  if (path.empty()) {
    return false;
  }
  while (true) {
    const size_t slash = path.find('/');
    const std::string_view part = path.substr(0, slash);
    if (part.empty() || part == "." || part == ".." || part.find_first_of(":\\") != std::string_view::npos) {
      return false;
    }
    for (const char c : part) {
      if (static_cast<unsigned char>(c) < 0x20) {
        return false;
      }
    }
    if (slash == std::string_view::npos) {
      return true;
    }
    path = path.substr(slash + 1);
  }
}

inline std::filesystem::path FromPortable(std::string_view path) {
  std::filesystem::path out;
  while (!path.empty()) {
    const size_t slash = path.find('/');
    out /= PathFromUtf8(path.substr(0, slash));
    path = slash == std::string_view::npos ? std::string_view{} : path.substr(slash + 1);
  }
  return out;
}

}  // namespace crackdown::bundler
