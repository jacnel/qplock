cc_library(
    name = "util",
    hdrs = ["util.h"],
    deps = [],
)

cc_library(
    name = "alock-common",
    hdrs = ["a_lock.h"],
    deps = [
        ":util",
        "@rome//rome/rdma/memory_pool",
    ],
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
            "@rome//rome/util:thread_util",
    ],
)

cc_library(
    name = "a_lock",
    srcs = ["a_lock_handle.cc"],
    hdrs = ["a_lock_handle.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":util",
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
        ":util",
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
        ":util",
        ":alock-common",
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

cc_test(
    name = "a_lock_test",
    srcs = ["a_lock_test.cc"],
    deps = [
        ":a_lock",
        "@gtest//:gtest_main",
    ],
)
