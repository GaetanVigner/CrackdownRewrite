// Runs recompiled guest functions outside the game: a 4 GB guest address space whose pages are committed on first
// touch (Windows: a vectored exception handler; Linux: a lazily backed mapping) and the generated code's prelude
// (generated/default/crackdown_pch.h). Only functions that call nothing are run here (tests/cmake/guest_sandbox.cmake extracts them
// from the local generated/ tree at configure time; no guest code is kept in the repository).

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string_view>

// The generated code's own prelude (types, memory macros, VMX helpers), as the game build uses it.
#include "crackdown_pch.h"


#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <signal.h>
#include <sys/mman.h>
#endif

namespace guest_sandbox {

// The generated code adds 0x1000 to host addresses of the 0xE0000000+ physical views on Windows.
constexpr uint64_t kGuestSpace = 0x100000000ull + 0x1000;
constexpr uint32_t kPageSize = 4096;

class Memory {
 public:
  static Memory& Get() {
    static Memory memory;
    return memory;
  }
  uint8_t* base() const { return base_; }
  // Pages touched since the last Reset (committed on demand), as page-aligned guest addresses.
  const std::set<uint32_t>& touched() const { return touched_; }
  // Gives back every touched page, so the next access faults again and reads zeros.
  void Reset() {
    for (uint32_t page : touched_) {
#if defined(_WIN32)
      VirtualFree(base_ + page, kPageSize, MEM_DECOMMIT);
#else
      madvise(base_ + page, kPageSize, MADV_DONTNEED);
      mprotect(base_ + page, kPageSize, PROT_NONE);
#endif
    }
    touched_.clear();
  }
  void Fill(uint32_t address, uint32_t size, uint8_t value) { std::memset(base_ + address, value, size); }
  void Write32(uint32_t address, uint32_t value) {
    value = __builtin_bswap32(value);
    std::memcpy(base_ + address, &value, 4);
  }
  uint32_t Read32(uint32_t address) const {
    uint32_t value;
    std::memcpy(&value, base_ + address, 4);
    return __builtin_bswap32(value);
  }

 private:
  Memory() {
#if defined(_WIN32)
    base_ = static_cast<uint8_t*>(VirtualAlloc(nullptr, kGuestSpace, MEM_RESERVE, PAGE_NOACCESS));
    AddVectoredExceptionHandler(1, &Memory::Handler);
#else
    void* mapping = mmap(nullptr, kGuestSpace, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    base_ = mapping == MAP_FAILED ? nullptr : static_cast<uint8_t*>(mapping);
    struct sigaction action = {};
    action.sa_sigaction = &Memory::SignalHandler;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    sigaction(SIGSEGV, &action, nullptr);
#endif
    if (!base_ || (reinterpret_cast<uintptr_t>(base_) & 0x1F)) {
      std::printf("guest_sandbox: cannot reserve the guest address space\n");
      std::exit(2);
    }
  }
#if defined(_WIN32)
  static LONG CALLBACK Handler(EXCEPTION_POINTERS* info) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
        info->ExceptionRecord->NumberParameters < 2) {
      return EXCEPTION_CONTINUE_SEARCH;
    }
    Memory& memory = Get();
    uint8_t* address = reinterpret_cast<uint8_t*>(info->ExceptionRecord->ExceptionInformation[1]);
    if (address < memory.base_ || address >= memory.base_ + kGuestSpace) {
      return EXCEPTION_CONTINUE_SEARCH;
    }
    uint8_t* page = memory.base_ + ((address - memory.base_) & ~uint64_t(kPageSize - 1));
    if (!VirtualAlloc(page, kPageSize, MEM_COMMIT, PAGE_READWRITE)) {
      return EXCEPTION_CONTINUE_SEARCH;
    }
    memory.touched_.insert(uint32_t(page - memory.base_));
    return EXCEPTION_CONTINUE_EXECUTION;
  }
#else
  static void SignalHandler(int, siginfo_t* info, void*) {
    Memory& memory = Get();
    uint8_t* address = static_cast<uint8_t*>(info->si_addr);
    if (address < memory.base_ || address >= memory.base_ + kGuestSpace) {
      signal(SIGSEGV, SIG_DFL);  // not the sandbox's: crash as usual
      return;
    }
    uint8_t* page = memory.base_ + ((address - memory.base_) & ~uint64_t(kPageSize - 1));
    mprotect(page, kPageSize, PROT_READ | PROT_WRITE);
    memory.touched_.insert(uint32_t(page - memory.base_));
  }
#endif
  uint8_t* base_ = nullptr;
  std::set<uint32_t> touched_;
};

using GuestFunction = void (*)(PPCContext& __restrict ctx, uint8_t* base);
struct FunctionEntry {
  uint32_t address;
  GuestFunction function;
};

}  // namespace guest_sandbox
