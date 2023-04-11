#include "a_lock_handle.h"

namespace X {

using ::rome::rdma::ConnectionManager;
using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;
using ::rome::rdma::RemoteObjectProto;

ALockHandle::ALockHandle(MemoryPool::Peer self, MemoryPool &pool)
    : self_(self), pool_(pool) {}

// TODO: MOVE THIS CODE TO SERVER? WE WANT TO CONTROL HOW MANY WE ARE CREATING.....
absl::Status ALockHandle::ALockInit(const std::vector<MemoryPool::Peer> &peers){
  // TODO: Currently at 16 MB and runs OOM, but other locks dont even need this much
  // Allocate pool for ALocks
  auto capacity = 1 << 24;
  auto status = pool_.Init(capacity, peers);
  // Send all peers the base address of the lock residing on the host
  RemoteObjectProto proto;
  a_lock_pointer_ = pool_.Allocate<ALock>();
  proto.set_raddr(a_lock_pointer_.address());
  // tell all the peers where to find the addr of the first lock
  for (const auto &p : peers) {
    auto conn_or = pool_.connection_manager()->GetConnection(p.id);
    ROME_CHECK_OK(ROME_RETURN(conn_or.status()), conn_or);
    status = conn_or.value()->channel()->Send(proto);
    ROME_CHECK_OK(ROME_RETURN(status), status);
  }

  ROME_DEBUG("Lock pointer {:x}", static_cast<uint64_t>(a_lock_pointer_));
  return absl::OkStatus();
}

absl::Status ALockHandle::Init(MemoryPool::Peer host,
                               const std::vector<MemoryPool::Peer> &peers) {
  // TODO: Currently at 16 MB and runs OOM, but other locks dont even need this much
  auto capacity = 1 << 30;
  // Allocate pool for Remote Descriptors
  auto status = pool_.Init(capacity, peers);
  // ROME_ASSERT_OK(status);
  ROME_CHECK_OK(ROME_RETURN(status), status);
 
  // allocate local and remote descriptors for client to use
  AllocateClientDescriptors();

  //Get base address of alock mem pool from each server role

  // Otherwise, wait until the base address is shared by the host
  auto conn_or = pool_.connection_manager()->GetConnection(host.id);
  ROME_CHECK_OK(ROME_RETURN(conn_or.status()), conn_or);
  auto got = conn_or.value()->channel()->TryDeliver<RemoteObjectProto>();
  while (got.status().code() == absl::StatusCode::kUnavailable) {
    got = conn_or.value()->channel()->TryDeliver<RemoteObjectProto>();
  }
  ROME_CHECK_OK(ROME_RETURN(got.status()), got);
  // set lock pointer to the base address of the lock on the host
  a_lock_pointer_ = decltype(a_lock_pointer_)(host.id, got->raddr());

  //Used as preallocated memory for RDMA writes
  prealloc_ = pool_.Allocate<remote_ptr<RdmaDescriptor>>();
  
  ROME_DEBUG("Lock pointer {:x}", static_cast<uint64_t>(a_lock_pointer_));
  return absl::OkStatus();
}

void ALockHandle::AllocateClientDescriptors(){
  // Pointer to first remote descriptor
  r_desc_pointer_ = pool_.Allocate<RdmaDescriptor>();
  r_desc_ = reinterpret_cast<RdmaDescriptor *>(r_desc_pointer_.address());
  ROME_INFO("First RdmaDescriptor @ {:x}", static_cast<uint64_t>(r_desc_pointer_));
  for (int i = 0; i < DESCS_PER_CLIENT; i++){
    auto temp = pool_.Allocate<RdmaDescriptor>();
    ROME_DEBUG("RdmaDescriptor @ {:x}", static_cast<uint64_t>(temp));
  }
  
  // Make sure all rdma descriptors are allocated first in contiguous memory
  std::atomic_thread_fence(std::memory_order_release);

  // Pointer to first local descriptor
  l_desc_pointer_ = pool_.Allocate<LocalDescriptor>();
  l_desc_ = reinterpret_cast<LocalDescriptor *>(l_desc_pointer_.address());
  ROME_DEBUG("First LocalDescriptor @ {:x}", static_cast<uint64_t>(l_desc_pointer_));
  for (int i = 0; i < DESCS_PER_CLIENT; i++){
    auto temp = pool_.Allocate<LocalDescriptor>();
    ROME_DEBUG("LocalDescriptor @ {:x}", static_cast<uint64_t>(temp));
  }
}

bool ALockHandle::IsLocked() {
  // TODO: figure this out, not used rn
  // return a_lock_->locked; 
  return false;
}

// may need to change to take in an addr for when we expand to multiple locks
bool inline ALockHandle::IsLocal(){
  return ((a_lock_pointer_).id() == self_.id);
}

bool ALockHandle::IsRTailLocked(){
    // read in value of current r_tail on host
    auto remote = pool_.Read<remote_ptr<RdmaDescriptor>>(r_tail_);
    // store result of if its locked (0 = unlocked)
    auto locked = static_cast<uint64_t>(*(std::to_address(remote))) != 0;
    // deallocate the ptr used as a landing spot for reading in (which is created in Read)
    auto ptr =
        remote_ptr<remote_ptr<RdmaDescriptor>>{self_.id, std::to_address(remote)};
    pool_.Deallocate(ptr);
    return locked;

}

bool ALockHandle::IsLTailLocked(){
    // remotely read in value of current l_tail on host
    auto remote = pool_.Read<remote_ptr<LocalDescriptor>>(r_l_tail_);
    // store result of if its locked
    auto locked = static_cast<uint64_t>(*(std::to_address(remote))) != 0;
    // deallocate the ptr used as a landing spot for reading in (which is created in Read)
    auto ptr =
        remote_ptr<remote_ptr<LocalDescriptor>>{self_.id, std::to_address(remote)};
    pool_.Deallocate(ptr);
    return locked;
}

bool ALockHandle::IsRemoteVictim() {
    // read in value of victim var on host
    auto remote = pool_.Read<uint64_t>(r_victim_);
    // store result of if its locked
    auto check_victim = static_cast<uint64_t>(*(std::to_address(remote))) == REMOTE_VICTIM;
    // deallocate the ptr used as a landing spot for reading in (which is created in Read)
    auto ptr =
        remote_ptr<uint64_t>{self_.id, std::to_address(remote)};
    pool_.Deallocate(ptr);
    return check_victim;
}

void ALockHandle::LockRemoteMcsQueue(){
    ROME_DEBUG("Locking remote MCS queue...");
  
    // Set local RdmaDescriptor to initial values
    r_desc_->budget = -1;
    r_desc_->next = remote_nullptr;

    // swap RdmaDescriptor onto the remote tail of the alock 
    auto prev =
      pool_.AtomicSwap(r_tail_, static_cast<uint64_t>(r_desc_pointer_));
    
    if (prev != remote_nullptr) { //someone else has the lock
        auto temp_ptr = remote_ptr<uint8_t>(prev);
        temp_ptr +=32; //temp_ptr = next field of the current tail's RdmaDescriptor
        // make prev point to the current tail RdmaDescriptor's next pointer
        prev = remote_ptr<RdmaDescriptor>(temp_ptr);
        // set the address of the current tail's next field = to the addr of our local RdmaDescriptor
        pool_.Write<remote_ptr<RdmaDescriptor>>(
            static_cast<remote_ptr<remote_ptr<RdmaDescriptor>>>(prev), r_desc_pointer_,
            prealloc_);
        ROME_DEBUG("[Lock] Enqueued: {} --> (id={})",
                static_cast<uint64_t>(prev.id()),
                static_cast<uint64_t>(r_desc_pointer_.id()));
        // spins locally, waits for current tail/lockholder to write to budget when it unlocks
        while (r_desc_->budget < 0) {
            cpu_relax();
        }
        if (r_desc_->budget == 0) {
            ROME_DEBUG("Budget exhausted (id={})",
                        static_cast<uint64_t>(r_desc_pointer_.id()));
            // Release the lock before trying to continue
            Reacquire();
            r_desc_->budget = kInitBudget;
        }
    } else { //no one had the lock, we were swapped in
        // set lock holders RdmaDescriptor budget to initBudget since we are the first lockholder
        r_desc_->budget = kInitBudget;
        is_r_leader_ = true;
    }
    // budget was set to greater than 0, CS can be entered
    ROME_DEBUG("[Lock] Acquired: prev={:x}, budget={:x} (id={})",
                static_cast<uint64_t>(prev), r_desc_->budget,
                static_cast<uint64_t>(r_desc_pointer_.id()));

    //  make sure Lock operation finished
    std::atomic_thread_fence(std::memory_order_acquire);
}

void ALockHandle::RemoteLock(){
    LockRemoteMcsQueue();
    if (is_r_leader_){
        auto prev = pool_.AtomicSwap(r_victim_, static_cast<uint8_t>(REMOTE_VICTIM));
        //! this is remotely spinning on the victim var?? --> but competition is only among two at this point
        while (IsRemoteVictim() && IsLTailLocked()){
            cpu_relax();
        } 
    }
}

void ALockHandle::LockLocalMcsQueue(){

}

void ALockHandle::LocalLock(){
  ROME_DEBUG("ALockHandle::LocalLock()");
    // ROME_ASSERT(!is_locked_, "Lock has already been called on LockHandle.");
    // is_locked_ = true;
    LockLocalMcsQueue();
    if (is_l_leader_){
        auto prev = l_victim_.exchange(LOCAL_VICTIM, std::memory_order_acquire);
        while (*l_victim_== LOCAL_VICTIM && IsRTailLocked()){
            cpu_relax();
        } 
    }
    std::atomic_thread_fence(std::memory_order_release);
}

void ALockHandle::RemoteUnlock(){
    // Make sure everything finished before unlocking
    std::atomic_thread_fence(std::memory_order_release);
    // if r_tail_ == my desc (we are the tail), set it to 0 to unlock
    // otherwise, someone else is contending for lock and we want to give it to them
    // try to swap in a 0 to unlock the RdmaDescriptor at the addr of the remote tail, which we expect to currently be equal to our RdmaDescriptor
    auto prev = pool_.CompareAndSwap(r_tail_,
                                    static_cast<uint64_t>(r_desc_pointer_), 0);
   
    // if the descriptor at r_tail_ was not our RdmaDescriptor (other clients have attempted to lock & enqueued since)
    if (prev != r_desc_pointer_) {  
        // attempt to hand the lock to prev

        // make sure next pointer gets set before continuing
        while (r_desc_->next == remote_nullptr)
        ;
        std::atomic_thread_fence(std::memory_order_acquire);

        // gets a pointer to the next RdmaDescriptor object
        auto next = const_cast<remote_ptr<RdmaDescriptor> &>(r_desc_->next);
        //writes to the the next descriptors budget which lets it know it has the lock now
        pool_.Write<uint64_t>(static_cast<remote_ptr<uint64_t>>(next),
                            r_desc_->budget - 1,
                            static_cast<remote_ptr<uint64_t>>(prealloc_));
    } 
    //else: successful CAS, we unlocked our RdmaDescriptor and no one is queued after us
    // is_locked_ = false;
    is_r_leader_ = false;
    ROME_DEBUG("[Unlock] Unlocked (id={})",
                static_cast<uint64_t>(r_desc_pointer_.id()));
}

void ALockHandle::LocalUnlock(){
    std::atomic_thread_fence(std::memory_order_release);
    ROME_DEBUG("ALockHandle::LocalUnLock()");
    //...leave the critical section
    // check whether this thread's local node’s next field is null
    if (l_desc_->next == nullptr) {
        // if so, then either:
        //  1. no other thread is contending for the lock
        //  2. there is a race condition with another thread about to
        // to distinguish between these cases atomic compare exchange the tail field
        // if the call succeeds, then no other thread is trying to acquire the lock,
        // tail is set to nullptr, and unlock() returns
        LocalDescriptor* p = l_desc_;
        if (l_l_tail_.compare_exchange_strong(p, nullptr, std::memory_order_release,
                                        std::memory_order_relaxed)) {
            return;
        }
        // otherwise, another thread is in the process of trying to acquire the
        // lock, so spins waiting for it to finish
        while (l_desc_->next == nullptr) {
        };
    }
    // in either case, once the successor has appeared, the unlock() method sets
    // its successor’s locked field to false, indicating that the lock is now free
    l_desc_->next->budget = l_desc_->budget - 1;
    //! NOT sure if this is reasonable
    is_l_leader_ = false;
    // ! THIS IS HOW WE KNOW WHEN TO ALLOW REUSING (CHECK IF NEXT IS NULLPTR AND ALWAYS MOVE FORWARD)
    // at this point no other thread can access this node and it can be reused
    l_desc_->next = nullptr;
}

//Eventually will take in remote_ptr<ALock> once using sharding or just an offset?
void ALockHandle::Lock(){
  // ROME_ASSERT(a_lock_->locked == false, "Attempting to lock handle that is already locked.")
  if (IsLocal()){ 
    ROME_DEBUG("ALock is LOCAL to node {}", static_cast<uint64_t>(self_.id));
    LocalLock();
  } else {
    ROME_DEBUG("ALock is REMOTE");
    RemoteLock();
  }
  // a_lock_->locked = true;
}

void ALockHandle::Unlock(){
  // ROME_ASSERT(a_lock_->locked == true, "Attempting to unlock handle that is not locked.")
  if (IsLocal()){
    LocalUnlock();
  } else {
    RemoteUnlock();
  }
  // a_lock_->locked = false;
}


}
