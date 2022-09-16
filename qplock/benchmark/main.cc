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
#include "client.h"
#include "google/protobuf/text_format.h"
#include "qplock/benchmark/experiment.pb.h"
#include "rome/colosseum/client_adaptor.h"
#include "rome/colosseum/qps_controller.h"
#include "rome/colosseum/streams/streams.h"
#include "rome/colosseum/workload_driver.h"
#include "rome/logging/logging.h"
#include "rome/metrics/summary.h"
#include "rome/rdma/rdma_broker.h"
#include "rome/util/clocks.h"
#include "rome/util/proto_util.h"
#include "server.h"
#include "setup.h"

ROME_PROTO_FLAG(ExperimentParams);
ROME_PROTO_FLAG(ClusterProto);

ABSL_FLAG(ClusterProto, cluster, {}, "Cluster description");
ABSL_FLAG(ExperimentParams, experiment_params, {}, "Experimental parameters");

using ::util::SystemClock;

std::function<void(int)> signal_handler_internal;
void signal_handler(int signum) { signal_handler_internal(signum); }
volatile bool done = false;

int main(int argc, char *argv[]) {
  ROME_INIT_LOG();
  absl::ParseCommandLine(argc, argv);

  auto cluster = absl::GetFlag(FLAGS_cluster);
  auto experiment_params = absl::GetFlag(FLAGS_experiment_params);
  ROME_ASSERT_OK(ValidateExperimentParams(&experiment_params));

  if (!experiment_params.has_runtime() || experiment_params.runtime() < 0) {
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

  auto server = Peer((uint16_t)cluster.server().id(), cluster.server().host(),
                     (uint16_t)cluster.server().port());
  std::vector<Peer> clients;
  std::vector<Peer> peers;
  std::for_each(
      cluster.clients().begin(), cluster.clients().end(), [&](auto &c) {
        auto p = Peer((uint16_t)c.id(), c.host(), (uint16_t)c.port());
        std::for_each(experiment_params.client_ids().begin(),
                      experiment_params.client_ids().end(), [&](auto &id) {
                        if (id != c.id()) {
                          peers.push_back(p);
                        } else {
                          clients.push_back(p);
                        }
                      });
      });

  if (experiment_params.mode() == Mode::SERVER) { // We are server
    auto s = Server::Create(server, clients);
    auto status = s->Launch(&done, experiment_params.runtime());
    ROME_ASSERT_OK(status);
  } else {
    peers.push_back(server);
    ROME_ASSERT_OK(ValidateClientExperimentParams(&experiment_params));

    // Launch each of the clients.
    std::vector<std::thread> client_threads;
    std::vector<std::future<absl::StatusOr<ResultProto>>> results;
    std::barrier client_barrier(experiment_params.client_ids().size());
    for (const auto &c : clients) {
      results.emplace_back(std::async([=, &client_barrier]() {
        auto client = Client::Create(c, server, peers, experiment_params,
                                     &client_barrier);
        return Client::Run(std::move(client), experiment_params, &done);
      }));
    }

    // Join clients.
    for (auto &r : results) {
      auto rproto = r.get();
      ROME_INFO("{}", VALUE_OR_DIE(rproto).DebugString());
    }
  }

  ROME_INFO("Done");
  return 0;
}