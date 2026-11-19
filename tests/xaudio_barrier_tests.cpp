#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <time.h>
#endif

#include "check.h"
#include "xaudio_barrier.h"

using crackdown::XAudioBarrier;

namespace {

constexpr uint64_t kProcessors45 = (uint64_t(1) << 4) | (uint64_t(1) << 5);

void TestLastArrivalReleases() {
  XAudioBarrier barrier;
  const XAudioBarrier::Arrival first = barrier.Arrive(4, kProcessors45);
  CHECK(!first.released_round && first.round == 0);
  CHECK(barrier.RoundOpen(0));
  const XAudioBarrier::Arrival second = barrier.Arrive(5, kProcessors45);
  CHECK(second.released_round && second.round == 0);
  CHECK(!barrier.RoundOpen(0));
  CHECK(barrier.RoundOpen(1));
}

// A worker that arrives again before its partner releases the round stays in that round.
void TestRepeatedArrivalKeepsRound() {
  XAudioBarrier barrier;
  barrier.Arrive(4, kProcessors45);
  const XAudioBarrier::Arrival again = barrier.Arrive(4, kProcessors45);
  CHECK(!again.released_round && again.round == 0);
  CHECK(barrier.Arrive(5, kProcessors45).released_round);
}

// The case the guest's byte mask loses: the late worker arrives after the round was released and must not be left
// waiting in it.
void TestLateWorkerNeverWaitsOnReleasedRound() {
  XAudioBarrier barrier;
  const XAudioBarrier::Arrival early = barrier.Arrive(4, kProcessors45);
  barrier.Arrive(5, kProcessors45);
  CHECK(!barrier.RoundOpen(early.round));
  const XAudioBarrier::Arrival next = barrier.Arrive(4, kProcessors45);
  CHECK(!next.released_round && next.round == 1);
}

void TestSingleParticipantReleasesAtOnce() {
  XAudioBarrier barrier;
  CHECK(barrier.Arrive(2, uint64_t(1) << 2).released_round);
  CHECK(barrier.Arrive(2, uint64_t(1) << 2).released_round);
  CHECK(barrier.RoundOpen(2));
}

void TestTwoThreadsManyRounds() {
  constexpr int kRounds = 20000;
  XAudioBarrier barrier;
  std::atomic<int> completed[2] = {0, 0};
  auto worker = [&](uint32_t processor, int index) {
    for (int round = 0; round < kRounds; ++round) {
      const XAudioBarrier::Arrival arrival = barrier.Arrive(processor, kProcessors45);
      while (!arrival.released_round && barrier.RoundOpen(arrival.round)) {
        std::this_thread::yield();
      }
      completed[index].fetch_add(1, std::memory_order_relaxed);
    }
  };
  std::thread a(worker, 4, 0);
  std::thread b(worker, 5, 1);
  a.join();
  b.join();
  CHECK(completed[0] == kRounds && completed[1] == kRounds);
  CHECK(barrier.RoundOpen(kRounds));
}

// The calling thread's processor time.
std::chrono::nanoseconds ThreadCpuTime() {
#if defined(_WIN32)
  FILETIME created, exited, kernel, user;
  GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user);
  const auto ticks = [](const FILETIME& f) { return (uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
  return std::chrono::nanoseconds((ticks(kernel) + ticks(user)) * 100);
#else
  timespec ts{};
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
  return std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec);
#endif
}

// The workers as the hook runs them (Wait): every round released for both, none lost, over many rounds, with one
// worker now and then far behind (it sleeps), so the other goes past the spin and sleeps too.
void TestWaitManyRounds() {
  constexpr int kRounds = 20000;
  XAudioBarrier barrier;
  std::atomic<int> completed[2] = {0, 0};
  auto worker = [&](uint32_t processor, int index) {
    for (int round = 0; round < kRounds; ++round) {
      if (index == 1 && round % 1000 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      const XAudioBarrier::Arrival arrival = barrier.Arrive(processor, kProcessors45);
      if (!arrival.released_round) barrier.Wait(arrival.round, [] { return false; });
      completed[index].fetch_add(1, std::memory_order_relaxed);
    }
  };
  std::thread a(worker, 4, 0);
  std::thread b(worker, 5, 1);
  a.join();
  b.join();
  CHECK(completed[0] == kRounds && completed[1] == kRounds);
  CHECK(barrier.RoundOpen(kRounds));
}

// A worker whose partner is 100 ms late sleeps: its processor time stays a small part of the wait (it spun for all of
// it before). It wakes as the partner arrives.
void TestWaiterSleeps() {
  XAudioBarrier barrier;
  std::chrono::nanoseconds cpu{0};
  std::chrono::steady_clock::time_point woke;
  std::thread waiter([&] {
    const auto c0 = ThreadCpuTime();
    const XAudioBarrier::Arrival arrival = barrier.Arrive(4, kProcessors45);
    barrier.Wait(arrival.round, [] { return false; });
    woke = std::chrono::steady_clock::now();
    cpu = ThreadCpuTime() - c0;
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  const auto released = std::chrono::steady_clock::now();
  CHECK(barrier.Arrive(5, kProcessors45).released_round);
  waiter.join();
  const double late_ms = std::chrono::duration<double, std::milli>(woke - released).count();
  CHECK(cpu < std::chrono::milliseconds(20));
  CHECK(late_ms < 50);
  std::printf("xaudio_barrier_tests: a 100 ms wait used %.2f ms of processor time; woken %.3f ms after the release\n",
              std::chrono::duration<double, std::milli>(cpu).count(), late_ms);
}

// A worker whose partner never comes leaves when `escape` says so (the title terminating), within a look or two.
void TestEscapeEndsWait() {
  XAudioBarrier barrier;
  std::atomic<bool> terminating{false};
  std::chrono::steady_clock::time_point left;
  std::thread waiter([&] {
    const XAudioBarrier::Arrival arrival = barrier.Arrive(4, kProcessors45);
    barrier.Wait(arrival.round, [&] { return terminating.load(); });
    left = std::chrono::steady_clock::now();
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  const auto told = std::chrono::steady_clock::now();
  terminating = true;
  waiter.join();
  CHECK(barrier.RoundOpen(0));  // still open: it left without a release
  CHECK(left - told < 3 * XAudioBarrier::kEscapeLook);
}

}  // namespace

int main() {
  TestLastArrivalReleases();
  TestRepeatedArrivalKeepsRound();
  TestLateWorkerNeverWaitsOnReleasedRound();
  TestSingleParticipantReleasesAtOnce();
  TestTwoThreadsManyRounds();
  TestWaitManyRounds();
  TestWaiterSleeps();
  TestEscapeEndsWait();
  return check::Finish("xaudio_barrier_tests");
}
