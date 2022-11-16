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

  // TODO: may want to rearrange how this is aligned
  struct alignas(64) AsyncLock {
    // pointer to the local tail
    McsLock *l_tail; // TODO: should this also just be the descriptor?
    // pointer to the remote tail
    remote_ptr<RdmaMcsLock> r_tail; //TODO: this should really just be the descriptor since we swap in a descriptor...
    uint8_t pad1[32 - sizeof(uintptr_t)]; // pad so victim starts at addr+32
    // node id of the victim
    uint16_t victim; 
    // pad to fill cacheline
    uint8_t pad2[CACHELINE_SIZE - sizeof(uint16_t) - (2*sizeof(uintptr_t))];
  };
  static_assert(alignof(AsyncLock) == 64);
  static_assert(sizeof(AsyncLock) == 64);
  // alignas(32) McsLock *l_tail;
  // alignas(16) remote_ptr<RdmaMcsLock> r_tail;
  // alignas(16) uint16_t victim;

  QPLock(MemoryPool::Peer self, MemoryPool& pool);

  absl::Status Init(MemoryPool::Peer host,
                    const std::vector<MemoryPool::Peer> &peers);

  void Lock();
  void Unlock();
  void Reacquire();

private:
  // bool is_host_; qp lock is always on the host?
  MemoryPool::Peer self_;
  MemoryPool &pool_;
  
  // address shared with other nodes in the system (remotely acessible address to the remotely accessible async lock)
  remote_ptr<remote_ptr<AsyncLock>> lock_pointer_;

  //Pointer to desc to allow it to be read/write via rdma
  remote_ptr<AsyncLock> async_pointer_;
  volatile AsyncLock *lock_;
};

} //namespace X