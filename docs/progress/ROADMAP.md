# Crackdown: roadmap

Generated on 2026-11-13 with [PROGRESS.md](PROGRESS.md). The rewrite is done when nothing of the Xbox 360 or of ReXGlue's runtime is left under the game: every system call on `any360::sys`, graphics on `any360::gfx`, Any360 as the host, and the game's code as portable C++. Steps are in working order; each lists what it still needs.

## 1. System calls on any360::sys

All 248 imports run on `any360::sys` (all eight groups, opt-in on `main`: `--any360_sys_groups=stateless,files,objects,threads,memory,xam,input,audio`; no call reaches ReXGlue's kernel then).

Still to do: make it the default build (today it is opt-in), then remove the ReXGlue kernel from the link.

## 2. Graphics on any360::gfx

The title draws through `any360::gfx`: opt-in on `main`: `--any360_gpu=true` (resolution scale, host MSAA, pipeline cache); 160-220 FPS at 1440p with 4x MSAA in the city on an RTX 5070 Ti.

Still to do: make it the default renderer (today ReXGlue's Xenos emulation is), then drop the emulated GPU from the build.

- `main` needs an Any360 checkout next to the repository to build (`CRACKDOWN_ANY360_DIR`, by default `../any360`): make it optional, as in The Simpsons Game, or fetched by the build.
- The game-side draw records (HLE renderer, milestones H1-H4 below) feed a renderer that knows the game's passes instead of translating Xenos commands; they move onto `any360::gfx` once the emulated path is the fallback only.

GPU features `any360::gfx` does not handle yet (40 of 323; none stops this title today, each is one a title may need):

| Family | Features |
|---|---|
| command processor | MEM_WRITE_CNTR, COND_EXEC, EVENT_WRITE_CFL, DRAW_INDX_2_BIN, SET_STATE, SET_BIN_BASE_OFFSET, IM_STORE |
| primitive types | kTriangleWithWFlags, kExplicitMajorModeForceStart, k2DCopyRectListV1, k2DCopyRectListV2, k2DCopyRectListV3, k2DFillRectList, k2DLineStrip, k2DTriStrip |
| render features | tessellation |
| texture formats | k_1_REVERSE, k_1, k_8_B, k_16_16_EDRAM, k_8_8_8_8_A, k_16_16_16_16_EDRAM, k_32, k_32_32, k_32_32_32_32, k_32_AS_8, k_32_AS_8_8, k_16_MPEG, k_16_16_MPEG, k_8_INTERLACED, k_32_AS_8_INTERLACED, k_32_AS_8_8_INTERLACED, k_16_INTERLACED, k_16_MPEG_INTERLACED, k_16_16_MPEG_INTERLACED, k_32_32_32_FLOAT, k_DXT3A_AS_1_1_1_1, k_8_8_8_8_GAMMA_EDRAM, k_2_10_10_10_FLOAT_EDRAM |
| vertex formats | kUndefined |

## 3. Any360 as the host

Shared by every title (Any360's own roadmap); the title follows once they land.

| Any360 step | What | State |
|---|---|---|
| 11 | third and fourth titles (Test Drive Unlimited, Sonic 06) | partial: Test Drive Unlimited boots to its title screen with all eight groups on `any360::sys` and no call reaching the runtime; gameplay, saves and sound not checked yet; Sonic 06 not started |
| 12 | paths no title took yet: `XMA*` decoding, STFS packages, several users, guide UIs, Live offline, xbdm | partial: 602 of 2,914 exports native with scenario tests, none of the 2,294 reference stubs sorted |
| 13 | Any360's own host: XEX loader, address space, threads, guest dispatch, MMIO, devices | not started |
| 14 | the platform layer: window, input, audio, presenter on Windows and Linux (Steam Deck) | partial: SDL3 audio output only |
| 15 | graphics through `any360::gfx` only, no `rexgpu-xenos` | partial: three titles draw through `any360::gfx`, the GPU session's plan in [docs/GFX.md](https://github.com/GaetanVigner/Any360/blob/main/docs/GFX.md) |
| 16 | porting a new title: a converter served from GitHub (as AnyPS5) and built into the cross-platform launcher, from a XEX to a running build with no title-specific code in Any360 | not started |

## 4. Library and middleware code

SDK code linked into the executable is not decompiled: Any360 provides the same service natively and the whole range goes.

| Library range | Functions | Instructions | Replaced | Verified in the sandbox | Left |
|---|---|---|---|---|---|
| XDK D3DX and XGraphics (shader compiler) | 4,171 | 487,670 | 39 | 0 | 4,132 |
| C runtime (CRT) | 595 | 41,901 | 24 | 0 | 571 |
| XDK D3D runtime | 553 | 34,977 | 49 | 252 | 504 |
| XDK audio (XAudio, XMA) | 484 | 28,795 | 13 | 0 | 471 |
| XDK Xbox Live and voice (XOnline, XHV) | 378 | 21,114 | 5 | 0 | 373 |
| XAPI (Win32 layer over the kernel and XAM) | 194 | 9,291 | 2 | 0 | 192 |
| kernel import thunks | 2 | 6 | 0 | 0 | 2 |

Third-party middleware the game links (physics, AI, scripting, video and the like) is neither the game's own code nor something Any360 provides. It is not rewritten function by function: it stays recompiled, or is replaced by the vendor's library for the target platform (or an open implementation) once the game's code around it is portable.

| Middleware | Functions | Instructions |
|---|---|---|
| Havok Physics | 2,699 | 191,551 |
| Kynapse (AI) | 947 | 56,021 |
| SpeedTree | 560 | 42,091 |
| Lua | 600 | 30,433 |
| Massive (in-game advertising) | 368 | 16,437 |
| Bink video | 127 | 16,392 |

## 5. Game code as portable C++

29,260 of 29,541 game functions are still recompiled PowerPC (29 of them already have a sandbox test to check a rewrite against). The list, function by function, is [functions.csv](functions.csv) (state `recompiled` or `verified`). By module, largest first:

| Module | Functions left | Instructions left | Rewritten | Verified |
|---|---|---|---|---|
| code 82Bxxxxx | 6,495 | 101,041 | 24 | 0 |
| code 821xxxxx | 3,830 | 210,006 | 41 | 0 |
| code 823xxxxx | 3,800 | 255,110 | 52 | 0 |
| code 825xxxxx | 3,788 | 247,698 | 51 | 0 |
| code 826xxxxx | 3,342 | 221,218 | 24 | 29 |
| code 824xxxxx | 3,306 | 253,049 | 47 | 0 |
| code 822xxxxx | 2,407 | 254,870 | 28 | 0 |
| code 82Axxxxx | 1,251 | 65,167 | 7 | 0 |
| code 829xxxxx | 677 | 33,309 | 5 | 0 |
| code 827xxxxx | 364 | 25,189 | 2 | 0 |

Order of work: symbol recovery and library recognition first (they shrink this list), then leaf functions (no calls: each is checked alone), then their callers, module by module; every rewrite is bound over the recompiled function and proven equal in the guest sandbox before it replaces it (Any360's `pipeline/any360.py <title> select | decompile | bind | verify`).

## 6. This title

- Native renderer from the game's own draw records: H2 switch the scene one render pass at a time (depth prepass first), H3 every pass native, H4 draws recorded on several threads and per-object motion vectors.
- FSR upscaling on the Steam Deck, which needs those motion vectors.
- Steam Deck as the primary target: the same Vulkan renderer on Linux, CPU-bound today.
- Follow upstream releases (merged up to v0.6.2).

## Known problems

- Single-tile scene (experimental): a shadow ghost is left at some spots.
- Keyboard and mouse through `any360::sys` input are not checked (gamepad and vibration are).
