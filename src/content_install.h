#pragma once

#include <string>

namespace crackdown {

// Shared by the Title Update and DLC installers.
enum class ContentStatus {
  kOk,
  kCancelled,
  kOpenFailed,
  kNotPackage,
  kWrongTitle,
  kWrongMedia,
  kUnsupportedPackage,
  kDamagedPackage,
  kNoSpace,
  kWriteFailed,
  kNoUserFolder,
  kUnsafeTarget,
  kNotDlcPackage,
  kUnsupportedDlc,
  kNotTitleUpdate,
  kUnsupportedUpdate,
  kTitleUpdateRequired,  // a valid package that needs Title Update 6 first
  kTitleUpdateInUse,     // Title Update 6 cannot be removed while Free-For-All is installed
  kNotInstalled,
  kUnverified,
  kWrongPackage,  // a supported package, but not the one the chosen action installs
};

enum class ContentKind { kNone, kFourPlay, kFreeForAll, kTitleUpdate };

struct ContentResult {
  ContentStatus status = ContentStatus::kOk;
  std::string detail;  // technical text for the log only
  ContentKind expected = ContentKind::kNone;  // kWrongPackage only
  ContentKind actual = ContentKind::kNone;    // kWrongPackage only
  bool ok() const { return status == ContentStatus::kOk; }
};

struct ContentMessage {
  std::string title;
  std::string text;  // plain, player-facing
};

ContentMessage DescribeContent(ContentStatus status);
ContentMessage DescribeContent(const ContentResult& result);

}  // namespace crackdown
