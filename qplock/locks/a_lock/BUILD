cc_library(
    name = "alock-common",
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
        ":alock-common",
        ":lock_handle",
        ":remote_lock_handle",
        "@rome//rome/rdma:rdma_memory",
        "@rome//rome/rdma/channel:sync_accessor",
        "@rome//rome/rdma/connection_manager",
        "@rome//rome/rdma/memory_pool",
        "@rome//rome/rdma/memory_pool:remote_ptr",
    ],
)

cc_library(
    name = "lock_handle",
    srcs = ["lock_handle.cc"],
    hdrs = ["lock_handle.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":alock-common",
        "@rome//rome/rdma:rdma_memory",
        "@rome//rome/rdma/channel:sync_accessor",
        "@rome//rome/rdma/connection_manager",
        "@rome//rome/rdma/memory_pool",
        "@rome//rome/rdma/memory_pool:remote_ptr",
    ],
)

cc_library(
    name = "remote_lock_handle",
    srcs = ["remote_lock_handle.cc"],
    hdrs = ["remote_lock_handle.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":alock-common",
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