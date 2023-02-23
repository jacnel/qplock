pragma once

#include "qplock/cluster/cluster.pb.h"
#include "qplock/cluster/common.h"

namespace X {

class Sharder {
 public:
  explicit Sharder(const ClusterProto& cluster) {
    for (auto server : cluster.servers()) {
      shards_.emplace(server.range().low(), server.node().nid());
    }
  }

  uint32_t GetShard(const key_type& key) {
    auto iter =  shards_.lower_bound(key);
    ROME_ASSERT(iter != shards_.end(), "Failed to lookup shard for key {}", key);
    return iter->second;
  }

 private:
  std::map<key_type, uint32_t, std::greater<key_type>> shards_;
};

}  // namespace X
Footer
