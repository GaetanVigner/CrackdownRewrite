# ReXGlue patches

Baseline: ReXGlue v0.10.0 (`patches/rexglue-sdk/baseline.env`). Only `rexruntime` and `rexgpu-xenos` are rebuilt, and the
result is installed over the stock files in `tools/rexglue`. The game compiles against the stock SDK headers, so a patch
must not move virtual functions or data members of classes the generated game code uses.

`NNNN-name.patch` applies on both platforms, `NNNN-name.windows.patch` only on Windows and `NNNN-name.linux.patch` only on
Linux. Numbers are unique and patches apply in name order; gaps are fine. 116 patches are common and 3 are Linux only, so 116 apply
on Windows and 119 on Linux. Every patch that was Windows only became common (2026-10-29): most touch only D3D12
files, which are in the Linux tree but not compiled there, and the rest keep their Windows code behind
`REX_PLATFORM_WIN32` or `if(WIN32)`; this way the shared-code optimizations (0100, 0109-0113) reach the Linux
build and the Steam Deck (checked: the Linux runtime and GPU plugin build with clang-20 on Ubuntu 20.04). The Vulkan patches are common since the Windows plugin carries the Vulkan backend too
(0114). Patches 0005, 0052 and 0095 change the code generator and
are the reason `tools\codegen.ps1` builds its own `rexglue.exe`; the rest change the runtime and the GPU plugin.

