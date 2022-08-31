#pragma once

#include <infiniband/verbs.h>

#include <cstdint>

#include "rome/rdma/channel/sync_accessor.h"
#include "rome/rdma/connection_manager/connection.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/rdma/rdma_memory.h"
#include "util.h"

namespace X {

class RdmaMcsLock {
public:
  using conn_type = MemoryPool::conn_type;

  struct alignas(128) Descriptor {
    long int budget{-1};
    uint8_t pad1[CACHELINE_SIZE - sizeof(budget)];
    remote_ptr<Descriptor> next{0};
    uint8_t pad2[CACHELINE_SIZE - sizeof(uintptr_t)];
  };
  static_assert(alignof(Descriptor) == 128);
  static_assert(sizeof(Descriptor) == 128);

  RdmaMcsLock(MemoryPool::Peer self, std::unique_ptr<MemoryPool::cm_type> cm);

  absl::Status Init(MemoryPool::Peer host,
                    const std::vector<MemoryPool::Peer> &peers);

  bool IsLocked();
  void Lock();
  void Unlock();

private:
  bool is_host_;
  MemoryPool::Peer self_;
  MemoryPool pool_;

  remote_ptr<remote_ptr<Descriptor>> lock_pointer_;
  remote_ptr<Descriptor> desc_pointer_;
  remote_ptr<remote_ptr<Descriptor>> prealloc_;

  volatile Descriptor *descriptor_;
};

} // namespace X
