#pragma once

#include <random>

#include "rome/rdma/memory_pool/memory_pool.h"
#include "qplock/cluster/client/sharder.h"
#include "qplock/cluster/cluster.pb.h"
#include "qplock/cluster/common.h"
#include "qplock/cluster/doorbell_messenger.h"
#include "qplock/datastore/btree.h"

namespace X {

class Client {
  using MemoryPool = ::rome::rdma::MemoryPool;
  using tree_type = RemoteBTree<key_type, value_type, 1 << 12>;
  using root_type = tree_type::versioned_root_t;

  static constexpr int kPoolSize = 1 << 23;

 public:
  using result_set_type = std::map<key_type, value_type>;

  ~Client();
  Client(const ClientProto& client, const ClusterProto& cluster);

  void Connect();

  void Disconnect();

  bool GetOneSided(const RequestProto& req, uint64_t* value);
  bool GetOneSided(uint64_t key, uint64_t* value);

  bool Get(const RequestProto& req, uint64_t* value);
  bool Get(uint64_t key, uint64_t* value);

  bool Insert(const RequestProto& req);
  bool Insert(uint64_t key, uint64_t val, bool update = false);

  bool Delete(const RequestProto& req);
  bool Delete(uint64_t key);

  void ScanOneSided(const RequestProto& req, result_set_type* result);
  void ScanOneSided(uint64_t lo, uint64_t hi, result_set_type* result);

  ResponseProto Scan(const RequestProto& req);
  ResponseProto Scan(uint64_t lo, uint64_t hi);

  inline ClientProto ToProto() { return client_; }

 private:
  DoorbellMessenger* SendRequest(uint16_t server, const RequestProto& req);
  ResponseProto GetResponse(DoorbellMessenger* doorbell);

  const ClientProto client_;
  const ClusterProto cluster_;
  std::unique_ptr<MemoryPool> pool_;

  Sharder sharder_;

  static constexpr int kResultsBufferSize = 1 << 20;

  struct server_ctx_t {
    std::unique_ptr<tree_type> tree;
    MemoryPool::conn_type* conn;
    DoorbellMessenger doorbell;
    remote_ptr<uint8_t> scan_buffer;
  };
  std::map<uint32_t, server_ctx_t> servers_;

  std::random_device rd_;
  std::default_random_engine rand_;
};

}  // namespace X