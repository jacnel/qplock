load("@rules_cc//cc:defs.bzl", "cc_proto_library")
load("@com_google_protobuf//:protobuf.bzl", "py_proto_library")
load("//benchmarks/qplock:flags.bzl", "lock_type")

proto_library(
    name = "experiment_proto",
    srcs = ["experiment.proto"],
    deps = [
        "@rome//protos:colosseum_proto",
        "@rome//protos:metrics_proto",
    ],
)

cc_proto_library(
    name = "experiment_cc_proto",
    deps = [":experiment_proto"],
)

py_proto_library(
    name = "experiment_py_proto",
    srcs = [
        "experiment.proto",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@rome//protos:colosseum_py_proto",
        "@rome//protos:metrics_py_proto",
    ],
)

cc_library(
    name = "setup",
    hdrs = ["setup.h"],
    deps = [
        "@absl//absl/status",
        "@rome//rome/logging",
        "@rome//rome/rdma/channel:sync_accessor",
        "@rome//rome/util:status_util",
    ],
)

cc_library(
    name = "server",
    hdrs = ["server.h"],
    deps = [
        ":experiment_cc_proto",
        ":setup",
        "//src/node:connection_manager",
        "//src/qplock:rdma_mcs_lock",
        "//src/qplock:rdma_spin_lock",
        "@absl//absl/status",
        "@absl//absl/status:statusor",
    ],
)

cc_library(
    name = "client",
    hdrs = ["client.h"],
    deps = [
        ":experiment_cc_proto",
        ":setup",
        "@absl//absl/status",
        "@absl//absl/status:statusor",
    ],
)

filegroup(
    name = "nodefiles",
    srcs = glob(["nodefiles/**"]),
)

filegroup(
    name = "configfiles",
    srcs = glob(["configfiles/**"]),
)

filegroup(
    name = "experiments",
    srcs = glob(["experiments/**"]),
)

lock_type(
    name = "lock_type",
    build_setting_default = "mcs",
)

config_setting(
    name = "mcs_lock",
    flag_values = {
        ":lock_type": "mcs",
    },
)

config_setting(
    name = "spin_lock",
    flag_values = {
        ":lock_type": "spin",
    },
)

cc_binary(
    name = "main",
    srcs = ["main.cc"],
    copts = ["-Wno-deprecated-enum-enum-conversion"],
    data = [":nodefiles"],
    defines = select(
        {
            ":mcs_lock": ["QPLOCK_LOCK_TYPE=::X::RdmaMcsLock"],
            ":spin_lock": ["QPLOCK_LOCK_TYPE=::X::RdmaSpinLock"],
        },
    ),
    deps = [
        ":client",
        ":experiment_cc_proto",
        ":server",
        ":setup",
        "//src/node:cloudlab_node",
        "//src/node:cluster_config",
        "//src/node:connection_manager",
        "@absl//absl/flags:flag",
        "@absl//absl/flags:parse",
        "@absl//absl/status",
        "@absl//absl/status:statusor",
        "@rome//rome/colosseum:client_adaptor",
        "@rome//rome/colosseum:qps_controller",
        "@rome//rome/colosseum/streams:streams",
        "@rome//rome/colosseum:workload_driver",
        "@rome//rome/logging",
        "@rome//rome/metrics:summary",
    ],
)

# py_binary(
#     name = "launch",
#     srcs = ["launch.py"],
#     data = [
#         ":configfiles",
#         ":experiments",
#         ":nodefiles",
#     ],
#     deps = [
#         ":experiment_py_proto",
#     ],
# )

# # We assume that data is saved to a directory within this subdirectory called `results`.
# filegroup(
#     name = "results",
#     srcs = glob(["results/**"]),
# )

# py_binary(
#     name = "plot",
#     srcs = ["plot.py"],
#     data = [
#         ":experiments",
#         ":results",
#         "@rome//protos:colosseum_py_proto",
#         "@rome//protos:metrics_py_proto",
#     ],
#     deps = [
#         ":experiment_py_proto",
#     ],
# )
