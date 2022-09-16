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

QPLock::QPLock(MemoryPool::Peer self,
                         std::unique_ptr<MemoryPool::cm_type> cm)
    : self_(self), pool_(self, std::move(cm)) {}

bool QPLock::IsLocked(){}

void QPLock::Lock(){}

void QPLock::Unlock(){}

} // namespace X