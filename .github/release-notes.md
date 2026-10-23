# Crackdown Recompiled v0.6.1

Crackdown for Xbox 360 as a native PC recompilation, with enhanced graphics, high frame rates, modern display support, keyboard and mouse support, a full launcher, offline achievements and language selection. It runs natively on Windows, and experimentally on Linux and Steam Deck as an AppImage.

v0.6.1 adds DLC and Title Update 6 support with save preservation, and brings a major GPU performance pass to the Direct3D 12 command processor.

Thanks to @DyllanM for the GPU command processor work (#13), and to [Unowa001/crackdown-recomp](https://github.com/Unowa001/crackdown-recomp) (BSD 3-Clause) for the vehicle-physics and present-latency fixes it is adapted from.

## Highlights
- **DLC support**, with save preservation and compatibility improvements: installing, removing or switching DLC never touches your saves, and a failed install leaves your existing game and save data untouched.
- **Major GPU performance improvements**: profile-guided optimization, ThinLTO and a leaner Direct3D 12 command processor cut per-draw CPU overhead by roughly 7-16% in heavy scenes, measured on city drives with 12,000-15,000 draws per frame.
- **Additional upstream performance and gameplay fixes** for smoother, more stable gameplay, including a vehicle-physics fix adapted from Unowa001/crackdown-recomp and a present-latency fix on Windows.
- **Improved save handling**: the pause menu's **Save and Quit** now uses the game's own save flow again, and the launcher's **Ctrl+S** reliably saves settings even while a text field has focus.
- **Orb map improvements**: the in-game map can show agility and hidden orb markers, correctly matched to whichever guest (base game or Title Update 6) is running.

## What's New

### Title Update 6 and DLC
- **Title Update 6 support**: the launcher can install Title Update 6 alongside the base game. `crackdown.exe` carries both recompiled guests; the one that runs is selected from what is installed, and a process never switches guests mid-run.
- **DLC support**: supported Marketplace DLC packages can be installed and removed from the launcher's Content page. DLC is detected and classified by its own package metadata; packages for another title or an unsupported pack are rejected before anything is written.
- **Save preservation across TU6 and DLC changes**: the base game and Title Update 6 use incompatible save generations, so switching between them keeps one snapshot per generation and swaps the live files with a journaled, crash-safe move. Installing, removing or switching DLC does not touch the game files, the other DLC, or any save.

### Graphics & Performance
- **GPU command processor optimizations**: unchanged GPU constants, stencil bits, and used-constant copies are now detected and skipped cheaply; the texture cache looks up resolve targets by page instead of scanning the whole cache; the Windows GPU plugin builds with ThinLTO and an optional profile-guided optimization pass. Net effect: noticeably lower CPU cost per draw call in GPU-bound scenes, with no change to rendering output.
- **Upstream gameplay and timing fixes** ported from the community Unowa001/crackdown-recomp fork and DyllanM's work, including a 1/30 s vehicle action step fix and a Direct3D 12 present-latency wait on Windows.

### Launcher & Saves
- **Pause menu Save and Quit restored**: quitting from the in-game pause menu goes through the game's own save-and-quit flow again, instead of the frontend-only quit confirmation.
- **Ctrl+S reliably saves launcher settings**, including while a text field in the launcher has keyboard focus.
- **Orb map markers**: agility and hidden orb locations can be shown on the in-game map, using the correct marker data for the guest that is actually running (base game or Title Update 6).

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
**Windows**: **Crackdown-v0.6.1-windows-x64.zip**
1. Unzip it anywhere you can write to.
2. Run **crackdown.exe**.
3. On the launcher's **Play** page click **Install from disc image...** and pick your own Crackdown `.iso`, or **Install from folder...** for extracted disc files.
4. Press **PLAY**.

**Linux and Steam Deck (Experimental)**: **CrackdownRecompiled-v0.6.1-x86_64.AppImage**
1. Put it in a folder you can write to and mark it executable (right-click > **Properties** > **Permissions** > **Is executable**, or `chmod +x`).
2. Run it and install your own disc image from the **Play** page, as on Windows. The game files go in a `game` folder next to the AppImage.
3. On a Steam Deck, do this in **Desktop Mode**, then add the AppImage with **Games > Add a Non-Steam Game to My Library** and play it from **Game Mode**.

## Reporting Problems
If the game does not start, crashes or freezes, please open an [issue](https://github.com/saulob/crackdown-xbox360-recompiled/issues). Attach the newest `crackdown_*.log` file from the `logs` folder next to the program (next to the AppImage on Linux), and after a freeze any `hang_<n>.txt` file, with a line about what you were doing. The launcher's **About** page opens that folder. For Linux reports, please also say your distribution and graphics card, and whether you start the AppImage from Steam.

## Checksums
SHA-256 files are published with the release for both downloads: `Crackdown-v0.6.1-windows-x64.sha256` and `CrackdownRecompiled-v0.6.1-x86_64.sha256`.

| File | SHA-256 |
| --- | --- |
| `Crackdown-v0.6.1-windows-x64.zip` | `5af8c7e2c99b41a6640d7cdf611960d34969cea3032f24a001fe7b7185b18a09` |
| `CrackdownRecompiled-v0.6.1-x86_64.AppImage` | `3206e06902a734b2d1f02225cdce9d2886e17eba3aa401a8458a2f83bb489a8e` |
