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
#include "benchmarks/qplock/experiment.pb.h"
#include "client.h"
#include "google/protobuf/text_format.h"
#include "rome/colosseum/client_adaptor.h"
#include "rome/colosseum/qps_controller.h"
#include "rome/colosseum/streams/streams.h"
#include "rome/colosseum/workload_driver.h"
#include "rome/logging/logging.h"
#include "rome/metrics/summary.h"
#include "rome/rdma/rdma_broker.h"
#include "rome/util/clocks.h"
#include "server.h"
#include "setup.h"
#include "src/node/cloudlab_node.h"
#include "src/node/cluster_config.h"
#include "src/node/connection_manager.h"

#define DECL_PROTO_FLAG(proto_type)                                          \
  bool AbslParseFlag(std::string_view text, proto_type* proto,               \
                     std::string* error) {                                   \
    auto ok =                                                                \
        ::google::protobuf::TextFormat::ParseFromString(text.data(), proto); \
    if (!ok) *error = "Failed to parse proto flag";                          \
    return ok;                                                               \
  }                                                                          \
  std::string AbslUnparseFlag(const proto_type& proto) {                     \
    return proto.DebugString();                                              \
  }

DECL_PROTO_FLAG(ExperimentParams);

ABSL_FLAG(std::string, nodes, {},
          "A list of strings representing nodes in the cluster");
ABSL_FLAG(ExperimentParams, experiment_params, {}, "Experimental parameters");

using ::util::SystemClock;
using ::X::NodefileClusterConfig;

std::function<void(int)> signal_handler_internal;
void signal_handler(int signum) { signal_handler_internal(signum); }
volatile bool done = false;

Peer MakePeer(const Node& n) {
  return {static_cast<uint16_t>(n.id()), std::string(n.private_hostname()),
          static_cast<uint16_t>(kBaseClientPort + n.id())};
}

int main(int argc, char* argv[]) {
  ROME_INIT_LOG();
  absl::ParseCommandLine(argc, argv);

  auto nodes = absl::GetFlag(FLAGS_nodes);
  auto experiment_params = absl::GetFlag(FLAGS_experiment_params);
  ROME_ASSERT_OK(ValidateExperimentParams(&experiment_params));

  if (!experiment_params.has_runtime() || experiment_params.runtime() < 0) {
    signal_handler_internal = std::function([](int signum) {
      if (done) exit(1);
      ROME_INFO("Shutting down...");
      done = true;
    });
    signal(SIGINT, signal_handler);
  }

  // For now we assume that the nodes are CloudLab nodes.
  NodefileClusterConfig<Node> config;
  ROME_ASSERT_OK(config.ParseNodestring(nodes));
  ROME_DEBUG(config.DebugString());

  // Convert the mode passed as a command line parameter to a `Role` to decide
  // what to run.
  Role mode_role;
  mode_role.ParseFromString(experiment_params.mode());
  auto cluster = config.GetCluster();

  // Update the experiment config based on the cluster configuration.
  experiment_params.set_cluster_size(cluster.size() - 1);
  experiment_params.set_num_clients(experiment_params.client_ids().size());

  switch (mode_role.type()) {
    case Role::Type::kServer: {
      Peer me;
      std::vector<Peer> peers;
      for (const auto& c : cluster) {
        auto node = c.second;
        auto p = MakePeer(node);
        if (node.role().type() == Role::Type::kServer) {
          me = p;
        }
        peers.push_back(p);
      }
      auto server = Server::Create(me, peers);
      auto status = server->Launch(&done, experiment_params.runtime());
      ROME_ASSERT_OK(status);
    } break;
    case Role::Type::kClient: {
      ROME_ASSERT_OK(ValidateClientExperimentParams(&experiment_params));

      Peer host;
      std::vector<Peer> peers;
      bool found = false;
      for (const auto& c : cluster) {
        auto node = c.second;
        auto p = MakePeer(node);
        if (node.role().type() == Role::Type::kServer) {
          host = p;
          found = true;
        }
        peers.push_back(p);
      }
      ROME_ASSERT(found, "No server found in the cluster config");

      // Launch each of the clients.
      std::vector<std::thread> client_threads;
      std::barrier client_barrier(experiment_params.client_ids().size());
      for (const auto& id : experiment_params.client_ids()) {
        auto iter = cluster.find(id);
        ROME_ASSERT(iter != cluster.end(), "Failed to locate node: {}", id);

        auto node = iter->second;
        client_threads.emplace_back([=, &client_barrier]() {
          auto client = McsLockClient::Create(
              Peer{static_cast<uint16_t>(node.id()),
                     std::string(node.private_hostname()),
                     static_cast<uint16_t>(kBaseClientPort + node.id())},
              host, peers, experiment_params, &client_barrier);
          ROME_ASSERT_OK(
              McsLockClient::Run(std::move(client), experiment_params, &done));
        });
      }

      // Join clients.
      for (auto& c : client_threads) {
        c.join();
      }
    } break;
    default:
      ROME_FATAL("Unknown mode: {}", experiment_params.mode());
  }

  ROME_INFO("Done");
  return 0;
}