#pragma once

#include <barrier>
#include <chrono>
#include <filesystem>
#include <memory>
#include <unordered_map>

#include "qplock/rdma_mcs_lock.h"
#include "qplock/rdma_spin_lock.h"
#include "rome/colosseum/qps_controller.h"
#include "rome/colosseum/streams/streams.h"
#include "rome/colosseum/workload_driver.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/util/clocks.h"
#include "setup.h"

using ::rome::rdma::MemoryPool;

class Client : public ClientAdaptor<rome::NoOp> {
public:
  static std::unique_ptr<Client>
  Create(const Peer &self, const Peer &server, const std::vector<Peer> &peers,
         const ExperimentParams &experiment_params, std::barrier<> *barrier) {
    return std::unique_ptr<Client>(
        new Client(self, server, peers, experiment_params, barrier));
  }

  static absl::StatusOr<ResultProto>
  Run(std::unique_ptr<Client> client, const ExperimentParams &experiment_params,
      volatile bool *done) {
    // Setup qps_controller.
    std::unique_ptr<rome::LeakyTokenBucketQpsController<util::SystemClock>>
        qps_controller;
    if (experiment_params.has_max_qps() && experiment_params.max_qps() > 0) {
      qps_controller =
          rome::LeakyTokenBucketQpsController<util::SystemClock>::Create(
              experiment_params.max_qps());
    }

    auto *client_ptr = client.get();

    // Create and start the workload driver (also starts client).
    auto driver = rome::WorkloadDriver<rome::NoOp>::Create(
        std::move(client), std::make_unique<rome::NoOpStream>(),
        qps_controller.get(),
        std::chrono::milliseconds(experiment_params.sampling_rate_ms()));
    ROME_ASSERT_OK(driver->Start());

    // Sleep while driver is running then stop it.
    if (experiment_params.has_runtime() && experiment_params.runtime() > 0) {
      ROME_INFO("Running workload for {}s", experiment_params.runtime());
      auto runtime = std::chrono::seconds(experiment_params.runtime());
      std::this_thread::sleep_for(runtime);
    } else {
      ROME_INFO("Running workload indefinitely");
      while (!(*done)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      }
    }
    ROME_INFO("Stopping client...");
    ROME_ASSERT_OK(driver->Stop());

    // Output results.
    ResultProto result;
    result.mutable_experiment_params()->CopyFrom(experiment_params);
    result.mutable_client()->CopyFrom(client_ptr->ToProto());
    result.mutable_driver()->CopyFrom(driver->ToProto());

    // if (experiment_params.has_save_dir()) {
    //   auto save_dir = experiment_params.save_dir();
    //   if (save_dir.empty()) {
    //     save_dir = client_ptr->self_.address;
    //   }

    //   while (!std::filesystem::exists(save_dir) &&
    //          !std::filesystem::create_directories(save_dir)) {
    //     std::this_thread::sleep_for(std::chrono::milliseconds(500));
    //   }

    //   std::ofstream file;
    //   std::filesystem::path outfile;
    //   outfile /= save_dir;
    //   outfile /= BuildResultName(client_ptr->self_.id, experiment_params);
    //   file.open(outfile);
    //   ROME_ASSERT(file.is_open(), "Failed to open output file: {}",
    //               outfile.c_str());
    //   file << result.DebugString();
    //   file.close();
    // } else {
    //   std::cout << result.DebugString();
    // }

    // Sleep for a hot sec to let the server receive the messages sent by the
    // clients before disconnecting.
    // (see https://github.com/jacnel/project-x/issues/15)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return result;
  }

  absl::Status Start() override {
    ROME_INFO("Starting client...");
    auto status = lock_.Init(host_, peers_);
    barrier_->arrive_and_wait();
    return status;
  }

  absl::Status Apply(const rome::NoOp &op) override {
    lock_.Lock();
    auto start = util::SystemClock::now();
    if (experiment_params_.has_think_time_us()) {
      while (util::SystemClock::now() - start <
             std::chrono::microseconds(experiment_params_.think_time_us()))
        ;
    }
    lock_.Unlock();
    return absl::OkStatus();
  }

  absl::Status Stop() override {
    // Announce done.
    auto conn = pool_.connection_manager()->GetConnection(host_.id);
    ROME_CHECK_OK(ROME_RETURN(util::InternalErrorBuilder()
                              << "Failed to retrieve server connection"),
                  conn);
    auto e = AckProto();
    auto sent = conn.value()->channel()->Send(e);

    // Wait for all other clients.
    barrier_->arrive_and_wait();
    return absl::OkStatus();
  }

  NodeProto ToProto() {
    NodeProto client;
    *client.mutable_host() = self_.address;
    client.set_id(self_.id);
    client.set_port(self_.port);
    return client;
  }

private:
  Client(const Peer &self, const Peer &host, const std::vector<Peer> &peers,
         const ExperimentParams &experiment_params, std::barrier<> *barrier)
      : experiment_params_(experiment_params), self_(self), host_(host),
        peers_(peers), barrier_(barrier),
        pool_(self_, std::make_unique<cm_type>(self.id)), lock_(self_, pool_) {}

  static std::string
  BuildResultName(uint32_t id, const ExperimentParams &experiment_params) {
    std::stringstream ss;
    ss << experiment_params.name() << "-";
    ss << "i" << id;
    ss << "_d" << experiment_params.runtime();
    if (experiment_params.has_max_qps() && experiment_params.max_qps() > 0) {
      ss << "_q" << experiment_params.max_qps();
    }
    ss << ".pbtxt";
    return ss.str();
  }

  ExperimentParams experiment_params_;

  const Peer self_;
  const Peer host_;
  std::vector<Peer> peers_;
  std::barrier<> *barrier_;

  MemoryPool pool_;
  LockType lock_;
};