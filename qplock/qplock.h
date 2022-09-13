#pragma once

#include <assert.h>

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>

#include "rdma_mcs_lock.h"

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

  struct alignas(128) Descriptor {
    RdmaMcsLock::Descriptor *remote_desc;
    uint8_t pad1[CACHELINE_SIZE - sizeof(uintptr_t)]; // bit confused about what type this pointer would be
    McsLock::Descriptor *local_desc;
    uint8_t pad2[CACHELINE_SIZE - sizeof(uintptr_t)]; // see above
  };
  static_assert(alignof(Descriptor) == 128);
  static_assert(sizeof(Descriptor) == 128);

  //Should the QP Lock take on the responsibility of setting up the rdma stuff or should it just be calling init of the RdmaMcsLock descriptor? if that is possible...
  QPLock(MemoryPool::Peer self, std::unique_ptr<MemoryPool::cm_type> cm);

  absl::Status Init(MemoryPool::Peer host,
                    const std::vector<MemoryPool::Peer> &peers);

  bool IsLocked();
  void Lock();
  void Unlock();

private:
  bool is_host_;
  
  volatile Descriptor *descriptor_;
};

} //namespace X