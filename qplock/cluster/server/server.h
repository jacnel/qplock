pragma once

#include <cstdint>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "qplock/cluster/cluster.pb.h"
#include "qplock/cluster/common.h"
#include "qplock/cluster/doorbell_messenger.h"
#include "qplock/cluster/server/handler_pool.h"
#include "qplock/datastore/btree.h"

namespace X {

template <typename K, typename V>
class Server {
  using key_type = K;
  using lock_type = V;
  using MemoryPool = rome::rdma::MemoryPool;

 public:
  using ds_type = std::unordered_map<K, std::unique_ptr<V>>;

  ~Server();
  Server(const ServerProto& server, const ClusterProto& cluster,
         bool prefill = false, int num_handlers = 16);

  absl::Status Prefill(const key_type& min_key, const key_type& max_key);

  tree_type* GetDatastore() { return ds_.get(); }

 private:
  void HandleRequest(const RequestProto& req);

  std::unique_ptr<MemoryPool> pool_;
  std::unique_ptr<ds_type> ds_;
  std::unordered_map<uint16_t, std::unique_ptr<RdmaMcsLock>> locks_;
  std::unique_ptr<RequestHandlerPool<key_type, value_type>> handler_pool_;

  // static inline thread_local struct client_ctx_t {
  //   int cid;
  //   MemoryPool::conn_type* conn;
  //   bool connected;
  //   DoorbellMessenger* doorbell;
  //   remote_ptr<RingableBuffer> dest = remote_nullptr;
  //   std::barrier<>* init_barrier;
  // } ctx_;

  // std::barrier<> init_barrier_;
  // std::vector<std::thread> threads_;
};

}  // namespace X

#include "server_impl.h"