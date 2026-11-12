// The title's memory functions as host routines: the C runtime's memcpy, its plain forward copy, memmove and
// memset, and the XDK's vector copy. All are Any360's native functions, shared by every title (any360::xdk: unit
// tests, a benchmark and the any360_xdk_verify / any360_xdk_bench settings, Any360's docs/NATIVE_FUNCTIONS.md);
// each hands the calls it does not take to the function's own code.

#include <cstdint>

#include "any360/host/xdk_natives.h"
#include "guest_hooks.h"
#include "guest_layout.h"

// memcpy: r3 destination, r4 source, r5 size; returns the destination, which it also leaves at r1 - 8.
void crackdown::hooks::CrtMemcpy(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  any360::host::rexglue::xdk::CrtMemcpy(ctx, base, original);
}

// The plain forward copy memcpy and the vector copy use for short ranges: r3 destination, r4 source, r5 size.
void crackdown::hooks::CrtCopyForward(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  any360::host::rexglue::xdk::CrtCopyForward(ctx, base, original);
}

// memmove: r3 destination, r4 source, r5 size; returns the destination. Below the source (signed compare) it is
// memcpy, which leaves the destination at r1 - 8; otherwise it copies backward.
void crackdown::hooks::CrtMemmove(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  any360::host::rexglue::xdk::CrtMemmove(ctx, base, original);
}

// memset: r3 destination, r4 fill byte, r5 size; returns the destination.
void crackdown::hooks::CrtMemset(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  any360::host::rexglue::xdk::CrtMemset(ctx, base, original);
}

// The XDK's vector copy (16-byte vectors, cache lines zeroed ahead): r3 destination, r4 source, r5 size; returns the
// destination. Below 16 bytes its byte loop subtracts a global from the count, 1 in this guest: the layout says
// where it is.
void crackdown::hooks::XdkVectorCopy(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  any360::host::rexglue::xdk::XMemCpy(ctx, base, original, crackdown::Layout().vector_copy_byte_step);
}

// The C runtime's string functions (wcslen, strchr, strrchr, strncmp and tolower only read and leave their value in
// r3; strncpy, strncat, wcscpy and wcsncpy write). toupper (sub_82AB6A08) has the same replacement in Any360 and is
// not bound: no run of the game has called it with both sides compared.
void crackdown::hooks::CrtWcslen(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  any360::host::rexglue::xdk::CrtWcslen(ctx, base, original);
}
void crackdown::hooks::CrtStrchr(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  any360::host::rexglue::xdk::CrtStrchr(ctx, base, original);
}
void crackdown::hooks::CrtStrrchr(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  any360::host::rexglue::xdk::CrtStrrchr(ctx, base, original);
}
void crackdown::hooks::CrtStrncmp(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  any360::host::rexglue::xdk::CrtStrncmp(ctx, base, original);
}
void crackdown::hooks::CrtStrncpy(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  any360::host::rexglue::xdk::CrtStrncpy(ctx, base, original);
}
void crackdown::hooks::CrtStrncat(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  any360::host::rexglue::xdk::CrtStrncat(ctx, base, original);
}
void crackdown::hooks::CrtWcscpy(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  any360::host::rexglue::xdk::CrtWcscpy(ctx, base, original);
}
void crackdown::hooks::CrtWcsncpy(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  any360::host::rexglue::xdk::CrtWcsncpy(ctx, base, original);
}
void crackdown::hooks::CrtTolower(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  any360::host::rexglue::xdk::CrtTolower(ctx, base, original);
}
void crackdown::hooks::CrtStricmp(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  any360::host::rexglue::xdk::CrtStricmp(ctx, base, original);
}
void crackdown::hooks::CrtStrstr(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  any360::host::rexglue::xdk::CrtStrstr(ctx, base, original);
}
// The C runtime's helpers on a double's bits: _isnan, _get_exp, _set_exp. _finite (sub_82AB9C18) and _sptype
// (sub_82AC44A8) have their replacements in Any360 too and are not bound: no run of the game has called them;
// _add_exp (sub_82AC4470) neither, and keeps the copy written here before (decomp/leaf_batch4.cpp).
void crackdown::hooks::CrtIsnan(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  any360::host::rexglue::xdk::CrtIsnan(ctx, base, original);
}
void crackdown::hooks::CrtGetExp(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  any360::host::rexglue::xdk::CrtGetExp(ctx, base, original);
}
void crackdown::hooks::CrtSetExp(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  any360::host::rexglue::xdk::CrtSetExp(ctx, base, original);
}
void crackdown::hooks::CrtWcscmp(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  any360::host::rexglue::xdk::CrtWcscmp(ctx, base, original);
}
void crackdown::hooks::CrtMbtowc(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  any360::host::rexglue::xdk::CrtMbtowc(ctx, base, original);
}
// The strlen and strcmp built as loops of bytes (sub_8294DFF8, sub_8294DFC8) are Havok's own, at the head of its
// code: middleware stays as it is, they are not bound.
// XAPI's GetLastError (sub_82746048) and SetLastError (sub_82745FF8) have their replacements in Any360, checked here
// with both sides compared (73,195 and 70,297 calls, none different), and are not bound: five instructions each,
// they gain nothing as host routines (--any360_xdk_bench: 1.1x and 1.0x).

// XAudio's mix with a volume ramp (r3 destination, r4 source, r5 and r6 the gains and their step, r7 samples):
// the audio threads' hottest function, as a host routine with the fused multiply-add the recompiled code uses.
void crackdown::hooks::XAudioMixRamp(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  any360::host::rexglue::xdk::XAudioMixRamp(ctx, base, original);
}

// The D3D runtime's upload of the shader constants a mask marks as changed into the command buffer (r3 device, r4
// mask, r5 index, r6 constants); this runtime keeps the buffer's write pointer at device + 48.
void crackdown::hooks::D3dDirtyConstants(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  any360::host::rexglue::xdk::D3dDirtyConstants(ctx, base, original, 48);
}

// The same upload for the constants of 4 bytes a bit (r3 device, r4 mask, r5 index, r6 words).
void crackdown::hooks::D3dDirtyWords(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  any360::host::rexglue::xdk::D3dDirtyWords(ctx, base, original, 48);
}
