#pragma once

#include <algorithm>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "qplock/benchmark/baseline/experiment.pb.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "setup.h"

class Server {
public:
  ~Server() = default;

  static void signal_handler(int signum) { 
    ROME_INFO("HANDLER!!!\n");
    // Wait for all clients to be done shutting down
    std::this_thread::sleep_for(std::chrono::seconds(10));
    exit(1);
  }

  static std::unique_ptr<Server> Create(Peer server,
                                        std::vector<Peer> clients) {
    return std::unique_ptr<Server>(new Server(server, clients));
  }

  absl::Status Launch(volatile bool *done, int runtime_s) {
    ROME_DEBUG("Starting server...");

    signal(SIGINT, signal_handler);
    
    auto status =
        lock_.Init(self_, peers_); // Starts `cm_` and connects to peers
    ROME_CHECK_OK(ROME_RETURN(status), status);

    // Sleep while clients are running if there is a set runtime.
    if (runtime_s > 0) {
      auto runtime = std::chrono::seconds();
      std::this_thread::sleep_for(runtime);
      *done = true; // Just run once
    }

    // Wait for all clients to be done.
    for (auto &p : peers_) {
      auto conn_or = pool_.connection_manager()->GetConnection(p.id);
      if (!conn_or.ok())
        return conn_or.status();

      auto *conn = conn_or.value();
      auto msg = conn->channel()->TryDeliver<AckProto>();
      while ((!msg.ok() &&
              msg.status().code() == absl::StatusCode::kUnavailable)) {
        msg = conn->channel()->TryDeliver<AckProto>();
      }
    }
    return absl::OkStatus();
  }

private:
  Server(Peer self, std::vector<Peer> peers)
      : self_(self), peers_(peers),
        pool_(self_, std::make_unique<cm_type>(self.id)), lock_(self_, pool_) {}

  const Peer self_;
  std::vector<Peer> peers_;
  X::MemoryPool pool_;
  LockType lock_;
};

class ServerHarness {
 public:
  ~ServerHarness() = default;

  static std::unique_ptr<ServerHarness> Create(
      std::unique_ptr<X::Server<key_type, value_type>> server,
      const X::NodeProto& node, ExperimentParams params) {
    return std::unique_ptr<ServerHarness>(
        new ServerHarness(std::move(server), node, params));
  }

  absl::Status Launch(volatile bool* done, ExperimentParams experiment_params) {
    std::vector<std::unique_ptr<Worker>> workers;
    for (auto i = 0; i < experiment_params.workload().worker_threads(); ++i) {
      workers.emplace_back(
          Worker::Create(server_->GetDatastore(), node_, params_, &barrier_));
    }

    std::for_each(workers.begin(), workers.end(), [&](auto& worker) {
      results_.emplace_back(std::async([&]() {
        return Worker::Run(std::move(worker), experiment_params, done);
      }));
    });

    std::for_each(results_.begin(), results_.end(),
                  [](auto& result) { result.wait(); });

    for (auto& r : results_) {
      auto result_or = r.get();
      if (!result_or.ok()) {
        ROME_ERROR("{}", result_or.status().message());
      } else {
        result_protos_.push_back(result_or.value());
      }
    }

    ROME_DEBUG("Waiting for clients to disconnect (in destructor)...");
    server_.reset();
    return absl::OkStatus();
  }

  std::vector<ResultProto> GetResults() { return result_protos_; }

 private:
  static constexpr size_t kNumEntries = 1 << 12;

  ServerHarness(std::unique_ptr<X::Server<key_type, value_type>> server,
                const X::NodeProto& node, ExperimentParams params)
      : server_(std::move(server)),
        node_(node),
        params_(params),
        barrier_(params.workload().worker_threads()) {}

  std::unique_ptr<X::Server<key_type, value_type>> server_;
  const X::NodeProto& node_;
  ExperimentParams params_;

  std::barrier<> barrier_;
  std::vector<std::future<absl::StatusOr<ResultProto>>> results_;
  std::vector<ResultProto> result_protos_;
};