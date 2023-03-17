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

//This class is intended to only be used by a process that is local to the ALock. 
class LockHandle{
 public:
    using conn_type = MemoryPool::conn_type;

    LockHandle(MemoryPool::Peer self, MemoryPool &pool);
    
    absl::Status Init(remote_ptr<ALock> a_lock);
    void Lock();
    void Unlock();
    bool IsLocked();
    

 private:
    void Reacquire();
    void LockMcsQueue();
    bool IsRTailLocked();

    MemoryPool::Peer self_;
    MemoryPool &pool_;

    bool is_locked_;
    bool is_leader_;

    // These pointers allow for accessing different fields of the ALock
   ALock *a_lock_;
   remote_ptr<remote_ptr<RdmaDescriptor>> r_tail_;
   std::atomic<LocalDescriptor*> l_tail_;
   std::atomic<uint64_t*> victim_;

   remote_ptr<LocalDescriptor> desc_pointer_;
   // static thread_local causes undefined ref error
   LocalDescriptor* local_desc_;
};

} //namespace X