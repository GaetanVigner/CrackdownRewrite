# Building Crackdown

Normal development, with guest code that is current:

    .\tools\build.ps1          # build
    .\tools\build.ps1 -Run     # build, then launch the game

When `build.ps1` prints `Guest code: stale`, regenerate the guest code once, then build:

    .\tools\codegen.ps1
    .\tools\build.ps1 -Run

- **Guest code** (`generated\default`) is regenerated only by `tools\codegen.ps1`. `build.ps1` never runs codegen. It
  compares content hashes of the XEX, `crackdown_manifest.toml` and every include with
  `generated\codegen-inputs.stamp`, and stops when they differ. A patch that touches `src/codegen/` changes the
  codegen tool too: `codegen.ps1` then builds `rexglue.exe` from the patches into `out\rexglue-codegen` and uses it,
  and the stamp records that tool, so adding or editing such a patch makes the guest code stale.
- **ReXGlue runtime and GPU plugin** (`tools\rexglue\win-amd64`) are checked by content hash on every build. The
  fingerprint covers the baseline in `patches\rexglue-sdk\baseline.env`, the build settings in
  `tools\rexruntime-patch.ps1`, the toolchain and every `*.patch` file. When it does not match, `build.ps1` rebuilds
  the runtime itself, or restores the stock files when there are no patches. Adding, editing, renaming or removing a
  patch is enough; nothing else needs to change.
- **DLLs next to `crackdown.exe`** are compared with the installed files by hash after every build and before `-Run`.
  `-Run` does not start a stale or mismatched build.

Manual, for diagnosis only:

    .\tools\build-patched-rexruntime.ps1 -Check     # report the installed runtime and GPU plugin
    .\tools\build-patched-rexruntime.ps1 -Restore   # put the stock release files back

Only the requested `-Config` is built and installed (default: `RelWithDebInfo`), for targets `rexruntime` and
`rexgpu-xenos`. Debug and Release keep separate runtime stamps; all configurations use the same patch set.
Pass the same `-Config` to manual `-Check` or `-Restore` commands.

Toolchain detection restores `.EXE` to `PATHEXT` when missing before invoking `vswhere.exe`. This is needed when
a Windows PowerShell process inherits an incomplete environment from WSL; builds still use native Windows paths
and Visual Studio's Clang, LLD, CMake, Ninja and x64 SDK environment.

## Profile-guided optimization of the GPU plugin

The GPU plugin (`rexgpu-xenos.dll`, which holds the command processor) is built with profile-guided optimization in
Release when `patches\rexglue-sdk-pgo\rexgpu-xenos.profdata` exists (patch 0102). The profile's SHA-256 is part of the
runtime fingerprint, so replacing it rebuilds the runtime, and deleting it builds the plugin without the profile. The
profile was recorded with an instrumented build of the plugin (clang `-fprofile-generate`) during five minutes of city
driving and combat on foot, and merged with Visual Studio's `llvm-profdata`.

Functions whose code changed since the profile was made are optimized without it (clang matches functions by name and
control-flow hash, and the build silences the out-of-date warnings), so a stale profile only means less optimization,
never a failed build. Static functions are named with their source path, so their part of the profile only applies to a
clone at the same path as the one it was made in; member and other external functions match anywhere.

## Tests, launcher and packaging

    .\tools\test.ps1                         # pure-logic tests (user data, settings file, window placement, installer, ...)
    .\tools\build.ps1 -Target <names>        # runtime-linked tests (allocator stress, title-thread shutdown, RenderDoc detect)
    .\tools\test-release-check.ps1           # the release scanner accepts a clean folder and refuses player data and game files
    .\tools\package-release.ps1              # local Release build and dist\Crackdown-v<version>-windows-x64.zip
    .\tools\stamp-release-hashes.ps1         # writes the artifacts' SHA-256 into the release notes

Two kinds of tests compare outputs:

- `vulkan_per_draw_tests` runs each faster Vulkan path (sampler table, dynamic constant buffers, constant copies,
  residency masks, descriptor reuse) against the code it replaced on random inputs, and checks the patch text still
  holds the mirrored code.
- `guest_d3d_leaf_tests` runs the recompiled leaf functions of the title's D3D runtime (no calls, no MMIO) in a guest
  sandbox (`tests/guest_sandbox.h`: a 4 GB guest address space committed on first touch) on seeded random devices.
  Each function must give the same result twice from the same state, and its outputs must match
  `tests/golden/guest_d3d_leaf.txt` (hashes only), so a codegen or SDK change that alters a translated function shows
  up; `CRACKDOWN_UPDATE_GOLDEN=1` rewrites the hashes after checking such a change. `--layout` lists the device fields
  each function writes against the layout the HLE records use. The functions are extracted from the local
  `generated/default` at configure time (the test is skipped without it); no guest code is kept in the repository.

In-game measurements in one sitting:

    .\tools\game-test-battery.ps1 -List                  # the batteries and their runs
    .\tools\game-test-battery.ps1 -Battery vulkan-gpu    # runs each launch with its settings, then writes logs\battery-<time>.md
    .\tools\game-test-battery.ps1 -ReportOnly -Since '2026-10-29 01:00'   # the report for logs already written

The battery saves the player's settings, applies each run's (diagnostics off, frame statistics on), tells what to do
in game, waits for the game to close, restores the settings and reports FPS, the GPU command thread and process CPU,
the host GPU time and its categories, the IssueDraw sections and the errors of each run.

`package-release.ps1` builds into `build-release\`, stages only the allow-listed files and refuses to continue if
the staging folder holds game files, saves, logs, settings or local paths. Nothing is published or uploaded.
The packaged `trg-updater.exe` is built by `tools\build-updater.ps1` with MSYS2 CLANG64 (static, Windows system DLLs
only); its `trg-updater.debug` goes to `dist\Crackdown-v<version>-windows-x64-symbols\`, outside the ZIP.

The launcher is the TRG library in `third_party/trg-launcher` (git submodule, pinned commit). CMake copies it to
`<build dir>/trg-launcher-patched`, applies `patches/trg-launcher` to that copy, and builds it there, so the submodule
checkout stays clean. It has no upstream license file; get one from its author before distributing a package.

Patches with a `.windows` or `.linux` suffix apply only on that platform; unsuffixed patches apply everywhere (since
2026-10-29 every patch but three Linux ones is unsuffixed).
See `docs/rexglue-patches.md` for what each patch is for.

## Linux

Codegen only runs on Windows. The Linux scripts use the `generated\default` it wrote, and
`tools/check-guest-code.sh` stops them when `generated/codegen-inputs.stamp` no longer matches the manifest, the XEX,
the includes and the codegen patches.

    tools/build.sh [release|relwithdebinfo|debug]   # build into out/build/linux-amd64-<config>
    tools/test.sh                                    # tests that need no game files
    tools/package-linux.sh [--clean] [--out-dir DIR] # Release build and AppImage in dist/
    tools/build-patched-rexruntime.sh --check tools/rexglue/linux-amd64 --config Release   # report the installed runtime

`tools/build-patched-rexruntime.sh` is the Linux counterpart of `tools\build-patched-rexruntime.ps1`: the same pinned
source and patches, the Linux patch set, and a stamp checked before every build and after staging. The CMake build runs
the same check. See `docs/linux.md` for the build environment and the AppImage.
