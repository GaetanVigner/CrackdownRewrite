# Crackdown Recompiled v0.6.2

Crackdown for Xbox 360 as a native PC recompilation, with enhanced graphics, high frame rates, modern display support, keyboard and mouse support, a full launcher, offline achievements and language selection. It runs natively on Windows, and experimentally on Linux and Steam Deck as an AppImage.

v0.6.2 is a packaging and compatibility update. It reduces false-positive heuristic antivirus detections of the Windows update helper through conventional build and packaging changes. Gameplay, saves, DLC, the launcher and the update process work exactly as in v0.6.1.

## Highlights
- **Fewer antivirus false positives on Windows**: the previous Windows update helper, `trg-updater.exe`, could trigger heuristic antivirus detections. It is now built from the same Crackdown updater source with the conventional MSYS2 CLANG64 build model already used by the Super Street Fighter II Turbo HD Remix and Dragon's Crown Pro PC ports. The updater works exactly as before.
- **Same game build**: `crackdown.exe` keeps its existing Visual Studio clang Release build with ThinLTO.
- **Cleaner Windows package**: the ZIP now holds a single top-level folder, `Crackdown-v0.6.2-windows-x64`, and the update helper's debug symbols are kept outside the download.
- **Linux AppImage unchanged in function**: it is built with the existing official packaging and runtime, and it passed the final release validation.

## What's New

### Windows Packaging
- **Update helper rebuilt with a conventional toolchain**: `trg-updater.exe` is a self-contained helper that uses only Windows system libraries. Its job format, SHA-256 verification, backup, rollback and restart behavior are unchanged.
- **Single-folder ZIP**: extracting the ZIP creates one `Crackdown-v0.6.2-windows-x64` folder instead of loose files. The launcher's updater from v0.5.0 onward accepts this layout.
- **No debug files in the download**: the update helper's debug symbols stay outside the release ZIP.

### Validation
- The updater core tests pass, and the update helper test passes against the final production `trg-updater.exe` (58 checks, 0 failures).
- The release package scanner and the Windows package verification pass.
- The Linux AppImage passes its packaging verification. It still supports the Ubuntu 20.04 / glibc 2.31 baseline; the highest requirement found is `GLIBC_2.30`.

## Antivirus Scans
Both downloads were checked with VirusTotal before publication and had 0 detections at release preparation time:
- [Crackdown-v0.6.2-windows-x64.zip](https://www.virustotal.com/gui/file/fa1a6f9a46a3d9ea213a9898b86f11fcc35c855ddf6e42322e81ded15550b421)
- [CrackdownRecompiled-v0.6.2-x86_64.AppImage](https://www.virustotal.com/gui/file/e5468433e2c8fce57e68202b606f68148c7c84eb76b3c51254139b5e84245400)

VirusTotal results can change as antivirus engines update, and a clean scan is not a security guarantee. Compare your download with the SHA-256 checksums below.

## Updating from v0.6.1
- **From the launcher**: start v0.6.1. When the update banner appears, choose **UPDATE NOW** and then **RESTART TO UPDATE**. Only the port's own program files are replaced. Game files, DLC, saves, settings, logs and caches are kept. This works for the Windows ZIP and the AppImage.
- **Manually on Windows**: extract the ZIP and copy the contents of its `Crackdown-v0.6.2-windows-x64` folder into your existing Crackdown folder, replacing the program files. The `game`, `user_data` and `logs` folders already there are kept.
- **Manually on Linux**: replace the old AppImage with the new one in the same folder. The `game` folder and user data next to it are kept.

## Linux / Steam Deck (Experimental)
Linux and Steam Deck support is **Experimental**; the release itself is not. It is a native x86_64 build using Vulkan, shipped as an AppImage, and does not use Proton or Wine. It has the same launcher and platform-neutral features as Windows, including DLC, Title Update 6, keyboard and mouse, remapping and the launcher updates. The AppImage is built and checked by automated scripts and tests, but it has not yet been fully validated on real Linux PCs or Steam Deck hardware. Reports are welcome. See [docs/linux.md](https://github.com/saulob/crackdown-xbox360-recompiled/blob/main/docs/linux.md).

## Known Issues
- Background music may occasionally stop during gameplay while sound effects and voices continue.
- The Graphics card selection exists on Windows only.

## Installation
You need your own supported Crackdown game files. **No copyrighted game data is included.** The launcher installs the game from your disc image or extracted files and checks that it is this exact version:

| | |
| --- | --- |
| Game | Crackdown, Xbox 360 retail disc (2007), original release without a title update |
| Title ID | `4D5307DC` |
| `Default.xex` | 14,270,464 bytes, SHA-256 `7AAA7A8920D1C3C737E58CF583DEBAFE6013BE0A96BEC3EC136E5DE7E3377E5A` |
| Game files | 196 files, 6,555,220,594 bytes, copied from the disc |

Title Update 6 and supported DLC are optional and installed the same way, from the launcher's Content page, using your own licensed update and DLC files.

Requirements:
- **Windows**: Windows 10 or 11 (64-bit) and a DirectX 12 graphics card.
- **Linux**: 64-bit x86 Linux with glibc 2.31 or newer (such as Ubuntu 20.04 or later), a Vulkan driver, and an X11 or Wayland desktop.

## Downloads
**Windows**: **Crackdown-v0.6.2-windows-x64.zip**
1. Unzip it anywhere you can write to. It contains one folder, `Crackdown-v0.6.2-windows-x64`.
2. Run **crackdown.exe** from that folder.
3. On the launcher's **Play** page click **Install from disc image...** and pick your own Crackdown `.iso`, or **Install from folder...** for extracted disc files.
4. Press **PLAY**.

**Linux and Steam Deck (Experimental)**: **CrackdownRecompiled-v0.6.2-x86_64.AppImage**
1. Put it in a folder you can write to and mark it executable (right-click > **Properties** > **Permissions** > **Is executable**, or `chmod +x`).
2. Run it and install your own disc image from the **Play** page, as on Windows. The game files go in a `game` folder next to the AppImage.
3. On a Steam Deck, do this in **Desktop Mode**, then add the AppImage with **Games > Add a Non-Steam Game to My Library** and play it from **Game Mode**.

## Reporting Problems
If the game does not start, crashes or freezes, please open an [issue](https://github.com/saulob/crackdown-xbox360-recompiled/issues). Attach the newest `crackdown_*.log` file from the `logs` folder next to the program (next to the AppImage on Linux), and after a freeze any `hang_<n>.txt` file, with a line about what you were doing. The launcher's **About** page opens that folder. For Linux reports, please also say your distribution and graphics card, and whether you start the AppImage from Steam.

## Checksums
SHA-256 files are published with the release for both downloads: `Crackdown-v0.6.2-windows-x64.sha256` and `CrackdownRecompiled-v0.6.2-x86_64.sha256`.

| File | SHA-256 |
| --- | --- |
| `Crackdown-v0.6.2-windows-x64.zip` | `fa1a6f9a46a3d9ea213a9898b86f11fcc35c855ddf6e42322e81ded15550b421` |
| `CrackdownRecompiled-v0.6.2-x86_64.AppImage` | `e5468433e2c8fce57e68202b606f68148c7c84eb76b3c51254139b5e84245400` |
