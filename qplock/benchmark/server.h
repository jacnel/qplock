#pragma once

#include <algorithm>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "benchmarks/qplock/experiment.pb.h"
#include "benchmarks/qplock/setup.h"
#include "src/node/connection_manager.h"
#include "src/node/memory_pool.h"
#include "src/qplock/rdma_mcs_lock.h"
#include "src/qplock/rdma_spin_lock.h"

class Server {
 public:
  ~Server() = default;

  static std::unique_ptr<Server> Create(Peer server,
                                        std::vector<Peer> clients) {
    return std::unique_ptr<Server>(new Server(server, clients));
  }

  absl::Status Launch(volatile bool* done, int runtime_s) {
    ROME_DEBUG("Starting server...");
    lock_ = std::make_unique<LockType>(self_, &cm_);
    auto status =
        lock_->Init(self_, peers_);  // Starts `cm_` and connects to peers
    ROME_CHECK_OK(ROME_RETURN(status), status);

    // Sleep while clients are running if there is a set runtime.
    if (runtime_s > 0) {
      auto runtime = std::chrono::seconds();
      std::this_thread::sleep_for(runtime);
      *done = true;  // Just run once
    }

    // Wait for all clients to be done.
    for (auto& p : peers_) {
      auto conn_or = cm_.GetConnection(p.id);
      if (!conn_or.ok()) return conn_or.status();

      auto* conn = conn_or.value();
      auto msg = conn->channel()->TryDeliver<Status>();
      while ((!msg.ok() &&
              msg.status().code() == absl::StatusCode::kUnavailable) ||
             msg->state() != State::DONE) {
        msg = conn->channel()->TryDeliver<Status>();
      }
    }
    return absl::OkStatus();
  }

 private:
  Server(Peer self, std::vector<Peer> peers)
      : self_(self), peers_(peers), cm_(self.id) {}

  const Peer self_;
  std::vector<Peer> peers_;
  X::MemoryPool::cm_type cm_;
  std::unique_ptr<LockType> lock_;
};