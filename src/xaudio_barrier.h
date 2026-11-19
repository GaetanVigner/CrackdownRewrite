#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#endif

namespace crackdown {

// The rendezvous of the XAudio mix workers, kept on the host: the arrivals of the current round in the low byte (bit =
// processor) and the round number above them. Arrival and release are one compare-exchange, so a worker preempted
// after arriving can never be left behind in a round the others already released.
//
// A worker that arrives first waits for its partner: a short spin (kSpinPauses pauses, a few microseconds, no clock
// read: reading the clock in the spin cost more than the spin), then asleep until the releasing worker wakes it. The
// partner can be hundreds of microseconds away (it mixes more), and spinning that long burned a tenth of the title's
// processor time (Any360 docs/perf/archive/2026-11-19-profile). The sleeper looks at `escape` every kEscapeLook (the
// title terminating: a partner that stopped never releases).
class XAudioBarrier {
 public:
  struct Arrival {
    bool released_round;
    uint64_t round;
  };

  static constexpr int kSpinPauses = 256;
  static constexpr std::chrono::milliseconds kEscapeLook{10};

  // `expected` holds a bit per participating processor (0-7).
  Arrival Arrive(uint32_t processor, uint64_t expected) {
    uint64_t state = state_.load(std::memory_order_acquire);
    uint64_t next;
    do {
      const uint64_t arrived = (state & kArrivalMask) | (uint64_t(1) << processor);
      next = (arrived & expected) == expected ? (state | kArrivalMask) + 1 : (state & ~kArrivalMask) | arrived;
    } while (!state_.compare_exchange_weak(state, next, std::memory_order_acq_rel, std::memory_order_acquire));
    const bool released = (next >> 8) != (state >> 8);
    if (released) {
      // Taking the lock orders the release before any sleeper's last look at the round: none can miss the wake.
      { std::lock_guard<std::mutex> lock(mutex_); }
      released_.notify_all();
    }
    return {released, state >> 8};
  }

  bool RoundOpen(uint64_t round) const { return (state_.load(std::memory_order_acquire) >> 8) == round; }

  // Until round `round` is released, or `escape()` returns true.
  template <class Escape>
  void Wait(uint64_t round, Escape&& escape) {
    for (int i = 0; i < kSpinPauses && RoundOpen(round); ++i) {
#if defined(_M_X64) || defined(__x86_64__)
      _mm_pause();
#endif
    }
    std::unique_lock<std::mutex> lock(mutex_);
    while (RoundOpen(round)) {
      if (escape()) return;
      released_.wait_for(lock, kEscapeLook);
    }
  }

 private:
  static constexpr uint64_t kArrivalMask = 0xFF;
  std::atomic<uint64_t> state_{0};
  std::mutex mutex_;
  std::condition_variable released_;
};

}  // namespace crackdown
