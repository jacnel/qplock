#pragma once

#include <infiniband/verbs.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

#include "rome/rdma/channel/sync_accessor.h"
#include "rome/rdma/connection_manager/connection.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/rdma/rdma_memory.h"
#include "../../util.h"

namespace X {

using ::rome::rdma::ConnectionManager;
using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;
using ::rome::rdma::RemoteObjectProto;

class RdmaSpinLock {
public:
  using conn_type = MemoryPool::conn_type;

  RdmaSpinLock(MemoryPool::Peer self, MemoryPool &pool);

  absl::Status Init(MemoryPool::Peer host,
                    const std::vector<MemoryPool::Peer> &peers);

  bool IsLocked();
  void Lock();
  void Unlock();

private:
  static constexpr uint64_t kUnlocked = 0;
  bool is_host_;

  MemoryPool::Peer self_;
  MemoryPool &pool_;

  remote_ptr<uint64_t> lock_;
  remote_ptr<uint64_t> local_;
};

} // namespace X