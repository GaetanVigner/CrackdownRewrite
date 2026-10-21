#pragma once

#include <atomic>

namespace crackdown::main_menu_quit {

enum class Phase { Inactive, MainMenu, QuitPending, QuitConfirmation };
enum class Begin { PassThrough, Started, Busy };

class QuitState {
 public:
  void EnterMainMenu() { phase_.store(Phase::MainMenu, std::memory_order_release); }
  void Leave() { phase_.store(Phase::Inactive, std::memory_order_release); }

  bool InMainMenu() const { return Load() != Phase::Inactive; }
  bool Confirming() const { return Load() == Phase::QuitConfirmation; }

  Begin BeginQuit() {
    Phase expected = Phase::MainMenu;
    if (phase_.compare_exchange_strong(expected, Phase::QuitPending, std::memory_order_acq_rel)) {
      return Begin::Started;
    }
    return expected == Phase::Inactive ? Begin::PassThrough : Begin::Busy;
  }

  bool ShowPopup() {
    Phase expected = Phase::QuitPending;
    return phase_.compare_exchange_strong(expected, Phase::QuitConfirmation, std::memory_order_acq_rel);
  }

  void EndQuit() {
    Phase expected = Phase::QuitPending;
    if (!phase_.compare_exchange_strong(expected, Phase::MainMenu, std::memory_order_acq_rel)) {
      expected = Phase::QuitConfirmation;
      phase_.compare_exchange_strong(expected, Phase::MainMenu, std::memory_order_acq_rel);
    }
  }

 private:
  Phase Load() const { return phase_.load(std::memory_order_acquire); }

  std::atomic<Phase> phase_{Phase::Inactive};
};

}  // namespace crackdown::main_menu_quit
