#pragma once

#include <infiniband/verbs.h>

#include <assert.h>

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>

#include "../../util.h"
#include "rome/util/thread_util.h"

namespace X {

class McsLock {
 public:
  struct Descriptor {
    int8_t budget{-1}; //budget == -1 indicates its locked, unlocked and passed off when it can proceed to critical section
    uint8_t pad1[CACHELINE_SIZE - sizeof(budget)];
    Descriptor* next{nullptr};
    uint8_t pad2[CACHELINE_SIZE - sizeof(next)];
  };
  static_assert(sizeof(Descriptor) == 2 * CACHELINE_SIZE, "");

  void Lock();
  void Unlock();
  bool IsLocked();

 private:
  static constexpr uint32_t kInitBudget = 5;
  std::atomic<Descriptor*> tail_{nullptr};
  static thread_local Descriptor local_desc_;
};

}  // namespace X
