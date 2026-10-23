#include "main_menu_quit.h"

#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <string_view>
#include <utility>

#include <rex/hook.h>
#include <rex/ppc.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "guest_hooks.h"
#include "guest_layout.h"
#include "guest_memory.h"
#include "main_menu_quit_state.h"

namespace crackdown::main_menu_quit {

namespace {

using guest::Load32;
using guest::Plausible;

constexpr uint32_t kHelpId = 878;
constexpr uint32_t kSaveAndQuitId = 655;
constexpr uint32_t kQuitId = 656;
constexpr uint32_t kSavedUnits = 64;

struct StringOverride {
  uint32_t id = 0;
  uint32_t address = 0;
  uint32_t length = 0;
  uint16_t original[kSavedUnits]{};
  bool active = false;
};

QuitState g_state;
std::mutex g_handler_mutex;
std::function<void()> g_quit_handler;

StringOverride g_help_override{kHelpId};
StringOverride g_save_and_quit_override{kSaveAndQuitId};
StringOverride g_quit_override{kQuitId};

uint16_t LoadGuest16(uint8_t* base, uint32_t address) {
  return uint16_t(guest::Load8(base, address) << 8 | guest::Load8(base, address + 1));
}

void StoreGuest16(uint8_t* base, uint32_t address, uint16_t value) {
  const uint16_t be = std::byteswap(value);
  std::memcpy(rex::memory::GuestPtr<uint8_t*>(base, address), &be, sizeof(be));
}

void StoreWide(uint8_t* base, uint32_t address, std::u16string_view text) {
  for (size_t i = 0; i < text.size(); ++i) {
    StoreGuest16(base, address + uint32_t(i * 2), uint16_t(text[i]));
  }
  StoreGuest16(base, address + uint32_t(text.size() * 2), 0);
}

void OverrideString(StringOverride& override, uint8_t* base, uint32_t address, std::u16string_view text) {
  if (!Plausible(address)) {
    return;
  }
  if (!override.active) {
    override.address = address;
    override.length = 0;
    while (override.length < kSavedUnits) {
      const uint16_t unit = LoadGuest16(base, address + override.length * 2);
      override.original[override.length++] = unit;
      if (unit == 0) {
        break;
      }
    }
    override.active = true;
  }
  StoreWide(base, address, text);
}

void RestoreString(StringOverride& override, uint8_t* base) {
  if (!override.active) {
    return;
  }
  for (uint32_t i = 0; i < override.length; ++i) {
    StoreGuest16(base, override.address + i * 2, override.original[i]);
  }
  override.active = false;
  override.address = 0;
  override.length = 0;
}

void ApplyStringOverride(uint8_t* base, uint32_t id, uint32_t address) {
  if (id == kHelpId && g_state.InMainMenu()) {
    OverrideString(g_help_override, base, address, u"Quit the game");
  } else if (g_state.Confirming()) {
    if (id == kSaveAndQuitId) {
      OverrideString(g_save_and_quit_override, base, address, u"Quit Game");
    } else if (id == kQuitId) {
      OverrideString(g_quit_override, base, address, u"Quit Game");
    }
  }
}

}  // namespace

void InvokeQuitHandler() {
  std::function<void()> handler;
  {
    std::lock_guard lock(g_handler_mutex);
    handler = g_quit_handler;
  }
  if (handler) {
    handler();
  }
}

bool NativeConfirmationActive() { return g_state.Confirming(); }

void SetQuitHandler(std::function<void()> handler) {
  std::lock_guard lock(g_handler_mutex);
  g_quit_handler = std::move(handler);
}

void MarkMainMenuActive() { g_state.EnterMainMenu(); }

void LeaveMainMenu(uint8_t* base) {
  g_state.Leave();
  RestoreString(g_help_override, base);
  RestoreString(g_save_and_quit_override, base);
  RestoreString(g_quit_override, base);
}

Begin BeginNativeConfirmation() { return g_state.BeginQuit(); }

bool ShowNativeConfirmation() { return g_state.ShowPopup(); }

void EndNativeConfirmation(uint8_t* base) {
  g_state.EndQuit();
  RestoreString(g_save_and_quit_override, base);
  RestoreString(g_quit_override, base);
}

bool InFrontend(uint8_t* base) {
  const uint32_t frontend = Load32(base, Layout().frontend_singleton);
  return Plausible(frontend) && Load32(base, frontend + 12) == 0;
}

void Install() {}

}  // namespace crackdown::main_menu_quit

// The quit block normally disables the control when the frontend session flag is clear. Take its existing
// event-handler branch instead, then continue through the game's own enable call. In a session (pause menu) the
// block already takes that branch and the main-menu state stays off.
void crackdown_main_menu_quit_use_handler() {
  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  if (crackdown::main_menu_quit::InFrontend(base)) {
    crackdown::main_menu_quit::MarkMainMenuActive();
  } else {
    crackdown::main_menu_quit::LeaveMainMenu(base);
  }
}

void crackdown_main_menu_quit_handler_attached() {}

void crackdown_main_menu_quit_enable(PPCRegister& enabled, PPCRegister& control, PPCRegister& self) {
  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  const uint32_t quit = crackdown::guest::Load32(base, self.u32 + 0x10);
  if (crackdown::guest::Plausible(quit)) {
    control.u64 = quit;
  }
  enabled.u64 = 1;
}

void crackdown_main_menu_quit_popup_state(PPCRegister& state) {
  if (crackdown::main_menu_quit::ShowNativeConfirmation()) {
    state.u64 = 2;
  }
}

// Opens the popup of the main menu's Quit block.
void crackdown::hooks::MenuPopupOpen(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  switch (crackdown::main_menu_quit::BeginNativeConfirmation()) {
    case crackdown::main_menu_quit::Begin::Busy:
      ctx.r3.u64 = 1;
      return;
    case crackdown::main_menu_quit::Begin::Started:
      original(ctx, base);
      if (!crackdown::main_menu_quit::NativeConfirmationActive()) {
        crackdown::main_menu_quit::EndNativeConfirmation(base);
      }
      return;
    case crackdown::main_menu_quit::Begin::PassThrough:
      original(ctx, base);
      return;
  }
}

// The popup's Yes handler.
void crackdown::hooks::MenuPopupYes(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  if (!crackdown::main_menu_quit::NativeConfirmationActive()) {
    original(ctx, base);
    return;
  }
  crackdown::main_menu_quit::EndNativeConfirmation(base);
  original(ctx, base);
  crackdown::main_menu_quit::InvokeQuitHandler();
}

// The popup's No handler.
void crackdown::hooks::MenuPopupNo(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  if (!crackdown::main_menu_quit::NativeConfirmationActive()) {
    original(ctx, base);
    return;
  }
  crackdown::main_menu_quit::EndNativeConfirmation(base);
  original(ctx, base);
}

// The popup's destructor, which also runs when it is cancelled.
void crackdown::hooks::MenuPopupDestroyed(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  const bool pending = crackdown::main_menu_quit::NativeConfirmationActive();
  original(ctx, base);
  if (pending) {
    crackdown::main_menu_quit::EndNativeConfirmation(base);
  }
}

// The localized string lookup: r3 is the string id and returns the text.
void crackdown::hooks::MenuStringById(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  const uint32_t id = ctx.r3.u32;
  original(ctx, base);
  crackdown::main_menu_quit::ApplyStringOverride(base, id, ctx.r3.u32);
}

// The Quit block's destructor: it leaves the main menu.
void crackdown::hooks::MenuBlockDestroyed(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  crackdown::main_menu_quit::LeaveMainMenu(base);
  original(ctx, base);
}
