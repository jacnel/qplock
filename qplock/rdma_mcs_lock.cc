#include "rdma_mcs_lock.h"

#include <infiniband/verbs.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/remote_ptr.h"
#include "util.h"

constexpr int kInitBudget = 5;

namespace X {

using ::rome::rdma::ConnectionManager;
using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;
using ::rome::rdma::RemoteObjectProto;

RdmaMcsLock::RdmaMcsLock(MemoryPool::Peer self, MemoryPool& pool)
    : self_(self), pool_(pool) {}

absl::Status RdmaMcsLock::Init(MemoryPool::Peer host,
                               const std::vector<MemoryPool::Peer> &peers) {
  is_host_ = self_.id == host.id;
  auto capacity = 1 << 20;
  auto status = pool_.Init(capacity, peers);
  ROME_CHECK_OK(ROME_RETURN(status), status);

  // Reserve remote memory for the local descriptor.
  desc_pointer_ = pool_.Allocate<Descriptor>();
  descriptor_ = reinterpret_cast<Descriptor *>(desc_pointer_.address());
  ROME_DEBUG("Descriptor @ {:x}", static_cast<uint64_t>(desc_pointer_));

  if (is_host_) {
    // Send all peers the base address of the lock residing on the host
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

bool RdmaMcsLock::IsLocked() {
  if (is_host_) {
    return std::to_address(*(std::to_address(lock_pointer_))) != 0;
  } else {
    auto remote = pool_.Read<remote_ptr<Descriptor>>(lock_pointer_);
    auto locked = static_cast<uint64_t>(*(std::to_address(remote))) != 0;
    auto ptr =
        remote_ptr<remote_ptr<Descriptor>>{self_.id, std::to_address(remote)};
    pool_.Deallocate(ptr);
    return locked;
  }
}

void RdmaMcsLock::Lock() {
  ROME_ASSERT_DEBUG(!is_host_, "Unimplemented!");
  descriptor_->budget = -1;
  descriptor_->next = remote_nullptr;
  auto prev =
      pool_.AtomicSwap(lock_pointer_, static_cast<uint64_t>(desc_pointer_));
  if (prev != remote_nullptr) {
    auto temp_ptr = remote_ptr<uint8_t>(prev);
    temp_ptr += 64;
    prev = remote_ptr<Descriptor>(temp_ptr);
    pool_.Write<remote_ptr<Descriptor>>(
        static_cast<remote_ptr<remote_ptr<Descriptor>>>(prev), desc_pointer_,
        prealloc_);
    ROME_DEBUG("[Lock] Enqueued: {} --> (id={})",
               static_cast<uint64_t>(prev.id()),
               static_cast<uint64_t>(desc_pointer_.id()));
    while (descriptor_->budget < 0) {
      cpu_relax();
    }
    if (descriptor_->budget == 0) {
      ROME_DEBUG("Budget exhausted (id={})",
                 static_cast<uint64_t>(desc_pointer_.id()));
      descriptor_->budget = kInitBudget;
    }
  } else {
    descriptor_->budget = kInitBudget;
  }
  ROME_DEBUG("[Lock] Acquired: prev={:x}, budget={:x} (id={})",
             static_cast<uint64_t>(prev), descriptor_->budget,
             static_cast<uint64_t>(desc_pointer_.id()));
  std::atomic_thread_fence(std::memory_order_acquire);
}

void RdmaMcsLock::Unlock() {
  std::atomic_thread_fence(std::memory_order_release);
  ROME_ASSERT_DEBUG(!is_host_, "Unimplemented!");
  auto prev = pool_.CompareAndSwap(lock_pointer_,
                                   static_cast<uint64_t>(desc_pointer_), 0);
  if (prev != desc_pointer_) {
    while (descriptor_->next == remote_nullptr)
      ;
    std::atomic_thread_fence(std::memory_order_acquire);
    auto next = const_cast<remote_ptr<Descriptor> &>(descriptor_->next);
    pool_.Write<uint64_t>(static_cast<remote_ptr<uint64_t>>(next),
                          descriptor_->budget - 1,
                          static_cast<remote_ptr<uint64_t>>(prealloc_));
  } else {
    ROME_DEBUG("[Unlock] Unlocked (id={})",
               static_cast<uint64_t>(desc_pointer_.id()));
  }
}

} // namespace X
