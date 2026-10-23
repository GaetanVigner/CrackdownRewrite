# Title Update 6 guest mapping

`crackdown.exe` (the `crackdown` binary on Linux) is the only host executable. It contains two separately recompiled
guests, the base game (`generated/default`) and Title Update 6 (`generated/tu6`), and every process runs exactly one of
them. The host sources are shared; only each guest's generated code, bindings and layout differ. The AppImage holds the
same single executable.

## Guest selection

- Start-up selects the guest before the runtime exists (`CrackdownApp::Create`). `SelectStartup` resolves the user data
  folder (`--user_data_root`, else `<port folder>/user_data`) and reads the installed content from it. Title Update 6 is
  selected when it is installed and verified; otherwise the base guest. Free-For-All requires Title Update 6: with it
  installed and no usable update the base guest is selected and the launcher blocks Play.
- `SelectGuest` makes the selected `GuestImage` (`src/guest_image.h`) the process's guest and hands its image to
  ReXGlue. A second, different selection aborts. Nothing of the other guest is initialized or registered.
- The Title Update 6 guest applies the installed `Default.xexp` through `xex_patch` and `--update_data_root`. The
  update, `Default.xexp` and `GameStrings_DLC.str` stay user-supplied; none is embedded or packaged. The base game file
  is `default.xex`. The case of these names is significant on Linux.
- A process cannot change guests. When the launcher installs or removes Title Update 6, Play relaunches the same
  program (the original AppImage on Linux) with `RelaunchSelf`, and the new process selects again.

## Save generations

The base game and Title Update 6 write incompatible profile and campaign save files (base writes version 4; Title
Update 6 writes 5 and 6 and reads 4). Neither guest is changed. `src/save_generation.*` keeps one snapshot per
generation under `<user_data>/4D5307DC/save-versions/{base,tu6}` (the profile folder `profile/User`, the campaign
container `Crackdown.sav` and its header) and swaps the live files before the selected guest starts.

- The live generation comes from the files (`src/save_compat.*`): version 4 or no save is base, a newer version or chunk
  type is Title Update 6. Malformed files switch nothing.
- A start whose guest matches the live generation writes nothing. Otherwise the live state refreshes the snapshot of its
  own generation, then the target generation's snapshot, if there is one, becomes the live state. Without a Title
  Update 6 snapshot the base saves stay and Title Update 6 upgrades them itself.
- With no base snapshot, going to the base game preserves the Title Update 6 state, creates an empty base snapshot (no
  profile files, no campaign save, no header) and leaves the live saves empty, so the base game starts a new save. A
  base snapshot that exists but is malformed is never restored or replaced, and the switch is refused.
- The switch stages and validates the target, preserves the live state, then swaps folders with a journal; an
  interrupted switch is rolled back at the next start. Links and junctions in any copied tree reject the switch.
- `SelectStartup` makes the switch for every start; the launcher makes it before relaunching for a guest change.
  Removing or reinstalling the update never touches `save-versions`, which is user data and is never packaged.

## Symbol isolation

Both guests are generated from a project named `crackdown`, so their `extern "C"` functions (`sub_XXXXXXXX`,
`__imp__sub_XXXXXXXX`, `xstart`, `__save*`, `__rest*`) and image tables (`PPCImageConfig`, `PPCFuncMappings`,
`crackdown_RegisterFunctions`) share names. `cmake/crackdown_guests.cmake` compiles each guest with a generated
`crackdown_guest_symbols.h` (`guest_base_` or `guest_tu6_`) that renames them by macro, and `src/guest_symbol_macros.h`
makes the `__imp__` half follow. Kernel imports (`__imp__KeWaitForSingleObject`, ...) are shared with the runtime and
not renamed. The bindings are compiled with the same header, so each guest's hooks override only that guest's weak
function aliases. Regenerating either guest needs no change here; CMake rewrites the header from `crackdown_funcs.h`.

## Layout

- `src/guest_layout.h` holds one `GuestLayout` per build: data globals, return addresses, struct offsets, and constants.
  `Layout()` returns the table of the guest this process runs.
- `src/guest_hooks.h` declares the address-free hook handlers in `crackdown::hooks`. Their bodies are shared by both
  builds and read the layout instead of constants.
- `src/guest_bindings_base.cpp` and `src/guest_bindings_tu6.cpp` bind each guest function to a handler with
  `CRACKDOWN_BIND` (or `CRACKDOWN_BIND_REPLACEMENT`). These two files are the only host files that name guest function
  addresses, and each defines its guest's `GuestImage`. `src/guest_binding.h` defines the macros.
- `cmake/crackdown_guests.cmake` builds each guest's generated code and bindings as object libraries and links both
  into `crackdown`. `generated/tu6` is optional: without it `crackdown` carries the base guest only.

## Midasm hooks

- `config/overrides.toml` (base) and `config/tu6/overrides.toml` (Title Update 6) each bind the same named hooks to
  their own addresses.
- Title Update 6 codegen bakes these hooks in, so `.\tools\codegen-tu6.ps1` must run after any change to
  `config/tu6/overrides.toml`. The freshness stamp includes that file.

## Address map

- `config/tu6/address-map.csv` is the evidence record. Columns: `kind, name, tu0, tu6, tu0_word, tu6_word, tu0_size,
  tu6_size, evidence`. Kinds are `fn`, `call`, `site`, `return`, `data`, and `layout`.
- `tools/verify-address-map.py` checks both images against their pinned SHA-256 values. It checks function skeleton
  similarity and the instruction words at site and return rows.

```
python tools/verify-address-map.py game/extracted/default_image.bin out/tu6/default_image.bin
```

## Tests

`tests/guest_layout_tests.cpp` (registered through `tests/cmake/guest_layout.cmake`) checks:

- the CSV agrees with both layout tables;
- bindings and midasm sites agree with the CSV for each build;
- no guest address is shared between builds;
- shared sources contain no guest address;
- every critical Title Update 6 target is mapped, except the Jump Height launch;
- only the Jump Height cheat is disabled in Title Update 6.

`tests/guest_isolation_tests.cpp` checks that each guest has one descriptor, only the selected one reaches the runtime,
and every binding names a function its own guest declares. `tests/content_state_tests.cpp` covers start-up selection.
`tests/sdk_patch_guards_tests.cpp` checks that midasm addresses are unique within each override file.

## Known gaps

- Jump Height is not available with Title Update 6. Its launch code moved to a virtual call on a character sub-object,
  so no binding exists. `Supported(Cheat::JumpHeight)` is false for that build and the launcher shows it as unavailable.
- Three Title Update 6 functions use the exact 1/30 step with no base counterpart and are not scaled above 30 FPS:
  8221e810, 8254f7d8 and 82567548. These need a runtime check at 60 and 120 FPS.
