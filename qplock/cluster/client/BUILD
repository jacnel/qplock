package(default_visibility = ["//qplock:__subpackages__"])

cc_library(
    name = "sharder",
    hdrs = ["sharder.h"],
    deps = [
        "//qplock/cluster:cluster_cc_proto",
        "//qplock/cluster:common",
    ],
)

cc_library(
    name = "client",
    srcs = ["client.cc"],
    hdrs = ["client.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":sharder",
        "//qplock/cluster:cluster_cc_proto",
        "//qplock/cluster:common",
        "//qplock/cluster:doorbell_messenger"
        "@absl//absl/flags:flag",
        "@rome//rome/logging",
        "@rome//rome/rdma/memory_pool",
    ],
)