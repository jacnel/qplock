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
        "@rome//rome/rdma:rdma_memory",
        "@rome//rome/rdma/channel:sync_accessor",
        "@rome//rome/rdma/connection_manager",
        "@rome//rome/rdma/memory_pool",
        "@rome//rome/rdma/memory_pool:remote_ptr",
    ],
)

cc_library(
    name = "rdma_spin_lock",
    srcs = ["rdma_spin_lock.cc"],
    hdrs = ["rdma_spin_lock.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":util",
        "@rome//rome/rdma:rdma_memory",
        "@rome//rome/rdma/channel:sync_accessor",
        "@rome//rome/rdma/connection_manager",
        "@rome//rome/rdma/memory_pool",
        "@rome//rome/rdma/memory_pool:remote_ptr",
    ],
)

cc_library(
    name = "mcs_lock",
    srcs = ["mcs_lock.cc"],
    hdrs = ["mcs_lock.h"],
    visibility = ["//visibility:public"],
    deps = [":util",
            "@rome//rome/logging",
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

cc_test(
    name = "rdma_spin_lock_test",
    srcs = ["rdma_spin_lock_test.cc"],
    deps = [
        ":rdma_spin_lock",
        "@gtest//:gtest_main",
    ],
)

cc_test(
    name = "mcs_lock_test",
    srcs = ["mcs_lock_test.cc"],
    deps = [
        ":mcs_lock",
        "@gtest//:gtest_main",
    ],
)
