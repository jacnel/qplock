#pragma once

#include <infiniband/verbs.h>

#include <cstdint>

#include "rome/rdma/channel/sync_accessor.h"
#include "rome/rdma/connection_manager/connection.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/rdma/rdma_memory.h"
#include "util.h"
#include "remote_lock_handle.h"
#include "lock_handle.h"
#include "common.h"

namespace X {

using ::rome::rdma::ConnectionManager;
using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;
using ::rome::rdma::RemoteObjectProto;

class ALockHandle{
public:
  using conn_type = MemoryPool::conn_type;

  ALockHandle(MemoryPool::Peer self, MemoryPool& pool);

  absl::Status Init(MemoryPool::Peer host,
                      const std::vector<MemoryPool::Peer> &peers);

  bool IsLocked();
  void Lock();
  void Unlock();

private:
  bool inline IsLocal();
  bool is_host_;
  
  MemoryPool::Peer self_;
  MemoryPool &pool_; //reference to pool object, so all descriptors in same pool

  std::unique_ptr<RemoteLockHandle> r_handle_;
  std::unique_ptr<LockHandle> l_handle_;
  
  // Used for rdma writes
  remote_ptr<ALock> prealloc_;

  // // Points to first lock on the host
  // remote_ptr<remote_ptr<ALock>> lock_pointer_;

  //Pointer to desc to allow it to be read/write via rdma
  remote_ptr<ALock> a_lock_pointer_;
  volatile ALock *a_lock_;
};

} // namespace X
