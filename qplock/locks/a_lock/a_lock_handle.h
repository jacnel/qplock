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
#include "rome/rdma/memory_pool/remote_ptr.h"
#include "rome/rdma/rdma_memory.h"

#include "a_lock.h"

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

  absl::Status ALockInit(const std::vector<MemoryPool::Peer> &peers);
  
  absl::Status HandleInit(MemoryPool::Peer host,
                      const std::vector<MemoryPool::Peer> &peers);

  bool IsLocked();
  void Lock();
  void Unlock();

private:
  bool inline IsLocal();
  bool IsVictim(uint32_t victim_id);
  bool IsLTailLocked();
  bool IsRTailLocked();
  void Reacquire();
  void LockRemoteMcsQueue();
  void LockLocalMcsQueue();
 
  static constexpr int DESCS_PER_LOCK = 10;
  bool is_host_;
  bool is_r_leader_;
  bool is_l_leader_;
  
  MemoryPool::Peer self_;
  MemoryPool &pool_; //reference to pool object, so all descriptors in same pool

  //Pointer to alock to allow it to be read/write via rdma
  remote_ptr<ALock> a_lock_pointer_;
  volatile ALock *a_lock_;

  // TODO: NOT SURE ITS NECESSARY TO STORE ALL THIS STUFF SINCE WE KNOW THE OFFSETS....
  // Access to fields remotely
  remote_ptr<remote_ptr<RdmaDescriptor>> r_tail_;
  remote_ptr<remote_ptr<LocalDescriptor>> r_l_tail_;
  remote_ptr<uint64_t> r_victim_;

  // Access to fields locally
  std::atomic<LocalDescriptor*> l_l_tail_;
  std::atomic<uint64_t*> l_victim_;
  
  // Prealloc used for rdma writes
  remote_ptr<ALock> prealloc_;

  // Pointers to pre-allocated descriptor to be used locally
  remote_ptr<LocalDescriptor> l_desc_pointer_;
  LocalDescriptor* l_desc_; // static thread_local causes undefined ref error

  // Pointers to pre-allocated descriptor to be used remotely

  remote_ptr<RdmaDescriptor> r_desc_pointer_;
  volatile RdmaDescriptor *r_desc_;

  
};

} // namespace X
