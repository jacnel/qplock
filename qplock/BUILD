cc_library(
    name = "util",
    hdrs = ["util.h"],
    deps = [],
)

cc_library(
    name = "rdma_mcs_lock",
    srcs = ["rdma_mcs_lock.cc"],
    hdrs = ["rdma_mcs_lock.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":util",
        "//src/node:connection_manager",
        "//src/node:memory_pool",
        "@rome//rome/rdma:rdma_memory",
        "@rome//rome/rdma/channel:sync_accessor",
    ],
)

cc_test(
    name = "rdma_mcs_lock_test",
    srcs = ["rdma_mcs_lock_test.cc"],
    deps = [
        ":rdma_mcs_lock",
        "@gtest//:gtest_main",
    ],
)

cc_library(
    name = "rdma_spin_lock",
    visibility = ["//visibility:public"],
    hdrs = ["rdma_spin_lock.h"],
    deps = ["@absl//absl/status"],
)
