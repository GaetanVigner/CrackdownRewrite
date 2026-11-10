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
