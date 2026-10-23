#pragma once

// The rename of the guest this file is compiled for: the bindings below name that guest's functions only.
#include "crackdown_guest_symbols.h"

#include <rex/hook.h>

#include "guest_hooks.h"
#include "guest_image.h"

extern const rex::PPCImageInfo PPCImageConfig;

// The guest function at `address`, and its own code (the part a hook replaces and can still call).
#define CRACKDOWN_GUEST_FUNC(address) CRACKDOWN_GUEST_JOIN(sub_, address)
#define CRACKDOWN_GUEST_ORIGINAL(address) CRACKDOWN_GUEST_JOIN(__imp__, CRACKDOWN_GUEST_FUNC(address))

// The guest function at `address` runs `handler` instead, with its own code available to the handler as `original`.
#define CRACKDOWN_BIND(address, handler)                                                      \
  REX_EXTERN(CRACKDOWN_GUEST_ORIGINAL(address));                                              \
  REX_HOOK_RAW(CRACKDOWN_GUEST_FUNC(address)) {                                               \
    crackdown::hooks::handler(ctx, base, CRACKDOWN_GUEST_ORIGINAL(address));                  \
  }

// The guest function at `address` is replaced outright. Naming its original fails the link when the guest has no
// function there, instead of leaving the replacement unused.
#define CRACKDOWN_BIND_REPLACEMENT(address, handler)                                          \
  REX_EXTERN(CRACKDOWN_GUEST_ORIGINAL(address));                                              \
  [[gnu::used]] static const auto CRACKDOWN_GUEST_JOIN(bound_, address) =                     \
      &CRACKDOWN_GUEST_ORIGINAL(address);                                                     \
  REX_HOOK_RAW(CRACKDOWN_GUEST_FUNC(address)) {                                               \
    crackdown::hooks::handler(ctx, base);                                                     \
  }

// A guest function the hooks call without replacing it.
#define CRACKDOWN_GUEST_CALL(address) REX_EXTERN(CRACKDOWN_GUEST_ORIGINAL(address));
