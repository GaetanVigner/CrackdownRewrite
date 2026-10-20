#pragma once

// ZIP archives, through miniz: the Windows release comes as one, and a Windows package can be written as one.

#include <filesystem>
#include <functional>
#include <set>
#include <string>

namespace crackdown::bundler {

// Extracts `zip` into the existing folder `dest`. A single folder that holds everything is dropped (an archive of
// "Crackdown-v1/..." extracts like one of "..."). Top-level entries named in `skip` (lower case) are left out.
// Names that could leave `dest` are refused.
bool ExtractZip(const std::filesystem::path& zip, const std::filesystem::path& dest, const std::set<std::string>& skip,
                std::string& error);

// Writes every file under `folder` to `zip` (ZIP64), inside a top folder named `top`. Files under `store_prefix` (a
// forward-slash path relative to `folder`) are stored without compression: game data does not compress.
bool WriteZip(const std::filesystem::path& folder, const std::filesystem::path& zip, const std::string& top,
              const std::string& store_prefix, const std::function<void(const std::string&)>& log, std::string& error);

}  // namespace crackdown::bundler
