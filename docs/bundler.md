# crackdown-bundler

`crackdown-bundler` makes one ready-to-play package from a published Crackdown Recompiled release and **your own**
Crackdown disc image. The package starts the game directly: the launcher finds the game files already in place and the
**Install from disc image...** step is not needed.

## Personal use only

- The package contains game files copied from your disc image. They are copyrighted by their owners. The package is for
  your own use on your own devices: do not share, upload, sell or otherwise distribute it.
- The tool runs entirely on your computer. It has no upload, share or download feature and never connects to the
  network.
- Every package carries a `PERSONAL-USE-ONLY.txt` notice (inside the AppImage: `usr/share/doc/crackdown/`).
- Releases of Crackdown Recompiled, and of this tool, never contain game files. The project's automated builds never
  download, use or store game data: the bundler's tests build a small synthetic disc image of their own.

## Usage

```
crackdown-bundler --iso <disc image or folder> --release <release> --target windows|linux --out <path>
```

| Option | Meaning |
| --- | --- |
| `--iso` | Your Crackdown disc image (`.iso`), or a folder with the files extracted from it. It is checked exactly as the launcher's install checks it: the same Xbox 360 disc reader, the same list of required files and sizes, the same SHA-256 of `default.xex` (`src/game_files.cpp`, `src/game_manifest.h`). Another version of the game is refused. |
| `--release` | Windows: `Crackdown-v<version>-windows-x64.zip`, or the folder extracted from it. Linux: `CrackdownRecompiled-v<version>-x86_64.AppImage`, or a folder holding exactly one AppImage. A release folder you already play from works too: its `game`, `user_data` and `logs` folders are not copied. |
| `--target` | `windows`, `linux` (also `steamdeck`) or `android` (see below). |
| `--out` | What to write. It must not exist yet. Its name chooses the format (below). |

About 6.6 GB of free space is needed for a folder package, and about twice that for a `.zip` or an `.AppImage` (the game
files are copied into a work folder next to the output first, which is removed at the end). A failed or interrupted run
leaves no partial output; a work folder left by a crash (`<out>.bundler-work`) is recognised and removed by the next run.

### Windows

```
crackdown-bundler --iso D:\Crackdown.iso --release Crackdown-v0.4.0-windows-x64.zip --target windows --out C:\Games\Crackdown
crackdown-bundler --iso D:\Crackdown.iso --release Crackdown-v0.4.0-windows-x64.zip --target windows --out Crackdown-personal.zip
```

- **Folder** (any name without `.zip`): the release files, `game\` next to `crackdown.exe`, and the notice. Run
  `crackdown.exe`. `user_data` and `logs` are created next to it, as in a normal install.
- **`.zip`**: the same folder inside a ZIP64 archive (the game files stored without compression). Extract it and run
  `crackdown.exe`.
- **A single self-extracting `.exe` is not offered**: the game files are about 6.5 GB and Windows refuses to start any
  executable file larger than 4 GB ("not a valid application for this platform"; checked on Windows 11 with a 5 GB
  file). The folder or the `.zip` is the portable form.

### Linux and Steam Deck

```
crackdown-bundler --iso ~/Crackdown.iso --release CrackdownRecompiled-v0.4.0-x86_64.AppImage --target linux --out ~/Games/Crackdown.AppImage
crackdown-bundler --iso ~/Crackdown.iso --release CrackdownRecompiled-v0.4.0-x86_64.AppImage --target linux --out ~/Games/Crackdown
```

- **`.AppImage`**: one AppImage with the game files in `game/` inside its file system. Copy it to the Deck, make it
  executable and add it to Steam as a non-Steam game, as described in [linux.md](linux.md). `user_data` and `logs` stay
  **outside** the image, next to the AppImage, exactly as with the published AppImage: the image is read-only.
- **Folder** (any other name): the unchanged release AppImage with `game/` next to it, which is the layout the launcher's
  own install produces. Prefer it where AppImages cannot be mounted with FUSE: the AppImage then unpacks itself into a
  temporary folder before every start (see "If the AppImage does not start" in [linux.md](linux.md)), and for an
  AppImage that holds the game that means copying about 6.5 GB each time.

How the AppImage is built: the release AppImage's runtime (the ELF program before the file system) is kept byte for
byte; its SquashFS file system is read (zstd or gzip, as appimagetool writes it) and written again with `game/` added.
The release's `AppRun` is kept as `AppRun.release`, and a new `AppRun` starts it with `--game_data_root=<mount>/game`
(dropping a `--game_data_root` that a restart from the launcher carries over from the previous mount). Everything else
in the release is unchanged. After writing, the tool reads the new AppImage back and checks every required file's size
and the SHA-256 of `default.xex`.

**SquashFS from Windows.** The tool reads and writes SquashFS itself (`tools/bundler/src/squashfs.cpp`), so the Linux
AppImage can be made on Windows as well as on Linux, without squashfs-tools, WSL or root. The image it writes stores
every block uncompressed: game data barely compresses, any SquashFS reader accepts it (the kernel, squashfuse which the
AppImage runtime uses, unsquashfs), and the game reads it without decompression. CI checks the written image with
unsquashfs and squashfuse, and the reader against images made by mksquashfs.

### Android

There is no Android build of Crackdown Recompiled: the game is built for x86-64 PCs (it uses FMA3) and has never been
compiled for ARM64. `--target android` only says so.

## Building

The tool is a separate CMake project that needs neither the ReXGlue SDK nor the game build. It links the launcher's own
disc reader and installer (`cmake/game_files_library.cmake`) and fetches two pinned libraries by SHA-256: miniz 3.0.2
(ZIP, gzip) and the zstd 1.5.6 decompressor.

```
cmake -S tools/bundler -B out/bundler -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/bundler
ctest --test-dir out/bundler --output-on-failure
```

On Windows, run this from a Visual Studio developer prompt with clang (`-DCMAKE_C_COMPILER=clang
-DCMAKE_CXX_COMPILER=clang++`), as `tools/build.ps1` does for the game.

`.github/workflows/bundler.yml` builds the tool for windows-x64 and linux-x64, runs the tests, uploads both binaries as
artifacts, and attaches them to the GitHub release when a tag is pushed.

The test program `crackdown_bundler_tests` has three extra modes for checking images by hand:
`--write-image <folder> <image>`, `--compare-image <image> <offset> <folder>` and `--list-image <image> <offset>` (the
offset of a published AppImage's file system is printed by `<AppImage> --appimage-offset`).
