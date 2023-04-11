load("@bazel_skylib//rules:common_settings.bzl", "string_flag")

package(default_visibility = ["//qplock:__subpackages__"])

string_flag(
    name = "remote_only",
    build_setting_default = "false",
    values = [
        "true",
        "false",
    ],
    visibility = ["//visibility:public"],
)

config_setting(
    name = "remote_only_true",
    flag_values = {":remote_only": "true"},
)

config_setting(
    name = "remote_only_false",
    flag_values = {":remote_only": "false"},
)

cc_library(
    name = "handler_pool",
    hdrs = ["handler_pool.h"],
    deps = [
        "//qplock/cluster:cluster_cc_proto",
        "//qplock/cluster:doorbell_messenger",
        "//qplock/datastore:btree",
        "@absl//absl/synchronization",
        "@rome//rome/rdma/memory_pool",
        "@rome//rome/rdma/memory_pool:remote_ptr",
    ],
)

cc_test(
    name = "handler_pool_test",
    srcs = ["handler_pool_test.cc"],
    defines = select({
        ":no_rq_response_true": ["qplock_NO_RQ_RESPONSE"],
        ":no_rq_response_false": [],
    }),
    deps = [
        ":handler_pool",
        "//qplock/cluster:cluster_cc_proto",
        "//qplock/cluster/client",
        "//qplock/datastore:btree",
        "@gtest//:gtest_main",
        "@rome//rome/rdma/memory_pool",
        "@rome//rome/rdma/memory_pool:remote_ptr",
    ],
)

cc_library(
    name = "server_impl",
    hdrs = ["server_impl.h"],
    deps = [
        ":handler_pool",
        "//qplock/cluster:cluster_cc_proto",
        "//qplock/cluster:common",
        "@rome//rome/logging",
    ],
)

cc_library(
    name = "server",
    hdrs = ["server.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":handler_pool",
        ":server_impl",
        "//qplock/cluster:cluster_cc_proto",
        "//qplock/cluster:common",
        "//qplock/cluster:doorbell_messenger",
        "//qplock/datastore:btree",
        "@absl//absl/strings",
    ],
)