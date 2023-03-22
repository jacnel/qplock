#include "a_lock_handle.h"

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
  // TODO: Currently at 16 MB and runs OOM, but other locks dont even need this much
  // TODO: TRY OUT 1GB each (1 << 30)
  auto capacity = 1 << 24;
  auto status = pool_.Init(capacity, peers);
  // ROME_ASSERT_OK(status);
  ROME_CHECK_OK(ROME_RETURN(status), status);

  if (is_host_) {
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

// may need to change to take in an addr for when we expand to multiple locks
bool inline ALockHandle::IsLocal(){
  return ((a_lock_pointer_).id() == self_.id);
}

//Eventually will take in remote_ptr<ALock> once using sharding or just an offset?
void ALockHandle::Lock(){
  // ROME_ASSERT_DEBUG((r_handle_ != NULL || l_handle_ != NULL),"Lock() was already called on ALockHandle");
  // SEGFAULT ON LINE BELOW
  // ROME_ASSERT(a_lock_->locked == false, "Attempting to lock handle that is already locked.")
  if (IsLocal()){ 
    ROME_DEBUG("ALock is LOCAL");
    l_handle_->Init(a_lock_pointer_);
    std::atomic_thread_fence(std::memory_order_release);
    l_handle_->Lock();
  } else {
    ROME_DEBUG("ALock is REMOTE");
    r_handle_->Init(a_lock_pointer_);
    std::atomic_thread_fence(std::memory_order_release);
    r_handle_->Lock();
  }
  // a_lock_->locked = true;
}

void ALockHandle::Unlock(){
  // ROME_ASSERT(a_lock_->locked == true, "Attempting to unlock handle that is not locked.")
  if (IsLocal()){
    ROME_ASSERT_DEBUG(l_handle_ != NULL, "Attempting to unlock null local lock handle");
    l_handle_->Unlock();
  } else {
    ROME_ASSERT_DEBUG(r_handle_ != NULL, "Attempting to unlock null remote lock handle");
    r_handle_->Unlock();
  }
  // a_lock_->locked = false;
}


}
