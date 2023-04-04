load("@rules_cc//cc:defs.bzl", "cc_proto_library")
load("@com_google_protobuf//:protobuf.bzl", "py_proto_library")
load("@bazel_skylib//rules:common_settings.bzl", "string_flag")

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
        "//qplock/locks/rdma_mcs_lock:rdma_mcs_lock",
        "//qplock/locks/spin_lock:rdma_spin_lock",
        "//qplock/locks/a_lock:a_lock",
        "@absl//absl/status",
        "@absl//absl/status:statusor",
        "@rome//rome/rdma/connection_manager",
    ],
)

cc_library(
    name = "client",
    hdrs = ["client.h"],
    deps = [
        ":experiment_cc_proto",
        ":setup",
        "//qplock/locks/rdma_mcs_lock:rdma_mcs_lock",
        "//qplock/locks/spin_lock:rdma_spin_lock",
        "//qplock/locks/a_lock:a_lock",
        "@absl//absl/status",
        "@absl//absl/status:statusor",
    ],
)

string_flag(
    name = "lock_type",
    build_setting_default = "mcs",
    values = [
        "mcs",
        "spin",
        "alock",
    ],
)

config_setting(
    name = "alock",
    flag_values = {
        ":lock_type": "alock",
    },
)

config_setting(
    name = "mcs",
    flag_values = {
        ":lock_type": "mcs",
    },
)

config_setting(
    name = "spin",
    flag_values = {
        ":lock_type": "spin",
    },
)

cc_binary(
    name = "main",
    srcs = ["main.cc"],
    copts = ["-Wno-deprecated-enum-enum-conversion"],
    data = ["//qplock/benchmark:nodefiles"],
    defines = select(
        {
            ":mcs": ["LOCK_TYPE=::X::RdmaMcsLock"],
            ":spin": ["LOCK_TYPE=::X::RdmaSpinLock"],
            ":alock": ["LOCK_TYPE=::X::ALockHandle"],
        },
    ),
    deps = [
        ":client",
        ":experiment_cc_proto",
        ":server",
        "@absl//absl/flags:flag",
        "@absl//absl/flags:parse",
        "@absl//absl/status",
        "@absl//absl/status:statusor",
        "@rome//rome/colosseum:client_adaptor",
        "@rome//rome/colosseum:qps_controller",
        "@rome//rome/colosseum:workload_driver",
        "@rome//rome/colosseum/streams",
        "@rome//rome/logging",
        "@rome//rome/metrics:summary",
        "@rome//rome/rdma/connection_manager",
        "@rome//rome/util:proto_util",
    ],
)

# We assume that data is saved to a directory within this subdirectory called `results`.
filegroup(
    name = "results",
    srcs = glob(["results/**"]),
)

py_binary(
    name = "launch",
    srcs = ["launch.py"],
    data = [
        "//qplock/benchmark:nodefiles",
    ],
    deps = [
        ":experiment_py_proto",
        ":plot",
        "@rome//rome/util:debugpy_util",
    ],
)

py_binary(
    name = "plot",
    srcs = ["plot.py"],

    deps = [
        ":experiment_py_proto",
        "@rome//protos:colosseum_py_proto",
        "@rome//protos:metrics_py_proto",
    ],
)
