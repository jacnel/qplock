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
  is_host_ = self_.id == host.id;
  auto capacity = 1 << 20;
  auto status = pool_.Init(capacity, peers);
  ROME_ASSERT_OK(status);

  // Reserve remote memory for the async lock.
  lock_pointer_ = pool_.Allocate<AsyncLock>();
  lock_ = reinterpret_cast<AsyncLock *>(lock_pointer_.address());
  ROME_DEBUG("AsyncLock @ {:x}", static_cast<uint64_t>(lock_pointer_));

  //Allocated a pointer here
  //Pass this memory to the mcs queue

  if (is_host_) {
    // Send all peers the base address of the AsyncLock residing on the host
    RemoteObjectProto proto;
    lock_pointer_ = pool_.Allocate<remote_ptr<Descriptor>>();
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
    descriptor_->local_q.Lock(); 
  } else {
    descriptor_->remote_q.Lock();
  }
  //attempt to acquire remote mcs lock

}

void QPLock::Unlock(){}

void QPLock::Reacquire(){}

} // namespace X