#pragma once

#include <infiniband/verbs.h>

#include <memory>

#include "qplock/benchmark/baseline/experiment.pb.h"
#include "qplock/locks/rdma_mcs_lock/rdma_mcs_lock.h"
#include "qplock/locks/spin_lock/rdma_spin_lock.h"
#include "qplock/locks/a_lock/a_lock_handle.h"

#include "absl/status/status.h"
#include "rome/colosseum/client_adaptor.h"
#include "rome/colosseum/streams/streams.h"
#include "rome/logging/logging.h"
#include "rome/metrics/counter.h"
#include "rome/rdma/channel/sync_accessor.h"
#include "rome/rdma/connection_manager/connection.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/util/status_util.h"

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
using ::rome::rdma::RemoteObjectProto;
using Peer = ::rome::rdma::MemoryPool::Peer;
using cm_type = ::rome::rdma::MemoryPool::cm_type;
using conn_type = ::rome::rdma::MemoryPool::conn_type;

#ifdef LOCK_TYPE
using LockType = LOCK_TYPE;
#else
#error "LOCK_TYPE is undefined"
#endif

static constexpr uint16_t kServerPort = 18000;
static constexpr uint16_t kBaseClientPort = 18001;

inline absl::Status ValidateExperimentParams(ExperimentParams *params) {
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

inline absl::Status ValidateClientExperimentParams(ExperimentParams *params) {
  ROME_CHECK_QUIET(ROME_RETURN(util::FailedPreconditionErrorBuilder()
                               << "Invalid ExperimentParams provided: "
                               << params->ShortDebugString()),
                   params->client_ids_size() > 0);
  return absl::OkStatus();
}

inline void RecordResults(const ExperimentParams &experiment_params,
                          const std::vector<ResultProto> &experiment_results) {
  ResultsProto results;
  results.mutable_experiment_params()->CopyFrom(experiment_params);
  results.set_cluster_size(experiment_params.cluster_size());
  for (auto &result : experiment_results) {
    auto *r = results.add_results();
    r->CopyFrom(result);
  }

  if (experiment_params.has_save_dir()) {
    auto save_dir = experiment_params.save_dir();
    ROME_ASSERT(!save_dir.empty(),
                "Trying to write results to a file, but results "
                "directory is empty string");
    if (!std::filesystem::exists(save_dir) &&
        !std::filesystem::create_directories(save_dir)) {
      ROME_FATAL("Failed to create save directory. Exiting...");
    }

    auto filestream = std::ofstream();
    std::filesystem::path outfile;
    outfile /= save_dir;
    std::string filename =
        ((experiment_params.mode() == Mode::CLIENT) ? "client." : "server.") +
        (experiment_params.has_name() ? experiment_params.name() : "data") +
        ".pbtxt";
    outfile /= filename;
    filestream.open(outfile);
    ROME_ASSERT(filestream.is_open(), "Failed to open output file: {}",
                outfile.c_str());
    ROME_INFO("Saving results to '{}'", outfile.c_str());
    filestream << results.DebugString();
    filestream.flush();
    filestream.close();
  } else {
    std::cout << results.DebugString();
  }
}