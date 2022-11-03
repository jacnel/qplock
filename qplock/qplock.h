#pragma once

#include <assert.h>

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>

#include "rdma_mcs_lock.h"
#include "mcs_lock.h"

#include "util.h"

namespace X {

using ::rome::rdma::ConnectionManager;
using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;
using ::rome::rdma::RemoteObjectProto;


class QPLock {
public:
  using conn_type = MemoryPool::conn_type;

  struct alignas(128) AsyncLock {
    // pointer to the remote tail
    remote_ptr<RdmaMcsLock> remote_q;
    // pointer to the local tail
    McsLock *local_q;
    // pointers share first cache line, victim var on next
    uint8_t pad2[CACHELINE_SIZE - sizeof(uintptr_t) - sizeof(uintptr_t)];
    // node id of the victim
    uint16_t victim; 
    uint8_t pad3[CACHELINE_SIZE - sizeof(uint16_t)];
  };
  static_assert(alignof(AsyncLock) == 128);
  static_assert(sizeof(AsyncLock) == 128);

  QPLock(MemoryPool::Peer self, MemoryPool& pool);

  absl::Status Init(MemoryPool::Peer host,
                    const std::vector<MemoryPool::Peer> &peers);

  void Lock();
  void Unlock();
  void Reacquire();

private:
  bool is_host_;
  MemoryPool::Peer self_;
  MemoryPool &pool_;
  
  //Pointer to desc to allow it to be read/write via rdma
  remote_ptr<AsyncLock> lock_pointer_;
  volatile AsyncLock *lock_;
};

} //namespace X