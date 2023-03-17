#include "lock_handle.h"

namespace X {

using ::rome::rdma::ConnectionManager;
using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;
using ::rome::rdma::RemoteObjectProto;


LockHandle::LockHandle(MemoryPool::Peer self, MemoryPool &pool)
    : self_(self), pool_(pool), is_locked_(false), is_leader_(false) {}

absl::Status LockHandle::Init(remote_ptr<ALock> a_lock){
    ROME_DEBUG("Local Handle ALock @ {:x}", static_cast<uint64_t>(a_lock));
    a_lock_ = reinterpret_cast<ALock *>(a_lock.address());
    r_tail_ = decltype(r_tail_)(a_lock.id(), a_lock.address());
    l_tail_ = reinterpret_cast<LocalDescriptor*>(a_lock.address() + 16);
    victim_ = reinterpret_cast<uint64_t*>(a_lock.address() + 32);

    // allocate memory for a LocalDescriptor in mempool
    desc_pointer_ = pool_.Allocate<LocalDescriptor>();
    local_desc_ = reinterpret_cast<LocalDescriptor *>(desc_pointer_.address());
    ROME_DEBUG("LocalDescriptor @ {:x}", static_cast<uint64_t>(desc_pointer_));
    return absl::OkStatus();
}

bool LockHandle::IsLocked(){
    //since we are the host, get the local addr and just interpret the value
    return l_tail_.load(std::memory_order_acquire) != 0;
}

bool LockHandle::IsRTailLocked(){
    // read in value of current r_tail on host
    auto remote = pool_.Read<remote_ptr<RdmaDescriptor>>(r_tail_);
    // store result of if its locked
    auto locked = static_cast<uint64_t>(*(std::to_address(remote))) != 0;
    // deallocate the ptr used as a landing spot for reading in (which is created in Read)
    auto ptr =
        remote_ptr<remote_ptr<RdmaDescriptor>>{self_.id, std::to_address(remote)};
    pool_.Deallocate(ptr);
    return locked;
}

//TODO: need to figure put a clever way to return whether or not we are the leader
void LockHandle::LockMcsQueue(){
    ROME_DEBUG("LOCKING LOCAL MCS QUEUE...");
    // to acquire the lock a thread atomically appends its own local node at the
    // tail of the list returning tail's previous contents
    // ROME_DEBUG("l_tail_ @ {:x}", (&l_tail_));
    auto prior_node = l_tail_.exchange(local_desc_, std::memory_order_acquire);
    if (prior_node != nullptr) {
        local_desc_->budget = -1;
        // if the list was not previously empty, it sets the predecessor’s next
        // field to refer to its own local node
        prior_node->next = local_desc_;
        // thread then spins on its local locked field, waiting until its
        // predecessor sets this field to false
        //!STUCK IN THIS LOOP!!!!!!!! 
        while (local_desc_->budget < 0) {
            cpu_relax();
            ROME_DEBUG("WAITING IN HERE...");
        }

        // If budget exceeded, then reinitialize.
        if (local_desc_->budget == 0) {
            local_desc_->budget = kInitBudget;
        }
    }
    // now first in the queue, own the lock and enter the critical section...
    is_leader_ = true;
}

void LockHandle::Lock(){
    ROME_DEBUG("LocalHandle::LOCK()");
    ROME_ASSERT(!is_locked_, "Lock has already been called on LockHandle.");
    is_locked_ = true;
    LockMcsQueue();
    if (is_leader_){
        auto prev = victim_.exchange(LOCAL_VICTIM, std::memory_order_acquire);
        while (*victim_== LOCAL_VICTIM && IsRTailLocked()){
            cpu_relax();
        } 
    }
    std::atomic_thread_fence(std::memory_order_release);
}

void LockHandle::Unlock(){
    ROME_DEBUG("LocalHandle::UNLOCK()");
    std::atomic_thread_fence(std::memory_order_release);
    //...leave the critical section
    // check whether this thread's local node’s next field is null
    if (local_desc_->next == nullptr) {
        // if so, then either:
        //  1. no other thread is contending for the lock
        //  2. there is a race condition with another thread about to
        // to distinguish between these cases atomic compare exchange the tail field
        // if the call succeeds, then no other thread is trying to acquire the lock,
        // tail is set to nullptr, and unlock() returns
        LocalDescriptor* p = local_desc_;
        if (l_tail_.compare_exchange_strong(p, nullptr, std::memory_order_release,
                                        std::memory_order_relaxed)) {
            return;
        }
        // otherwise, another thread is in the process of trying to acquire the
        // lock, so spins waiting for it to finish
        while (local_desc_->next == nullptr) {
        };
    }
    // in either case, once the successor has appeared, the unlock() method sets
    // its successor’s locked field to false, indicating that the lock is now free
    local_desc_->next->budget = local_desc_->budget - 1;
    //! NOT sure if this is reasonable
    is_leader_ = false;
    is_locked_ = false;
    // at this point no other thread can access this node and it can be reused
    local_desc_->next = nullptr;
}

void LockHandle::Reacquire(){
    ROME_DEBUG("LocalHandle::REACQUIRE()");
    //atomically swap our node_id into the victim_ field
    auto prev = victim_.exchange(LOCAL_VICTIM, std::memory_order_acquire);
    while (*victim_== LOCAL_VICTIM && IsRTailLocked()){
            cpu_relax();
    } 
}

} //namespace X