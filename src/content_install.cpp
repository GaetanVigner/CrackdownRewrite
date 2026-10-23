#include "content_install.h"

namespace crackdown {

ContentMessage DescribeContent(ContentStatus status) {
  switch (status) {
    case ContentStatus::kOk:
      return {"Done", ""};
    case ContentStatus::kCancelled:
      return {"Install cancelled", "Nothing was changed."};
    case ContentStatus::kOpenFailed:
      return {"Could not open the file", "Check that the file exists and that it is not in use."};
    case ContentStatus::kNotPackage:
      return {"Not an Xbox 360 package", "The selected file is not an Xbox 360 content package."};
    case ContentStatus::kWrongTitle:
      return {"Not a Crackdown package", "This package is for a different game."};
    case ContentStatus::kWrongMedia:
      return {"Wrong Media ID", "This update is for a different release of Crackdown."};
    case ContentStatus::kUnsupportedPackage:
      return {"Unsupported package", "This package uses a layout the installer cannot read."};
    case ContentStatus::kDamagedPackage:
      return {"Package is damaged", "The package is damaged or incomplete. Check the file and try again."};
    case ContentStatus::kNoSpace:
      return {"Not enough disk space", "Free up space on the drive that holds the port's data folder and try again."};
    case ContentStatus::kWriteFailed:
      return {"Install failed", "The files could not be written. Check that the data folder is writable and not in use."};
    case ContentStatus::kNoUserFolder:
      return {"No data folder", "Start the game with --user_data_root to choose where DLC and updates are stored."};
    case ContentStatus::kUnsafeTarget:
      return {"Install failed", "The install folder is not a plain folder, so nothing was changed."};
    case ContentStatus::kNotDlcPackage:
      return {"Not a DLC package", "The selected file is not Xbox 360 Marketplace content."};
    case ContentStatus::kUnsupportedDlc:
      return {"Unsupported DLC", "This package is not one of the Crackdown packs this port supports."};
    case ContentStatus::kNotTitleUpdate:
      return {"Not a title update", "The selected file is not a Crackdown title update package."};
    case ContentStatus::kUnsupportedUpdate:
      return {"Unsupported title update", "This port supports Title Update 6 only."};
    case ContentStatus::kTitleUpdateRequired:
      return {"Title Update 6 is required", "Install Title Update 6 first, then install this pack."};
    case ContentStatus::kTitleUpdateInUse:
      return {"Title Update 6 is in use", "Remove Free-For-All Pack before removing Title Update 6."};
    case ContentStatus::kNotInstalled:
      return {"Not installed", "This is not installed."};
    case ContentStatus::kUnverified:
      return {"Cannot remove this", "The installed package could not be verified, so it was left in place."};
    case ContentStatus::kWrongPackage:
      return {"Wrong package", "The selected package is not the one this button installs."};
  }
  return {"Install failed", ""};
}

namespace {

const char* KindName(ContentKind kind) {
  switch (kind) {
    case ContentKind::kFourPlay:
      return "Four-Play Pack";
    case ContentKind::kFreeForAll:
      return "Free-For-All Pack";
    case ContentKind::kTitleUpdate:
      return "Title Update 6";
    case ContentKind::kNone:
      break;
  }
  return "";
}

}  // namespace

ContentMessage DescribeContent(const ContentResult& result) {
  if (result.status != ContentStatus::kWrongPackage || result.expected == ContentKind::kNone ||
      result.actual == ContentKind::kNone) {
    return DescribeContent(result.status);
  }
  if (result.expected == ContentKind::kTitleUpdate) {
    return {"Wrong package", "The selected package is not Title Update 6."};
  }
  return {"Wrong package", std::string("The selected package is ") + KindName(result.actual) + ", not " +
                               KindName(result.expected) + "."};
}

}  // namespace crackdown
