#include "rdma_spin_lock.h"

namespace X {

using ::rome::rdma::ConnectionManager;
using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;
using ::rome::rdma::RemoteObjectProto;

RdmaSpinLock::RdmaSpinLock(MemoryPool::Peer self, MemoryPool &pool)
    : self_(self), pool_(pool) {}

absl::Status RdmaSpinLock::Init(MemoryPool::Peer host,
                                const std::vector<MemoryPool::Peer> &peers) {
  is_host_ = self_.id == host.id;
  auto capacity = 1 << 20;
  auto status = pool_.Init(capacity, peers);
  ROME_ASSERT_OK(status);

  if (is_host_) {
    // Send all peers the base address of the lock residing on the host
    RemoteObjectProto proto;
    lock_ = pool_.Allocate<uint64_t>();
    proto.set_raddr(lock_.address());

    *(std::to_address(lock_)) = 0;
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
    lock_ = decltype(lock_)(host.id, got->raddr());

    local_ = pool_.Allocate<uint64_t>();
  }
  ROME_DEBUG("Lock: {:x}", (uint64_t)lock_);
  return absl::OkStatus();
}

bool RdmaSpinLock::IsLocked() { return *std::to_address(lock_) != kUnlocked; }

void RdmaSpinLock::Lock() {
  while (pool_.CompareAndSwap(lock_, kUnlocked, self_.id) != kUnlocked) {
    cpu_relax();
  }
}

void RdmaSpinLock::Unlock() {
  pool_.Write<uint64_t>(lock_, 0, /*prealloc=*/local_);
}

} // namespace X
