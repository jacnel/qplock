#include <chrono>
#include <cstddef>
#include <thread>

#include "rome/logging/logging.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "qplock/cluster/cluster.pb.h"
#include "qplock/cluster/common.h"
#include "qplock/cluster/doorbell_messenger.h"
#include "qplock/cluster/server/handler_pool.h"
#include "qplock/datastore/quiescence.h"
#include "server.h"

namespace X {

using ::rome::rdma::MemoryPool;

inline static void cpu_relax() { asm volatile("pause\n" : : : "memory"); }

template <typename K, typename V>
Server<K, V>::~Server() {
  ds_->StopQuiescenceMonitor();
}

template <typename K, typename V>
Server<K, V>::Server(const ServerProto& server, const ClusterProto& cluster,
                     bool prefill, int num_handlers) {
  auto name = server.node().name();
  auto nid = server.node().nid();
  auto port = server.node().port();

  auto cm = std::make_unique<MemoryPool::cm_type>(nid);
  pool_ = std::make_unique<MemoryPool>(MemoryPool::Peer(nid, name, port),
                                       std::move(cm));

  std::vector<MemoryPool::Peer> peers;
  peers.reserve(cluster.clients().size());
  for (auto c : cluster.clients()) {
    peers.emplace_back(c.node().nid(), c.node().name(), c.node().port());
  }
  ROME_INFO("Clients: {}", peers.size());
  ROME_ASSERT_OK(pool_->Init(server.capacity(), peers));
  ds_ = std::make_unique<tree_type>(server.range().low(), server.range().high(),
                                    pool_.get());
  ROME_DEBUG("Root at {}", ds_->GetRemotePtr());

  ds_->StartQuiescenceMonitor();
  if (prefill)
    ROME_ASSERT_OK(Prefill(server.range().low(), server.range().high()));

  handler_pool_ = std::make_unique<RequestHandlerPool<key_type, value_type>>(
      num_handlers, ds_.get(), pool_.get());
  std::vector<ResponseProto> responses;
  for (const auto& c : cluster.clients()) {
    auto* conn = VALUE_OR_DIE(
        pool_->connection_manager()->GetConnection(c.node().nid()));
    auto req_or = conn->channel()->template Deliver<RequestProto>();
    RequestProto req = VALUE_OR_DIE(req_or);
    responses.push_back(handler_pool_->RegisterClient(req, conn));
  }

  for (auto i = 0; i < responses.size(); ++i) {
    auto c = cluster.clients(i);
    auto* conn = VALUE_OR_DIE(
        pool_->connection_manager()->GetConnection(c.node().nid()));
    ROME_ASSERT_OK(conn->channel()->Send(responses[i]));
  }
}

template <typename K, typename V>
absl::Status Server<K, V>::Prefill(const key_type& min_key,
                                   const key_type& max_key) {
  ds_->RegisterThisThread();
  ROME_INFO("Prefilling... [{}, {})", min_key, max_key);
  auto target_fill = (max_key - min_key) / 2;

  std::random_device rd;
  std::default_random_engine rand(rd());
  std::uniform_int_distribution<key_type> keys(min_key, max_key);
  std::uniform_int_distribution<value_type> values(
      std::numeric_limits<value_type>::min(),
      std::numeric_limits<value_type>::max());

  using std::chrono::system_clock;
  auto last = system_clock::now();
  size_t count = 0;
  while (count < target_fill) {
    auto k = keys(rand);
    auto v = values(rand);
    auto success = ds_->Insert(k, v);
    if (success) ++count;

    auto curr = system_clock::now();
    if (curr - last > std::chrono::seconds(1)) {
      ROME_INFO("Inserted {}/{} elements", count, target_fill);
      last = curr;
    }
  }
  ROME_INFO("Finished prefilling");

  return absl::OkStatus();
}

}  // namespace X