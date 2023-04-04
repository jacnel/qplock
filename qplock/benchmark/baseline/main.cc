#include <infiniband/verbs.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "google/protobuf/text_format.h"
#include "qplock/benchmark/baseline/experiment.pb.h"
#include "rome/colosseum/client_adaptor.h"
#include "rome/colosseum/qps_controller.h"
#include "rome/colosseum/streams/streams.h"
#include "rome/colosseum/workload_driver.h"
#include "rome/logging/logging.h"
#include "rome/metrics/summary.h"
#include "rome/rdma/rdma_broker.h"
#include "rome/util/clocks.h"
#include "rome/util/proto_util.h"

#include "client.h"
#include "server.h"
#include "setup.h"

ROME_PROTO_FLAG(ExperimentParams);
ROME_PROTO_FLAG(ClusterProto);

ABSL_FLAG(ClusterProto, cluster, {}, "Cluster description");
ABSL_FLAG(ExperimentParams, experiment_params, {}, "Experimental parameters");

using ::util::SystemClock;

// std::vector<unique_ptr<Client>> client_ptrs;

std::function<void(int)> signal_handler_internal;
void signal_handler(int signum) { 
  ROME_INFO("HANDLER!!!\n");
  signal_handler_internal(signum); 
}
volatile bool done = false;

int main(int argc, char *argv[]) {
  ROME_INIT_LOG();
  absl::ParseCommandLine(argc, argv);

  auto cluster = absl::GetFlag(FLAGS_cluster);
  auto experiment_params = absl::GetFlag(FLAGS_experiment_params);
  ROME_ASSERT_OK(ValidateExperimentParams(&experiment_params));

  if (!experiment_params.workload().has_runtime() ||
      experiment_params.workload().runtime() < 0) {
    signal_handler_internal = std::function([](int signum) {
      if (done)
        exit(1);
      ROME_INFO("Shutting down...");
      done = true;
    });
    signal(SIGINT, signal_handler);
  }

  // Update the experiment config based on the cluster configuration.
  experiment_params.set_cluster_size(cluster.clients().size());
  experiment_params.set_num_clients(experiment_params.client_ids().size());

  //! We may want to have multiple servers in the future, but for now we just
  //! assume the one given by `server_id` in the experiment params.
  std::vector<Peer> servers;
  Peer server;
  for (auto &s : cluster.servers()) {
    Peer p = Peer((uint16_t)s.nid(), s.private_hostname(), (uint16_t)s.port());
    if (s.nid() == experiment_params.server_id()) {
      server = p;
    }
    servers.push_back(p);
  }
  std::vector<Peer> clients;
  std::vector<Peer> peers;
  std::for_each(
      cluster.clients().begin(), cluster.clients().end(), [&](auto &c) {
        auto p =
            Peer((uint16_t)c.nid(), c.private_hostname(), (uint16_t)c.port());

        if (experiment_params.mode() == Mode::CLIENT) {
          bool run = false;
          for (const auto &id : experiment_params.client_ids()) {
            if (id == c.nid()) {
              run = true;
              break;
            }
          }
          if (run) {
            clients.push_back(p);
          }
          peers.push_back(p);
        } else {
          clients.push_back(p);
        }
      });

  if (experiment_params.mode() == Mode::SERVER) { // We are server
    auto s = Server::Create(server, clients);
    auto status = s->Launch(&done, experiment_params.workload().runtime());
    ROME_ASSERT_OK(status);
    // should we call record results here as well?
  } else {  // We are client
    peers.push_back(server);
    ROME_ASSERT_OK(ValidateClientExperimentParams(&experiment_params));

    // Launch each of the clients.
    std::vector<std::thread> client_threads;
    std::vector<std::future<absl::StatusOr<ResultProto>>> client_tasks;
    std::barrier client_barrier(experiment_params.client_ids().size());
    for (const auto &c : clients) {
      client_tasks.emplace_back(std::async([=, &client_barrier]() {
        std::vector<Peer> others;
        std::copy_if(peers.begin(), peers.end(), std::back_inserter(others),
                     [c](auto &p) { return p.id != c.id; });
        auto client = Client::Create(c, server, others, experiment_params,
                                     &client_barrier);
        // call Client::Connect() --> should store remote_ptr to first node in the kv store and first qp lock
        // Client::Connect(std::move(client));
        return Client::Run(std::move(client), experiment_params, &done);
      }));
    }

    // Join clients.
    std::vector<ResultProto> results;
    for (auto &r : client_tasks) {
      r.wait();
      ROME_ASSERT(r.valid(), "WTF");
      auto rproto = r.get();
      results.push_back(VALUE_OR_DIE(rproto));
      ROME_INFO("{}", VALUE_OR_DIE(rproto).DebugString());
    }

    RecordResults(experiment_params, results);
  }

  ROME_INFO("Done");
  return 0;
}