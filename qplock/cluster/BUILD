load("@rules_cc//cc:defs.bzl", "cc_proto_library")
load("@com_google_protobuf//:protobuf.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "cluster_proto",
    srcs = ["cluster.proto"],
)

cc_proto_library(
    name = "cluster_cc_proto",
    deps = ["cluster_proto"],
)

py_proto_library(
    name = "cluster_py_proto",
    srcs = ["cluster.proto"],
)

# cc_library(
#     name = "cluster",
#     srcs = ["cluster.cc"],
#     hdrs = ["cluster.h"],
#     deps = [
#         ":cluster_cc_proto",
#         "@absl//absl/status",
#         "@rome//rome/logging",
#         "@rome//rome/util:status_util",
#     ],
# )

cc_library(
    name = "common",
    hdrs = ["common.h"],
    deps = [
        "//qplock/datastore:policy",
        "@rome//rome/logging",
    ],
)

cc_test(
    name = "client_server_test",
    srcs = ["client_server_test.cc"],
    deps = [
        "//qplock/cluster:common",
        "//qplock/cluster/client",
        "//qplock/cluster/server",
        "@gtest//:gtest_main",
    ],
)

cc_library(
    name = "doorbell_messenger",
    srcs = ["doorbell_messenger.cc"],
    hdrs = ["doorbell_messenger.h"],
    deps = [
        "//qplock/cluster:cluster_cc_proto",
        "@rome//rome/rdma/memory_pool",
        "@rome//rome/rdma/memory_pool:remote_ptr",
    ],
)

cc_test(
    name = "doorbell_messenger_test",
    srcs = ["doorbell_messenger_test.cc"],
    deps = [
        ":doorbell_messenger",
        "//qplock/cluster:cluster_cc_proto",
        "@gtest//:gtest_main",
        "@rome//rome/logging",
        "@rome//rome/rdma/memory_pool",
        "@rome//rome/rdma/memory_pool:remote_ptr",
    ],
)