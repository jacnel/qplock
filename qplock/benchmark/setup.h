#pragma once

#include <infiniband/verbs.h>

#include <memory>

#include "absl/status/status.h"
#include "benchmarks/qplock/experiment.pb.h"
#include "benchmarks/qplock/setup.h"
#include "rome/colosseum/client_adaptor.h"
#include "rome/colosseum/streams/streams.h"
#include "rome/logging/logging.h"
#include "rome/metrics/counter.h"
#include "rome/rdma/channel/sync_accessor.h"
#include "rome/util/status_util.h"
#include "src/node/cloudlab_node.h"
#include "src/node/connection.h"
#include "src/node/connection_manager.h"
#include "src/node/memory_pool.h"
#include "src/qplock/rdma_mcs_lock.h"
#include "src/qplock/rdma_spin_lock.h"

#ifdef __cpp_lib_hardware_interference_size
using std::hardware_constructive_interference_size;
using std::hardware_destructive_interference_size;
#else
// 64 bytes on x86-64 │ L1_CACHE_BYTES │ L1_CACHE_SHIFT │ __cacheline_aligned │
// ...
constexpr std::size_t hardware_constructive_interference_size = 64;
constexpr std::size_t hardware_destructive_interference_size = 64;
#endif

using ::rome::ClientAdaptor;
using ::rome::Stream;
using ::rome::metrics::Counter;
using ::X::CloudlabNode;
using ::X::RemoteObjectProto;
using Peer = ::X::MemoryPool::Peer;
using cm_type = ::X::MemoryPool::cm_type;
using conn_type = ::X::MemoryPool::conn_type;

#ifdef QPLOCK_LOCK_TYPE
using LockType = QPLOCK_LOCK_TYPE;
#else
#error "QPLOCK_LOCK_TYPE is undefined"
#endif

static constexpr uint16_t kServerPort = 18000;
static constexpr uint16_t kBaseClientPort = 18001;

// Encodes the role of each participant in the system. In this case, we only
// have a single server and several clients. Hence, the types are either
// `Type::kServer` or `Type::kClient`.
class Role {
 public:
  enum class Type {
    kServer,
    kClient,
  };

  Type type() const { return type_; }

  void ParseFromString(std::string_view input) {
    if (input == "kServer") {
      type_ = Type::kServer;
    } else if (input == "kClient") {
      type_ = Type::kClient;
    } else {
      ROME_FATAL("Uknown type: {}", input);
    }
  }

  std::string DebugString() const {
    if (type_ == Type::kServer) {
      return "kServer";
    } else {
      return "kClient";
    }
  }

 private:
  Type type_;
};
using Node = CloudlabNode<Role>;

inline absl::Status ValidateExperimentParams(ExperimentParams* params) {
  ROME_CHECK_QUIET(ROME_RETURN(util::FailedPreconditionErrorBuilder()
                               << "Invalid ExperimentParams provided: "
                               << params->ShortDebugString()),
                   params->has_mode());
  if (!params->has_sampling_rate_ms()) {
    // Set default sampling rate to avoid doing it every time.
    params->set_sampling_rate_ms(50);
  }
  return absl::OkStatus();
}

inline absl::Status ValidateClientExperimentParams(ExperimentParams* params) {
  ROME_CHECK_QUIET(ROME_RETURN(util::FailedPreconditionErrorBuilder()
                               << "Invalid ExperimentParams provided: "
                               << params->ShortDebugString()),
                   params->client_ids_size() > 0);
  return absl::OkStatus();
}