| # | Platform | Purpose |
|---|----------|---------|
| 0001 | all | `XamReadTileToTexture` ABI fix (achievement icons) |
| 0003 | all | physical heap release order |
| 0004 | all | excluded audio channels |
| 0005 | all | codegen: `vpkd3d128` reads its source in place |
| 0006 | all | D3D12 occlusion query results written when the submission completes (Agency target indicators) |
| 0007 | all | XMA context synchronisation fixes |
| 0008 | all | stop guest threads on title termination, with a timeout |
| 0009 | all | RenderDoc: detect only when already loaded |
| 0010 | all | D3D12 adapter preference (high-performance GPU) |
| 0011 | all | Vulkan device preference |
| 0012 | all | log host MSAA 2x support |
| 0013 | all | SPIR-V texture exponent adjust |
| 0014 | all | cache the Vulkan swapchain paint pipeline |
| 0015 | all | timer queue blocks instead of spinning |
| 0016 | all | batch performance counter reads |
| 0017 | all | vblank thread sleeps until due |
| 0018 | all | vblank thread priority |
| 0019 | all | command processor waits instead of polling |
| 0020 | all | WAIT_REG_MEM woken by vblank |
| 0021 | all | high-resolution sleep |
| 0023 | all | UI repaint once per guest frame |
| 0024 | all | GPU register writes: block copy for render-state, constant and fetch-constant runs |
| 0025 | all | draw path: primitive cache threshold and overlap fix, reused request allocation, Xenia defaults for `clear_memory_page_state` and `execute_unclipped_draw_vs_on_cpu` |
| 0026 | all | guest critical sections poll briefly before a kernel wait |
| 0027 | linux | idle command thread waits without the alertable flag |
| 0028 | linux | `WaitMultiple` backs off on lock contention instead of spinning |
| 0030 | all | render target transfers cut around rectangles a draw provably overwrites; stencil bits tracked per depth target |
| 0031 | all | per-tile stencil bits and a conservative first row for ownership claims |
| 0032 | all | resolve dumps only EDRAM tiles drawn since the last dump |
| 0033 | all | command processor guest memory writes wake `WaitOnAddress`/futex waiters; `src/gpu_fence_wait.cpp` is the waiter |
| 0034 | all | early submission only while fewer than three are in flight, `PollBackgroundWork` finishes pending occlusion queries, no `direct_host_resolve` preflight |
| 0035-0037 | all | D3D12 halves of 0030-0032 |
| 0038-0040 | all | Vulkan halves of 0030-0032, submissions and occlusion queries |
| 0041-0043 | all | resolves copied straight into the textures that sample them (`resolve_copy_into_textures`) |
| 0044-0046 | all | above Original resolution, a resolve kept only in its texture is written back to memory when needed (`resolve_defer_writeback`) |
| 0047-0049 | all | `internal_render_scale` 100/75/50: host render targets below the render resolution |
| 0050 | all | XMA context loop layout acquire loads |
| 0051 | all | audio callbacks paced at the hardware's steady rate (`audio_callback_pacing`) |
| 0052 | all | codegen: bit-exact `vrsqrtefp` estimate instead of an exact 1/sqrt; the helper is emitted into the generated `crackdown_pch.h` because the game compiles against the stock SDK headers; regenerate with `tools\codegen.ps1` |
| 0053 | all | Vulkan occlusion query results, the counterpart of 0006: the guest's begin and end blocks are separate, and the end block stays unfinished until the submission completes |
| 0054 | all | keyboard and mouse binds also reach `XInputGetKeystroke` (menus such as Press Start), with the SDL driver's repeat timing |
| 0055 | all | mouse look sets the right stick from mouse speed (`mnk_mouse_scale`, `mnk_mouse_deadzone`), so slow movement still turns the camera; `mnk_mouse_stick` lets a title switch the mouse-derived stick off for its own mouse look |
| 0056 | all | `readback_resolve_max_bytes`: with `readback_resolve` on, only resolves of at most this many bytes are copied back to guest memory; larger ones stay GPU-only and may defer their writes (`resolve_defer_writeback`) |
| 0057 | all | D3D12 half of 0056 |
| 0058 | all | Vulkan half of 0056 |
| 0059 | all | a read fault on an uncommitted page in 0x70000000-0x70FFFFFF (save-load) maps one zeroed page, at most 8 pages, each logged as `[SAVE_RECOVERY]`; writes and other ranges are not recovered |
| 0060 | all | `XamEnumerate` sets the item count to zero before any error return and no longer copies an uninitialised value (an unimplemented `XamCreateEnumeratorHandle` made the title walk off its stack) |
| 0061 | all | `KeSaveFloatingPointState` and `KeRestoreFloatingPointState` stubs warn on the first call only (the title calls them every frame) |
| 0062 | all | the "Incorrect resolve sample selected" warning is logged once per sample/depth/MSAA combination; the fallback is unchanged |
| 0063 | all | an unhandled guest fault also logs `[SAVE_CRASH]` with the page's allocation and its place relative to the faulting thread's stack (from the PCR): own stack, guard, the page above, or other memory |
| 0064 | all | the "has \"invalid\" type" texture fetch warning is logged once per distinct fetch constant, up to 16, then one overflow notice; handling is unchanged |
| 0065 | all | 0059 recovers only free or reserved pages away from the faulting thread's stack and its guard pages, never a page adjacent to one it already recovered, and nothing when the stack is unknown |
| 0066 | all | a read-only open that finds nothing (`NO_SUCH_FILE`, `OBJECT_NAME_NOT_FOUND`) and a path without a device (the kernel has no current directory) log at debug instead of warning/error; other `NtCreateFile` failures and unknown devices still warn |
| 0067 | linux | when SDL video cannot start, stderr also gets the SDL error and the graphical-session hint (DISPLAY for X11, WAYLAND_DISPLAY for Wayland) before the normal failure exit |
| 0068 | all | the guest time base is read without a lock: it runs at one ratio from the last ratio change, so reading it no longer takes the clock mutex on every `mftb` |
| 0069 | all | the command processor also wakes `WaitOnAddress`/futex waiters after its read pointer and scratch register writebacks (the Direct3D ring waits in `src/gpu_fence_wait.cpp`) |
| 0070 | all | on Windows a missing host file is not searched for again by listing its folder after the exact name was not found (names are case-insensitive); `NtQueryFullAttributesFile` translates its path once |
| 0073 | all | an occlusion query index is reused only after the pending query that held it has its result |
| 0078 | all | a shared memory request whose pages are all valid and allocated returns without the lock (the invalidation callback clears valid bits with release stores); `gpu_invalidation_expand_pages` (16; 64 is the original whole block) limits the pages a CPU write drops to an aligned group |
| 0079 | all | the register block copy covers every range without write side effects, not only render state: everything but the shader constants, scratch registers, `COHER_STATUS_HOST` and the gamma ramp ports |
| 0080 | all | `gpu_draw_state_memo`: an update claiming the same render target ranges as the previous one, with the ownership unchanged since, skips the ownership walk |
| 0081 | all | `d3d12_async_submission`: a "D3D12 Submission" thread replays, executes and signals the recorded submissions in order, and a swap waits for it to drain; `d3d12_async_submission_draws` (2000) also ends a submission between draws outside occlusion queries. The thread is joined in the command processor's destructor and a repeated start does not create a second worker |
| 0082 | all | D3D12 half of 0080: draws without transfers or clears skip the transfer pass, and sampler parameters are recomputed only when the shader, a fetch constant or `anisotropic_override` changed |
| 0083 | all | `gpu_host_threads_avoid_cpu0`: the command processor and submission threads run on the process's CPUs except 0 and 1 when it has at least 8 in one processor group |
| 0084 | all | the ImGui drawer applies the cursor shape ImGui asked for through `SDL_SetCursor`, which nothing did otherwise (`imgui_drawer.cpp`), so launcher hyperlinks show the hand cursor |
| 0085 | all | `DetachFileSink` removes and closes the rotating file sink without destroying loggers, so a controlled hard exit can delete the session log without freeing logger state other threads may still use |
| 0086 | all | `XamLoaderLaunchTitle` keeps the expanded launch path in an owning `std::string`; a `std::string_view` pointed into the temporary from `utf8_join_guest_paths()` for a relative XEX name (`-Wdangling-assignment-gsl`) |
| 0087 | all | `RtlCompareStringN` compares up to the shorter real length (0xFFFF = `strlen`) and then orders by length, as `RtlCompareString`; before, "abc" and "abcd" compared equal |
| 0088 | all | D3D12 texture load: `host_slice_size_base` starts at 0 (a `-Wuninitialized` false positive; the conditional lvalue write is not tracked, and every read follows a write) |
| 0089 | all | draw extent estimate: a negative scissor edge no longer counts as the 8192 "no scissor" case (signed comparison against `int32_t(xenos::kTexture2DCubeMaxWidthHeight)`); non-negative scissors are unchanged |
| 0090 | all | D3D12 render target cache: the direct resolve success/fallback counters that 0034 left unused are removed (the class is private to the GPU plugin) |
| 0091 | all | warning cleanup without behaviour change except `MakePacketType0`, which now sets its one-register bit (its only caller passes false): `void*` casts for the deliberate texture-key `memcpy`/`memset`, unused parameter names, initializer lists in declaration order, unsigned `-1` sentinels, no pessimizing `std::move`, range-loop references, `= {}` for `X_SLIST_HEADER`, and an unused lambda capture |
| 0092 | all | Windows half of 0091 and 0093: full `D3D12_HEAP_PROPERTIES` initializers, a missing `override`, explicit `GetProcAddress` casts, an unused lambda capture and an unused local |
| 0093 | all | unused locals removed where their initializer has no side effect; guest-memory reads, FidelityFX-only locals and the only use of a parameter keep their evaluation with `[[maybe_unused]]` |
| 0094 | all | a passive input-activity callback reports each device's changed state from `InputSystem::GetState`; the cursor policy reads it without altering gamepad state, merging or device assignment |
| 0095 | all | codegen: `vmaddfp`/`vnmsubfp` (and the 128 forms) round once like the hardware, and `vmsum3fp128`/`vmsum4fp128` sum in double and round once (Xenia's order) instead of `dpps`; with two roundings a unit length could round above 1.0. The helpers are emitted into the generated `crackdown_pch.h`: with `-mfma` or on ARM64 they use the native fused multiply-add, otherwise x64 checks the CPU once and uses `vfmadd213ps` on FMA3 hosts and the exact round-to-odd emulation elsewhere, with the same bits, so FMA3 is not required |
| 0096 | all | XMA `Work()`: a partly delivered frame keeps draining after the input runs out, an input-starved context sets its output write offset to the read offset, and output is invalidated only when the ring is full, so voices no longer stall for good after a long host hitch |
| 0097 | all | `rex::bit_scan_forward` is inline in `math.h` instead of defined in the runtime DLL: the GPU plugin calls it in its per-draw loops (constant maps, render target and texture bindings), and every call went through the import table. The runtime no longer exports it; nothing outside the SDK calls it. Portable, but Windows only until a Linux build checks it |
| 0098 | all | the GPU plugin builds with ThinLTO in Release (`/opt:lldlto=3`), so the command processor's per-draw path (render target, texture and pipeline caches, shared memory, register file) inlines across its translation units |
| 0099 | all | `gpu_skip_unchanged_constant_writes` (on): when the game rewrites float, bool, loop or fetch constants with the values they already have, the D3D12 command processor keeps the uploaded constant buffers, the texture bindings and the sampler memo instead of rebuilding them for the next draw. Vertex buffer residency is still reset on every fetch constant write, since the game may have rewritten the vertex data at the same address |
| 0100 | all | the texture cache indexes its textures by base page, so `FindResolveCopyTargets` looks at the textures on the resolve's page instead of scanning all of them on every resolve. Portable, but Windows only until a Linux build checks it |
| 0101 | all | D3D12 per-draw loops: vertex buffer residency is invalidated a 64-bit mask at a time instead of a bit at a time, the used float constants are copied a run of consecutive constants per `memcpy`, and the stencil tile bits are read and added in up to two contiguous runs that vectorize instead of a byte at a time with wrapping |
| 0102 | all | profile-guided optimization of the GPU plugin in Release: `REXGPU_PGO_USE` names a merged profile to optimize it with, and `tools\rexruntime-patch.ps1` sets it to `patches\rexglue-sdk-pgo\rexgpu-xenos.profdata` when that file exists (see [build-workflow.md](build-workflow.md#profile-guided-optimization-of-the-gpu-plugin)) |
| 0103 | all | `d3d12_present_latency_wait` (on, restart): the swap chain has a frame-latency waitable object (at most 2 frames queued) and painting waits on it before recording, so a V-Sync forced by the driver no longer makes `Present` wait for a back buffer on the direct queue the GPU emulation submits to; `ResizeBuffers` keeps the same flags and the handle is closed with the swap chain |
| 0104 | all | `xex_patch` (runtime option): the guest path of the XEX patch applied to the main module, instead of the `.xexp` next to the XEX. The Title Update 6 guest sets it to `update:\Default.xexp` with `--update_data_root` pointing at the installed update, so the patch never sits in the game folder the base build reads. A patch file is not given a patch of its own; with the option set, a missing file fails the load. Unset, nothing changes |
| 0105 | all | Tracy zones and the perf counters only in Debug (they were in every configuration but Release): they cost the GPU command processor about a tenth of its time, so RelWithDebInfo, the profiling build, now performs like Release. The Tracy DLL is still built and staged; the game, compiled against the stock headers, still has its (inactive) zones |
| 0106 | all | the base and mip extents of a texture key's guest layout, needed on every binding change at a draw resolution scale, come from a 1024-entry direct-mapped table (about 4% of the command processor thread) |
| 0107 | all | `gpu_frame_stats` (off): every 10 s, logs what the guest command stream asked per frame (draws, resolves, type-0/type-3 packets and their registers, indirect buffers, constant registers by source, shader loads, texture lookups and creations), counted with plain increments on the command processor thread |
| 0108 | all | `gpu_draw_trace` = <file> (cleared once done) records the next `gpu_draw_trace_frames` frames (default 1) from a swap: per draw or resolve, the draw initiator, index buffer, predication and visibility-query kill, the first ucode dwords of both shaders, and the raw render-state, float, fetch and bool/loop constant registers (about 12 KB a draw). HLE renderer milestone 1 compares it with the game-side capture of the D3D device |
| 0109 | all | `gpu_cached_indirect_buffers` (on, hot reload): an indirect buffer executed again with the same address and size (the precompiled command buffers of mesh assets, replayed every frame) is decoded once into the operations its packets perform and replayed without parsing while its dwords are unchanged (one `memcmp` against the copy taken at translation). Replays make the parse's calls in its order; register runs that already hold their values, where rewriting them changes nothing, are skipped. Other type-3 packets (waits, events with memory writes, swaps) run their own handler from the buffer's dwords; buffers with a malformed packet are parsed as before, and buffers whose dwords keep changing (ring segments) stop being translated. Bypassed while `gpu_draw_trace` records. HLE renderer milestone 3a |
| 0110 | all | `gpu_draw_identity_memo` (off, hot reload; needs 0109's cache): a draw of a cached indirect buffer has an identity (its DRAW operation and occurrence in the frame) and a memo of what `IssueDraw` derived for it: pixel shader use, shader variants and translations, pipeline and root signature, color mask, viewport, used textures, and the texture bindings (keys and textures, reused without parsing the fetch constants or looking textures up while no texture was destroyed and the scaled resolve state is unchanged). The memo is reused when the shaders are the same and a 64-bit hash of the render state (0x2000-0x23FF but the resolve registers) and of the used textures' fetch constants matches; float, bool and loop constants are not inputs of any of it. `gpu_frame_stats` also logs the time `IssueDraw` takes per draw by memo outcome. HLE renderer milestone 3b |
| 0111 | all | `gpu_fetch_constant_caches` (on, hot reload): what texture bindings and sampler parameters derive from a texture fetch constant is kept in direct-mapped tables indexed by a hash of its six dwords: the binding key, signs and host format swizzle, the textures last looked up for the key (reused while no texture was destroyed and the scaled resolve state of memory is unchanged, or still the texture's), and the sampler parameters (also keyed on the binding's filter overrides and `anisotropic_override`). About 6,000 binding updates and 3,000 sampler parameter computations a frame in the city, from few distinct fetch constants |
| 0112 | all | `gpu_edram_bookkeeping_memo` (on, hot reload): every draw marks the EDRAM tiles of each render target it writes dirty and adds stencil bits to them; a range within one of the last four set dirty since tiles were last made clean, or within the render target's last stencil addition with no new bits since its bits were last set, is skipped (both are idempotent, and consecutive draws to the same render targets repeat them) |
| 0113 | all | `gpu_draw_trace_resolve_data`: a draw trace also writes the bytes of every resolve (`<trace>_resolves.bin`) |
| 0114 | all | the Vulkan backend is built on Windows too, and `gpu_backend` (`any`/`d3d12`/`vulkan`) picks the one the plugin creates |
| 0115 | all | `vulkan_async_submission` (off: slower here, see the patch): the Vulkan counterpart of 0081, a "Vulkan Submission" thread replays and submits the recorded command buffers in order; fences are only polled once submitted |
| 0116 | all | `gpu_frame_stats` on Vulkan: IssueDraw timed in 15 sections (ns a draw) and render pass entries counted, logged every 10 s with the GPU command thread's CPU time |
| 0117 | all | cheaper Vulkan draws: the memexport readback check (a by-name cvar lookup) only for draws that export, and a table of sampler parameters keyed by the fetch constant |
| 0118 | all | `gpu_ab_cvar` / `gpu_ab_values`: with `gpu_frame_stats` on Vulkan, a hot-reloadable cvar alternates every 10 s window so one run measures A and B in the same scene; `vulkan_sampler_parameters_cache` toggles 0117's table |
| 0119 | all | `vulkan_reuse_texture_descriptors` (on): a stage's texture descriptor set is kept while its image views and samplers are unchanged (~15 ns a draw) |
| 0120 | all | `vulkan_skip_unchanged_constant_writes` (on), the Vulkan half of 0099 (no measurable effect here: the constants do change) |
| 0121 | all | `gpu_frame_stats` on Vulkan: UpdateBindings timed in five subsections, descriptor sets written per frame counted |
| 0122 | all | Vulkan constants through dynamic uniform buffers: a set names the upload buffers, offsets are given at bind, so ~5,000 set writes a frame become none (+20-25% FPS at the city spot); upload pages keep a reserved tail |
| 0123 | all | `gpu_frame_stats` on Vulkan: EndSubmission, its command buffer replay and submit, and IssueSwap timed per frame |
| 0124 | all | portable `__rdtsc` include for the frame stats timers (`<x86intrin.h>` off MSVC-style toolchains), needed by the Linux build since 0110 became common |
| 0125 | all | Vulkan float constants copied a run per memcpy and vertex buffer residency cleared a 64-bit mask at a time (as 0101); `gpu_frame_stats` adds the whole process CPU time and the host GPU time per frame (timestamps around each replayed command buffer) |
| 0126 | all | `gpu_frame_stats` on Vulkan: host GPU time per frame by category (draw passes, render target updates, resolves, swap, other), from timestamps at render pass entries and exits and at changes of what the command processor does |
| 0127 | all | `gpu_frame_stats` on Vulkan: render pass ends per frame by reason (barriers, forced copies and dispatches, render target changes, occlusion queries, submission ends) |
| 0128 | all | `vulkan_occlusion_queries_in_render_pass` (off): occlusion queries run inside the guest's render passes, one host query per pass a guest query spans, summed, instead of ending the pass around each query |

Patches 0054 to 0058, 0078 to 0083, 0095, 0096, 0103 and 0115 and the hooks in `src/audio_guest_sync.cpp` and
`src/vehicle_action_step.cpp` are adapted from Unowa001/crackdown-recomp (BSD 3-Clause, notice in
[THIRD-PARTY-LICENSES.txt](../packaging/licenses/THIRD-PARTY-LICENSES.txt)).

The AppImage runtime has its own pin and patch in `patches/appimage-runtime` (see [linux.md](linux.md)).

ThinLTO (`CMakeLists.txt`) covers Release and RelWithDebInfo; the LTO cache is `<build dir>/lto-cache`.

## Hooks in `config/overrides.toml`

The XAudio worker rendezvous in `sub_82A50000` has no kernel wait; on title termination a codegen `[[midasm_hook]]` lets it
return (`crackdown_audio_barrier_escape`). The `crackdown_variable_step_frame_ms` hooks give the title's inline 16 ms
frame-time sites the host-paced frame time above 60 FPS. `crackdown_audio_wave_timeout_tick` at `0x8225B254` advances a
wave's safety timeout at 30 Hz at any frame rate, so speech is not stopped early above 30 FPS.
`crackdown_audio_pcm_firewall` sanitizes the mixed audio frame before it is submitted.

The Havok vehicle action (`sub_824A1708`, hooked in `src/vehicle_action_step.cpp`) loses speed per physics step, so above
30 FPS cars pulled away 7-12% slower. Each car's action runs once per 1/30 s of physics time with the time accumulated
since its last run; steps of 25 ms or more, so every step at 30 FPS, run unchanged.

The character hooks in `src/character_step_fixes.cpp` turn per-step terms the title tuned for 1/30 s into per-second ones
above 30 FPS, and change nothing at 30: the ledge and slope snap-down pull (`0x822B5634`, shorter steps only), the
vertical visual smoothing (`0x822A2DB8`, `0x822A2E28`), the first gravity increment after a grounded step (`0x822AA8F4`),
the hit-reaction and ragdoll tick windows (`0x822E191C`, `0x822BA48C`), the pose step of a restarted ragdoll
(`0x822BA2E8`) and the projectile impact ray (`0x823672B0`). `crackdown_crowd_anim_step` (`0x823308DC`) and
`crackdown_cosmetic_timer_step` (`0x823FF534`) scale two per-update constants, the crowd animation clock's 1/30 and a
0.033 s timer, to the update's length.
The crowd library's movement had the same flaw: its agents (pedestrians and far, low-detail traffic) advance by a
step sized for the 1/30 s update and never read dt, so above 30 FPS they moved up to 8x too fast and slowed down when
they became full NPCs near the player. `src/crowd_step_fixes.cpp` scales the walk and run step factors (1/30, 2/30)
in the steering update and at spawn, the 0.05 displacement blend per update, and the 30 updates a second that turn
the step into a velocity at the hand-off to a full NPC. The far traffic (crowd cars) gets the same treatment: their distance and heading blend per update, and
the per-update distance that counts the updates to the next road node. A twin of the ragdoll threshold window (`0x822B9EFC`) keeps
its 30 FPS length like the one at `0x822BA48C`. Two props use the same per-update step: the Agency garage door adds
1/30 to its seconds timer every update (`0x8252EDE8`), and above 30 FPS its 5 s window for the player to reach the
picked vehicle ran out while the door was still opening, so it closed again; the lift light fades by 2/30 per update
(`0x8253130C`). Both now advance by their step per original 1/30 s update.

The title's frame timer (`sub_823263D8`, hooked in `src/frame_pacing.cpp`) keeps the fixed step's minimum frame, so with
the fixed step off it published 0 ms at `0x82D99128` on almost every other 60 FPS frame; a camera mode change on such a
frame divided by it, and the camera, the player and the audio listener went NaN. With the fixed step off the hook
publishes the port's own frame time instead, never 0.

### Measured causes of the high frame rate fixes

- Ledge snap-down: the follower divides its pull by the step's dt, and the Havok proxy turned that vertical velocity
  into speed along the slope, which ran away about 1.64x per step at 120 FPS (the launches on slopes and off ledges).
- First gravity step: a grounded step resets the vertical speed accumulator, so settling and sliding ran at g * dt,
  four times slower at 120 FPS, long enough for the ledge follower to stop horizontal motion (the stuck-in-walls
  reports).
- Ragdoll restart: body start velocities are (pose - previous pose) / dt, and the previous pose stops updating while
  ragdolled; a second hit differenced a stale pair over one 1/120 s step, 160-270 m/s, through floors.
- Projectile ray: rockets and grenade rounds detect impacts only with one ray of launch velocity * dt, and Havok
  contacts never detonate them; at 1/120 s the ray barely passed the rocket's nose, so the rocket vanished.

### Guards (`src/visibility_grid_guard.cpp`, `src/nan_guards.cpp`)

- `crackdown_visibility_grid_guard` (`0x822541D8`, AudioCore `sub_82252150`): a NaN listener angle converted by `fctiwz`
  made the audio occlusion grid read 256 MB below its table (`Unhandled guest access violation ... 0x72D32F98`, issue
  #8). Spans that would leave the table are clamped; the title's own reads near it are untouched.
- `sub_824FD840` wrapper: the player's aim point (from the camera ray or a locked target's bone) is kept finite; a
  target whose body went NaN (a ragdoll that blew up) no longer feeds NaN into the look-at target, head and listener.
- `crackdown_look_target_guard` (`0x822A026C`, `0x822A2C50`) and `crackdown_audio_listener_guard` (`0x82252134`) repair
  a non-finite x, y or z (the w lanes hold garbage in the shipped game and are left alone).
- `sub_82287130` wrapper (camera update): a camera that comes out with a non-finite basis, position or listener point
  (seen on the step a ragdoll starts) is put back to its state before the update; a NaN camera made the stick useless.
- `crackdown_camera_lift_divisor` (`0x8228FDDC`, `sub_8228F990`): the above-water push divides by the camera forward
  axis's y; a level camera no longer makes it infinite.

### Hang watchdog (`src/hang_watchdog.cpp`)

When the window stops answering messages for 5 s, or the title produces no frame for 10 s while in the foreground,
every thread's call stack (symbolized from the PDB) is written to `hang_<n>.txt` in the session logs folder.

### Developer diagnostics (all off by default, set in `settings.toml`)

| Setting | Key | What it writes |
|---|---|---|
| `crackdown_physics_telemetry = true` | F9 | the last ~34 s of the player's and driven vehicle's raw state, `telemetry_<n>.bin`, also on the first NaN repair of a kind (one dump per 20 s) |
| `crackdown_debug_profiler = true` | F6 | a 10 s sampling profile of every thread using 5% of a core or more, `profile_<n>.txt` (self and total time per function) |
| `crackdown_debug_profiler = true` | F5 | cycles off, `gpu_cached_indirect_buffers` (0109) with `gpu_fetch_constant_caches` (0111), then also `gpu_edram_bookkeeping_memo` (0112), for an A/B/C at one spot; with `gpu_frame_stats` the log gives the time `IssueDraw` takes per draw and per subsection |
| `crackdown_hle_capture = true` | F8 | the HLE renderer's milestone 1 capture: `hle_game_<n>.bin` (D3D device state after every draw call) and `hle_gpu_<n>.bin` (`gpu_draw_trace`, 0108), compared with `tools/hle-trace-compare.ps1` |
| `crackdown_debug_frame_dips = <ms>` | | delays frames by up to that many ms (100 at most), sweeping every 6 s with jitter and rare stalls, to test physics under frame rate dips |
| `gpu_frame_stats = true` | | every 10 s, per frame: draws, resolves, packets, constant registers, shader loads and texture lookups (0107) |
