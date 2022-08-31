#pragma once

#include <memory>

#include "absl/status/status.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "util.h"

namespace X {

using Peer = MemoryPool::Peer;

class RdmaSpinLock {
public:
  RdmaSpinLock(const Peer &self, MemoryPool::cm_type *cm)
      : self_(self), cm_(cm), pool_(self_, cm) {}

  absl::Status Init(const Peer &host,
                    const std::vector<MemoryPool::Peer> &peers) {
    bool is_host = self_.id == host.id;
    auto capacity = 2 * sizeof(uint64_t);
    auto status = pool_.Init(capacity, peers);
    ROME_CHECK_OK(ROME_RETURN(status), status);

    if (is_host) {
      // Send all peers the base address of the lock residing on the host
      RemoteObjectProto proto;
      lock_ = pool_.Allocate<uint64_t>();
      proto.set_raddr(lock_.address());

      *(std::to_address(lock_)) = 0;
      for (const auto &p : peers) {
        auto conn_or = cm_->GetConnection(p.id);
        ROME_CHECK_OK(ROME_RETURN(conn_or.status()), conn_or);
        status = conn_or.value()->channel()->Send(proto);
        ROME_CHECK_OK(ROME_RETURN(status), status);
      }
    } else {
      // Otherwise, wait until the base address is shared by the host
      auto conn_or = cm_->GetConnection(host.id);
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

  void Lock() {
    while (pool_.CompareAndSwap(lock_, kUnlocked, self_.id) != kUnlocked) {
      cpu_relax();
    }
  }

  void Unlock() { pool_.Write<uint64_t>(lock_, 0, /*prealloc=*/local_); }

private:
  static constexpr uint64_t kUnlocked = 0;

  Peer self_;
  Peer host_;

  remote_ptr<uint64_t> lock_;
  remote_ptr<uint64_t> local_;

  MemoryPool::cm_type *cm_;
  MemoryPool pool_;
};

} // namespace X