#include <atomic>

#include <rex/hook.h>
#include <rex/logging.h>

#include "guest_hooks.h"
#include "guest_layout.h"
#include "language_setting.h"
#include "settings.h"

// Applies the language the console reports when D:\language.ini is absent; other callers keep the original call.
void crackdown::hooks::ApplyLanguage(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  if (uint32_t(ctx.lr) == crackdown::Layout().console_language_return) {
    static std::atomic<bool> logged{false};
    const crackdown::GameLanguage& language = crackdown::LanguageFor(REXCVAR_GET(crackdown_language));
    ctx.r3.u64 = language.title_index;
    if (!logged.exchange(true)) {
      REXLOG_INFO("Language: {} ({})", language.label, language.key);
    }
  }
  original(ctx, base);
}
