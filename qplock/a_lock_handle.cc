#include "a_lock_handle.h"

#include <infiniband/verbs.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/remote_ptr.h"
#include "util.h"



namespace X {

using ::rome::rdma::ConnectionManager;
using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;
using ::rome::rdma::RemoteObjectProto;

ALockHandle::ALockHandle(MemoryPool::Peer self, MemoryPool &pool)
    : self_(self), pool_(pool) {
      this->r_handle_ = std::unique_ptr<RemoteLockHandle>(new RemoteLockHandle(self, pool));
      this->l_handle_ = std::unique_ptr<LockHandle>(new LockHandle(self, pool));
    }

absl::Status ALockHandle::Init(MemoryPool::Peer host,
                               const std::vector<MemoryPool::Peer> &peers) {
  is_host_ = self_.id == host.id;
  // TODO: Currently at 16MB, but really shouldn't need this much
  auto capacity = 1 << 24;
  auto status = pool_.Init(capacity, peers);
  ROME_ASSERT_OK(status);

  if (is_host_) {
    // Send all peers the base address of the lock residing on the host
    RemoteObjectProto proto;
    a_lock_pointer_ = pool_.Allocate<ALock>();
    proto.set_raddr(a_lock_pointer_.address());
    ROME_DEBUG("Lock pointer {:x}", static_cast<uint64_t>(a_lock_pointer_));
    //! This might need to change because it's no longer a nested pointer but not sure 
    // TODO: THIS MIGHT BE THE BUG >:(
    // *(std::to_address(a_lock_pointer_)) = remote_ptr<ALock>(0);
    // a_lock_ = reinterpret_cast<ALock *>(a_lock_pointer_.address());
    // tell all the peers where to find the addr of the first lock
    for (const auto &p : peers) {
      auto conn_or = pool_.connection_manager()->GetConnection(p.id);
      ROME_CHECK_OK(ROME_RETURN(conn_or.status()), conn_or);
      status = conn_or.value()->channel()->Send(proto);
      ROME_CHECK_OK(ROME_RETURN(status), status);
    }
  } else {
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
    prealloc_ = pool_.Allocate<ALock>();
  }
  ROME_DEBUG("Lock pointer {:x}", static_cast<uint64_t>(a_lock_pointer_));
  return absl::OkStatus();
}

bool ALockHandle::IsLocked() {
  // TODO: figure this out
  // return a_lock_->locked; 
  return false;
}

// may need to change to take in an addr
bool inline ALockHandle::IsLocal(){
  bool isLocal = ((a_lock_pointer_).id() == self_.id);
  ROME_DEBUG("Checking if alock isLocal: {}", isLocal);
  return isLocal;
}

//Eventually will take in remote_ptr<ALock> once using sharding
void ALockHandle::Lock(){
  ROME_DEBUG("trying to lock alock...");
  // ROME_ASSERT_DEBUG((r_handle_ != NULL || l_handle_ != NULL),"Lock() was already called on ALockHandle");
  // TODO: SEGFAULT ON LINE BELOW
  // ROME_ASSERT(a_lock_->locked == false, "Attempting to lock handle that is already locked.")
  if (IsLocal()){ 
    ROME_DEBUG("ALock is LOCAL");
    //client is local to the desired ALock
    l_handle_->Init(a_lock_pointer_);
    std::atomic_thread_fence(std::memory_order_release);
    l_handle_->Lock();
  } else {
    ROME_DEBUG("ALock is REMOTE");
    //client is remote to the desired ALock
    r_handle_->Init(a_lock_pointer_);
    std::atomic_thread_fence(std::memory_order_release);
    r_handle_->Lock();
  }
  // a_lock_->locked = true;
  std::atomic_thread_fence(std::memory_order_release);
}

void ALockHandle::Unlock(){
  std::atomic_thread_fence(std::memory_order_release);
  // ROME_ASSERT(a_lock_->locked == true, "Attempting to unlock handle that is not locked.")
  if (IsLocal()){
    ROME_ASSERT_DEBUG(l_handle_ != NULL, "Attempting to unlock null local lock handle");
    l_handle_->Unlock();
  } else {
    ROME_ASSERT_DEBUG(r_handle_ != NULL, "Attempting to unlock null remote lock handle");
    r_handle_->Unlock();
  }
  std::atomic_thread_fence(std::memory_order_release);
  // a_lock_->locked = false;
}


}
