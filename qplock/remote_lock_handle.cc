#include "remote_lock_handle.h"

#include <assert.h>

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>

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
    ROME_DEBUG("Remote Handle A Lock pointer {:x}", static_cast<uint64_t>(a_lock_));
    r_tail_ = decltype(r_tail_)(a_lock.id(), a_lock.address());
    // TODO: THIS MUST BE WRONG
    l_tail_ = decltype(l_tail_)(a_lock.id(), a_lock.address() + 16); //!Should make sure this is accurate way to point to descriptors
    ROME_DEBUG("LTAIL PTR: (+16) {}  ",  l_tail_ );
    victim_ = decltype(victim_)(a_lock.id(), a_lock.address() + 32);   

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
    //  TODO: below line is where we get the local protection issue (reading the wrong address)
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


//TODO: need to figure put a clever way to return whether or not we are the leader
void RemoteLockHandle::LockMcsQueue(){
    ROME_DEBUG("Locking remote MCS queue...");
  
    // Set local RdmaDescriptor to initial values
    descriptor_->budget = -1;
    descriptor_->next = remote_nullptr;

    //TODO: global lock
    // glock.lock();
    // swap local RdmaDescriptor in at the address of the hosts lock pointer
    auto prev =
      pool_.AtomicSwap(r_tail_, static_cast<uint64_t>(desc_pointer_));
    
    if (prev != remote_nullptr) { //someone else has the lock
        auto temp_ptr = remote_ptr<uint8_t>(prev);
        // TODO: MAKE SURE THIS IS CORRECT
        temp_ptr +=32; //temp_ptr = next field of the current tail's RdmaDescriptor
        ROME_DEBUG("Prev RdmaDescriptor at Tail: prev={:x}, temp_ptr={:x}",
                static_cast<uint64_t>(prev), static_cast<uint64_t>(temp_ptr));
        // make prev point to the current tail RdmaDescriptor's next pointer
        prev = remote_ptr<RdmaDescriptor>(temp_ptr);
        // set the address of the current tail's next field = to the addr of our local RdmaDescriptor
        pool_.Write<remote_ptr<RdmaDescriptor>>(
            static_cast<remote_ptr<remote_ptr<RdmaDescriptor>>>(prev), desc_pointer_,
            prealloc_);
        //THis glock will tell us if we have a race condition between swapping the rtail and the writing the next
        // glock.unlock();
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
    //  while (**victim_== REMOTE_VICTIM && IsLTailLocked()){
        while (IsVictim() && IsLTailLocked()){
            cpu_relax();
        } 
    }
}

void RemoteLockHandle::Unlock(){
    // Make sure everything finished before unlocking
    //! LMAO THIS IS FUCKING DUMB
    std::atomic_thread_fence(std::memory_order_release);
    // if r_tail_ == my desc (we are the tail), set it to 0 to unlock
    // otherwise, someone else is contending for lock and we want to give it to them
    // try to swap in a 0 to unlock the RdmaDescriptor at the addr of the remote tail, which we expect to currently be equal to our RdmaDescriptor
    auto prev = pool_.CompareAndSwap(r_tail_,
                                    static_cast<uint64_t>(desc_pointer_), 0);
    // TODO: VERIFY THAT RTAIL IS NULL HERE
    //follow all next pointers and print the whole queue

    if (prev != desc_pointer_) {  // if the lock at r_tail_ was not equal to our RdmaDescriptor (new people have enqueued since)
        // attempt to hand the lock to prev
        // spin while no one is set to go next
        while (descriptor_->next == remote_nullptr){
            ROME_DEBUG("I AM STUCK");
            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        // gets a pointer to the next RdmaDescriptor object
        auto next = const_cast<remote_ptr<RdmaDescriptor> &>(descriptor_->next);
        //writes to the the next descriptors budget which lets it know it has the lock now
        pool_.Write<uint64_t>(static_cast<remote_ptr<uint64_t>>(next),
                            descriptor_->budget - 1,
                            static_cast<remote_ptr<uint64_t>>(prealloc_));
    } 
    //else: successful CAS, we unlocked our RdmaDescriptor and no one is after us
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
        if(!IsVictim()){
            break;
        }
        // wait a hot sec before checking again
        cpu_relax();
    } 
    //  make sure Reacquire operation finished
    std::atomic_thread_fence(std::memory_order_acquire);
}

} //namespace X