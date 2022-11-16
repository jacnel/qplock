#include "qplock.h"

#include <infiniband/verbs.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/remote_ptr.h"
#include "rdma_mcs_lock.h"
#include "mcs_lock.h"
#include "util.h"

namespace X {

using ::rome::rdma::ConnectionManager;
using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;
using ::rome::rdma::RemoteObjectProto;

QPLock::QPLock(MemoryPool::Peer self, MemoryPool &pool)
    : self_(self), pool_(pool) {}

// TODO: Modify
absl::Status QPLock::Init(MemoryPool::Peer host,
                               const std::vector<MemoryPool::Peer> &peers) {
  auto capacity = 1 << 20;
  auto status = pool_.Init(capacity, peers); 
  ROME_ASSERT_OK(status);

  // Reserve remote memory for the local async lock.
  async_pointer_ = pool_.Allocate<AsyncLock>();
  async_lock_ = reinterpret_cast<AsyncLock *>(async_pointer_.address());
  ROME_DEBUG("AsyncLock @ {:x}", static_cast<uint64_t>(async_pointer_));

  // Allocate the local MCS queue
  
  // Remote q begins as a nullptr to a rdma_mcs, first remote operation will allocate a 
  // remote desciptor and swap its descriptor in at this location
  // r_tail->desc_pointer_ = remote_nullptr;


  // Send all peers the address of the async lock
  // Address of the remote tail is address of the async lock + 32

  if (is_host_) {
    // Send all peers the base address of the AsyncLock residing on the host
    RemoteObjectProto proto;
    lock_pointer_ = pool_.Allocate<remote_ptr<AsyncLock>>();
    proto.set_raddr(lock_pointer_.address());

    *(std::to_address(lock_pointer_)) = remote_ptr<Descriptor>(0);
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
    lock_pointer_ = decltype(lock_pointer_)(host.id, got->raddr());

    prealloc_ = pool_.Allocate<remote_ptr<Descriptor>>();
  }
  ROME_DEBUG("Lock pointer {:x}", static_cast<uint64_t>(lock_pointer_));
  return absl::OkStatus();
}


void QPLock::Lock(){
  if (is_host_) {
    //attempt to acquire the local msc lock
    lock_->l_tail.Lock(); 
  } else {
    //attempt to acquire remote mcs lock
    lock_->r_tail.Lock();
  }
  // At this point, the leader has the lock, so whoever isLocked can execute their critical section

}

void QPLock::Unlock(){
  if (is_host_) { 
    lock_->l_tail.Unlock();
  } else { 
    lock_->r_tail.Unlock();
  }
}

void QPLock::Reacquire(){
  victim = getCid();
  
}

} // namespace Xj