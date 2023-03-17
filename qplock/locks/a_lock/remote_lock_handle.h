#pragma once

#include <assert.h>

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>

#include "a_lock.h"

namespace X{

using ::rome::rdma::ConnectionManager;
using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;
using ::rome::rdma::RemoteObjectProto;

class RemoteLockHandle{
 public:
    using conn_type = MemoryPool::conn_type;

    RemoteLockHandle(MemoryPool::Peer self, MemoryPool &pool);
    
    absl::Status Init(remote_ptr<ALock> a_lock);
    void Lock();
    void Unlock();
    bool IsLocked();
    

 private:
    void Reacquire();
    void LockMcsQueue();
    bool IsLTailLocked();
    bool IsVictim();

    bool is_locked_;

    bool is_leader_; //! this might cause a data race!

    MemoryPool::Peer self_;
    MemoryPool &pool_;

    // Used for rdma writes
    remote_ptr<remote_ptr<RdmaDescriptor>> prealloc_;

    // These allow for fully manipulating the ALock
    remote_ptr<ALock> a_lock_;
    remote_ptr<remote_ptr<RdmaDescriptor>> r_tail_;
    remote_ptr<remote_ptr<RdmaDescriptor>> r_tail_next_ptr_;
    remote_ptr<remote_ptr<LocalDescriptor>> l_tail_;
    remote_ptr<uint64_t> victim_;

    remote_ptr<RdmaDescriptor> desc_pointer_;
    volatile RdmaDescriptor *descriptor_;

    
};

} //namespace X