#include "remote_lock_handle.h"

namespace X {

using ::rome::rdma::ConnectionManager;
using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;
using ::rome::rdma::RemoteObjectProto;


RemoteLockHandle::RemoteLockHandle(MemoryPool::Peer self, MemoryPool &pool)
     : self_(self), pool_(pool), is_locked_(false), is_leader_(false) {}

absl::Status RemoteLockHandle::Init(remote_ptr<ALock> a_lock){
    a_lock_ = decltype(a_lock_)(a_lock.id(), a_lock.address());
    r_tail_ = decltype(r_tail_)(a_lock.id(), a_lock.address());
    l_tail_ = decltype(l_tail_)(a_lock.id(), a_lock.address() + 16);
    victim_ = decltype(victim_)(a_lock.id(), a_lock.address() + 32);   

    // ! ALLOCATING MEMORY AT RUN TIME IS A BAD IDEA!!
    // allocate memory for a RdmaDescriptor in mempool
    desc_pointer_ = pool_.Allocate<RdmaDescriptor>();
    descriptor_ = reinterpret_cast<RdmaDescriptor *>(desc_pointer_.address());
    ROME_DEBUG("RdmaDescriptor @ {:x}", static_cast<uint64_t>(desc_pointer_));

    return absl::OkStatus();
}

bool RemoteLockHandle::IsLocked(){
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

bool RemoteLockHandle::IsLTailLocked(){
     // read in value of current l_tail on host
    auto remote = pool_.Read<remote_ptr<LocalDescriptor>>(l_tail_);
    // store result of if its locked
    auto locked = static_cast<uint64_t>(*(std::to_address(remote))) != 0;
    // deallocate the ptr used as a landing spot for reading in (which is created in Read)
    auto ptr =
        remote_ptr<remote_ptr<LocalDescriptor>>{self_.id, std::to_address(remote)};
    pool_.Deallocate(ptr);
    return locked;
}

bool RemoteLockHandle::IsVictim() {
    // read in value of victim var on host
    auto remote = pool_.Read<uint64_t>(victim_);
    // store result of if its locked
    auto check_victim = static_cast<uint64_t>(*(std::to_address(remote))) == REMOTE_VICTIM;
    // deallocate the ptr used as a landing spot for reading in (which is created in Read)
    auto ptr =
        remote_ptr<uint64_t>{self_.id, std::to_address(remote)};
    pool_.Deallocate(ptr);
    return check_victim;
}

void RemoteLockHandle::LockMcsQueue(){
    ROME_DEBUG("Locking remote MCS queue...");
  
    // Set local RdmaDescriptor to initial values
    descriptor_->budget = -1;
    descriptor_->next = remote_nullptr;

    // swap local RdmaDescriptor in at the address of the hosts lock pointer
    auto prev =
      pool_.AtomicSwap(r_tail_, static_cast<uint64_t>(desc_pointer_));
    
    if (prev != remote_nullptr) { //someone else has the lock
        auto temp_ptr = remote_ptr<uint8_t>(prev);
        temp_ptr +=32; //temp_ptr = next field of the current tail's RdmaDescriptor
        // make prev point to the current tail RdmaDescriptor's next pointer
        prev = remote_ptr<RdmaDescriptor>(temp_ptr);
        // set the address of the current tail's next field = to the addr of our local RdmaDescriptor
        pool_.Write<remote_ptr<RdmaDescriptor>>(
            static_cast<remote_ptr<remote_ptr<RdmaDescriptor>>>(prev), desc_pointer_,
            prealloc_);
        ROME_DEBUG("[Lock] Enqueued: {} --> (id={})",
                static_cast<uint64_t>(prev.id()),
                static_cast<uint64_t>(desc_pointer_.id()));
        // spins locally, waits for current tail/lockholder to write to budget when it unlocks
        while (descriptor_->budget < 0) {
            cpu_relax();
        }
        if (descriptor_->budget == 0) {
            ROME_DEBUG("Budget exhausted (id={})",
                        static_cast<uint64_t>(desc_pointer_.id()));
            // Release the lock before trying to continue
            Reacquire();
            descriptor_->budget = kInitBudget;
        }
    } else { //no one had the lock, we were swapped in
        // set lock holders RdmaDescriptor budget to initBudget since we are the first lockholder
        descriptor_->budget = kInitBudget;
        is_leader_ = true;
    }
    // budget was set to greater than 0, CS can be entered
    ROME_DEBUG("[Lock] Acquired: prev={:x}, budget={:x} (id={})",
                static_cast<uint64_t>(prev), descriptor_->budget,
                static_cast<uint64_t>(desc_pointer_.id()));

    //  make sure Lock operation finished
    std::atomic_thread_fence(std::memory_order_acquire);
}

void RemoteLockHandle::Lock(){
    LockMcsQueue();
    if (is_leader_){
        auto prev = pool_.AtomicSwap(victim_, static_cast<uint8_t>(REMOTE_VICTIM));
        //! this is remotely spinning on the victim var?? --> but competition is only among two at this point
        while (IsVictim(REMOTE_VICTIM) && IsLTailLocked()){
            cpu_relax();
        } 
    }
}

void RemoteLockHandle::Unlock(){
    // Make sure everything finished before unlocking
    std::atomic_thread_fence(std::memory_order_release);
    // if r_tail_ == my desc (we are the tail), set it to 0 to unlock
    // otherwise, someone else is contending for lock and we want to give it to them
    // try to swap in a 0 to unlock the RdmaDescriptor at the addr of the remote tail, which we expect to currently be equal to our RdmaDescriptor
    auto prev = pool_.CompareAndSwap(r_tail_,
                                    static_cast<uint64_t>(desc_pointer_), 0);
   
    //TODO: follow all next pointers and print the whole queue

    // if the descriptor at r_tail_ was not our RdmaDescriptor (other clients have attempted to lock & enqueued since)
    if (prev != desc_pointer_) {  
        // attempt to hand the lock to prev

        // make sure next pointer gets set before continuing
        while (descriptor_->next == remote_nullptr)
        ;
        std::atomic_thread_fence(std::memory_order_acquire);

        // gets a pointer to the next RdmaDescriptor object
        auto next = const_cast<remote_ptr<RdmaDescriptor> &>(descriptor_->next);
        //writes to the the next descriptors budget which lets it know it has the lock now
        pool_.Write<uint64_t>(static_cast<remote_ptr<uint64_t>>(next),
                            descriptor_->budget - 1,
                            static_cast<remote_ptr<uint64_t>>(prealloc_));
    } 
    //else: successful CAS, we unlocked our RdmaDescriptor and no one is queued after us
    is_locked_ = false;
    ROME_DEBUG("[Unlock] Unlocked (id={})",
                static_cast<uint64_t>(desc_pointer_.id()));
}

void RemoteLockHandle::Reacquire() {
    // set victim to me (remote victim) to yield lock to waiting process
    auto prev = pool_.AtomicSwap(victim_, static_cast<uint64_t>(REMOTE_VICTIM));
    // wait to reacquire
    while (true){
        // If the local cohort is not locked OR...
        if (!IsLTailLocked()){
            break;
        }
        // ...if I am no longer the victim
        if(!IsVictim(REMOTE_VICTIM)){
            break;
        }
        // wait a hot sec before checking again
        cpu_relax();
    } 
    //  make sure Reacquire operation finished
    std::atomic_thread_fence(std::memory_order_acquire);
}

} //namespace X