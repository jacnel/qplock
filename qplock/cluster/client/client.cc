#include "client.h"

#include <random>

#include "absl/flags/flag.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "qplock/cluster/cluster.pb.h"
#include "qplock/cluster/common.h"
#include "qplock/cluster/doorbell_messenger.h"
#include "qplock/datastore/policy.h"

namespace X {
using ::rome::rdma::MemoryPool;

Client::~Client() {
  // Disconnect();
}

Client::Client(const ClientProto& client, const ClusterProto& cluster)
    : client_(client),
      cluster_(cluster),
      sharder_(cluster),
      rd_(),
      rand_(rd_()) {
  auto nid = client.node().nid();
  auto name = client.node().name();
  auto port = client.node().port();

  auto peer = MemoryPool::Peer(nid, name, port);
  auto cm = std::make_unique<MemoryPool::cm_type>(peer.id);
  pool_ = std::make_unique<MemoryPool>(peer, std::move(cm));
}

void Client::Connect() {
  // Init `MemoryPool`
  std::vector<MemoryPool::Peer> peers;
  peers.reserve(cluster_.servers().size());
  for (auto s : cluster_.servers()) {
    auto p =
        MemoryPool::Peer(s.node().nid(), s.node().name(), s.node().port());
    peers.push_back(p);
  }
  ROME_ASSERT_OK(pool_->Init(kPoolSize, peers));

  for (auto s : cluster_.servers()) {
    auto tree = std::make_unique<tree_type>(s.range().low(), s.range().high(),
                                            pool_.get(), remote_nullptr);
    auto* conn = VALUE_OR_DIE(
        pool_->connection_manager()->GetConnection(s.node().nid()));
    DoorbellMessenger doorbell(pool_.get());

    RequestProto req;
    auto* init = req.mutable_init();
    init->set_quiescence(static_cast<uint64_t>(tree->GetQuiescencePtr()));
    init->set_doorbell(static_cast<uint64_t>(doorbell.GetRxRemotePtr()));
    ROME_ASSERT_OK(conn->channel()->Send(req));

    auto resp = VALUE_OR_DIE(conn->channel()->Deliver<ResponseProto>());
    auto root = remote_ptr<root_type>(resp.init().root());
    ROME_INFO("Got root: {}", root);

    tree->SetRoot(root);
    doorbell.PrepareSend(remote_ptr<RingableBuffer>(resp.init().doorbell()));

    remote_ptr<uint8_t> scan_buffer =
        pool_->Allocate<uint8_t>(kResultsBufferSize);
    ROME_ASSERT(scan_buffer != remote_nullptr, "Failed to allocated buffer for scan results");
    servers_.emplace(
        s.node().nid(),
        server_ctx_t{std::move(tree), conn, std::move(doorbell), scan_buffer});
  }
}

DoorbellMessenger* Client::SendRequest(uint16_t server,
                                       const RequestProto& req) {
  ROME_DEBUG("Sending request to {}: {}", server, req.DebugString());
  ROME_ASSERT_DEBUG(req.ByteSizeLong() <= RingableBuffer::size, "WTF");
  auto& doorbell = servers_.at(server).doorbell;
  auto* tx = doorbell.GetRingableTxBuffer();
  req.SerializeToArray(tx->buffer, req.ByteSizeLong());
  doorbell.Send(req.GetCachedSize());
  return &doorbell;
}

ResponseProto Client::GetResponse(DoorbellMessenger* doorbell) {
  uint8_t* rx;
  uint64_t bytes = doorbell->TryDeliver(&rx);
  while (bytes == 0) {
    cpu_relax();  // Chill...
    bytes = doorbell->TryDeliver(&rx);
  }
  ResponseProto resp;
  resp.ParseFromArray(rx, bytes);
  ROME_TRACE("Got response: (bytes={}) {}", bytes, resp.DebugString());
  return resp;
}

void Client::Disconnect() {
  RequestProto req;
  req.mutable_disc();
  for (auto s : cluster_.servers()) {
    auto* doorbell = SendRequest(s.node().nid(), req);
    GetResponse(doorbell);
  }
}

bool Client::GetOneSided(const RequestProto& req, uint64_t* value) {
  uint32_t server = sharder_.GetShard(req.get().key());
  auto* tree = servers_.at(server).tree.get();
  return tree->Get(req.get().key(), value);
}

bool Client::GetOneSided(uint64_t key, uint64_t* value) {
  uint32_t server = sharder_.GetShard(key);
  auto* tree = servers_.at(server).tree.get();
  return tree->Get(key, value);
}

bool Client::Get(const RequestProto& req, uint64_t* value) {
  uint32_t server = sharder_.GetShard(req.get().key());
  auto* doorbell = SendRequest(server, req);
  auto resp = GetResponse(doorbell);
  if (resp.get().has_value()) *value = resp.get().value();
  return resp.get().has_value();
}

bool Client::Get(uint64_t key, uint64_t* value) {
  RequestProto req;
  auto* get = req.mutable_get();
  get->set_key(key);
  return Get(req, value);
}

bool Client::Insert(const RequestProto& req) {
  uint32_t server = sharder_.GetShard(req.ins().key());
  auto* doorbell = SendRequest(server, req);
  auto resp = GetResponse(doorbell);
  return resp.ins().ok();
}

bool Client::Insert(uint64_t key, uint64_t val, bool update) {
  RequestProto req;
  auto* ins = req.mutable_ins();
  ins->set_key(key);
  ins->set_value(val);
  ins->set_update(update);
  return Insert(req);
}

bool Client::Delete(const RequestProto& req) {
  uint32_t server = sharder_.GetShard(req.del().key());
  auto* doorbell = SendRequest(server, req);
  auto resp = GetResponse(doorbell);
  return resp.del().ok();
}

bool Client::Delete(uint64_t key) {
  RequestProto req;
  auto* del = req.mutable_del();
  del->set_key(key);
  return Delete(req);
}

void Client::ScanOneSided(const RequestProto& req, result_set_type* result) {
  const auto lo = req.scan().range().low();
  const auto hi = req.scan().range().high();
  uint32_t server = sharder_.GetShard(lo);
  auto* tree = servers_.at(server).tree.get();
  tree->GetRange(lo, hi, result);
}

void Client::ScanOneSided(uint64_t lo, uint64_t hi, result_set_type* result) {
  RequestProto req;
  auto* scan = req.mutable_scan();
  scan->mutable_range()->set_low(lo);
  scan->mutable_range()->set_high(hi);
  ScanOneSided(req, result);
}

ResponseProto Client::Scan(const RequestProto& req) {
  const auto lo = req.scan().range().low();
  const auto hi = req.scan().range().high();
  if (sharder_.GetShard(lo) != sharder_.GetShard(hi)) [[unlikely]] {
    ROME_WARN("Multi-partition scan: [{}, {}]", lo, hi);
  }

  uint32_t server = sharder_.GetShard(lo);
  auto mutable_req = const_cast<RequestProto&>(req);

  auto& messenger = servers_.at(server).doorbell;
  mutable_req.mutable_scan()->set_rx(messenger.GetRxRemotePtr().raw());
  mutable_req.mutable_scan()->set_doorbell(
      messenger.GetRxDoorbellRemotePtr().raw());

  auto buffer = servers_.at(server).scan_buffer;
  mutable_req.mutable_scan()->set_dest(buffer.raw());
  mutable_req.mutable_scan()->set_reserved(kResultsBufferSize);

  auto* doorbell = SendRequest(server, mutable_req);
  auto resp = GetResponse(doorbell);
  ROME_DEBUG("Received: {}", resp.scan().count());
  return resp;
}

ResponseProto Client::Scan(uint64_t lo, uint64_t hi) {
  RequestProto req;
  auto* scan = req.mutable_scan();
  scan->mutable_range()->set_low(lo);
  scan->mutable_range()->set_high(hi);
  return Scan(req);
}

}  // namespace X