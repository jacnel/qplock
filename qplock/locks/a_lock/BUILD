cc_library(
    name = "alock_common",
    hdrs = ["a_lock.h"],
    deps = [
        "//qplock:util",
        "@rome//rome/rdma/memory_pool",
    ],
)

cc_library(
    name = "a_lock",
    srcs = ["a_lock_handle.cc"],
    hdrs = ["a_lock_handle.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":alock_common",
        "@rome//rome/rdma:rdma_memory",
        "@rome//rome/rdma/channel:sync_accessor",
        "@rome//rome/rdma/connection_manager",
        "@rome//rome/rdma/memory_pool",
        "@rome//rome/rdma/memory_pool:remote_ptr",
    ],
)

cc_test(
    name = "a_lock_test",
    srcs = ["a_lock_test.cc"],
    deps = [
        ":a_lock",
        "@gtest//:gtest_main",
    ],
)