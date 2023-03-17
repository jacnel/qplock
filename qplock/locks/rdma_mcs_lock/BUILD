cc_library(
    name = "rdma_mcs_lock",
    srcs = ["rdma_mcs_lock.cc"],
    hdrs = ["rdma_mcs_lock.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//qplock:util",
        "@rome//rome/rdma:rdma_memory",
        "@rome//rome/rdma/channel:sync_accessor",
        "@rome//rome/rdma/connection_manager",
        "@rome//rome/rdma/memory_pool",
        "@rome//rome/rdma/memory_pool:remote_ptr",
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