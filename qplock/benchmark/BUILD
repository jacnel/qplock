load("@rules_cc//cc:defs.bzl", "cc_proto_library")
load("@com_google_protobuf//:protobuf.bzl", "py_proto_library")
load("@bazel_skylib//rules:common_settings.bzl", "string_flag")

# proto_library(
#     name = "experiment_proto",
#     srcs = ["experiment.proto"],
#     deps = [
#         "@rome//protos:colosseum_proto",
#         "@rome//protos:metrics_proto",
#     ],
# )

# cc_proto_library(
#     name = "experiment_cc_proto",
#     deps = [":experiment_proto"],
# )

# py_proto_library(
#     name = "experiment_py_proto",
#     srcs = [
#         "experiment.proto",
#     ],
#     visibility = ["//visibility:public"],
#     deps = [
#         "@rome//protos:colosseum_py_proto",
#         "@rome//protos:metrics_py_proto",
#     ],
# )

# cc_library(
#     name = "setup",
#     hdrs = ["setup.h"],
#     deps = [
#         "@absl//absl/status",
#         "@rome//rome/logging",
#         "@rome//rome/rdma/channel:sync_accessor",
#         "@rome//rome/util:status_util",
#     ],
# )

# cc_library(
#     name = "server",
#     hdrs = ["server.h"],
#     deps = [
#         ":experiment_cc_proto",
#         ":setup",
#         "//qplock:a_lock",
#         "@absl//absl/status",
#         "@absl//absl/status:statusor",
#         "@rome//rome/rdma/connection_manager",
#     ],
# )

# cc_library(
#     name = "client",
#     hdrs = ["client.h"],
#     deps = [
#         ":experiment_cc_proto",
#         ":setup",
#         "//qplock:a_lock",
#         "//qplock:lock_handle",
#         "//qplock:remote_lock_handle",
#         "@absl//absl/status",
#         "@absl//absl/status:statusor",
#     ],
# )

filegroup(
    name = "nodefiles",
    srcs = glob(["nodefiles/**"]),
    visibility = ["//visibility:public"],
)

filegroup(
    name = "configfiles",
    srcs = glob(["configfiles/**"]),
)

filegroup(
    name = "experiments",
    srcs = glob(["experiments/**"]),
)

# string_flag(
#     name = "lock_type",
#     build_setting_default = "mcs",
#     values = [
#         "mcs",
#         "spin",
#         "qp",
#         "alock",
#     ],
# )

# config_setting(
#     name = "alock",
#     flag_values = {
#         ":lock_type": "alock",
#     },
# )

# config_setting(
#     name = "mcs_lock",
#     flag_values = {
#         ":lock_type": "mcs",
#     },
# )

# config_setting(
#     name = "spin_lock",
#     flag_values = {
#         ":lock_type": "spin",
#     },
# )

# cc_binary(
#     name = "main",
#     srcs = ["main.cc"],
#     copts = ["-Wno-deprecated-enum-enum-conversion"],
#     data = [":nodefiles"],
#     deps = [
#         ":client",
#         ":experiment_cc_proto",
#         ":server",
#         ":setup",
#         "@absl//absl/flags:flag",
#         "@absl//absl/flags:parse",
#         "@absl//absl/status",
#         "@absl//absl/status:statusor",
#         "@rome//rome/colosseum:client_adaptor",
#         "@rome//rome/colosseum:qps_controller",
#         "@rome//rome/colosseum:workload_driver",
#         "@rome//rome/colosseum/streams",
#         "@rome//rome/logging",
#         "@rome//rome/metrics:summary",
#         "@rome//rome/rdma/connection_manager",
#         "@rome//rome/util:proto_util",
#     ],
# )

# We assume that data is saved to a directory within this subdirectory called `results`.
filegroup(
    name = "results",
    srcs = glob(["results/**"]),
)

# py_binary(
#     name = "launch",
#     srcs = ["launch.py"],
#     data = [
#         ":nodefiles",
#     ],
#     deps = [
#         ":experiment_py_proto",
#         ":plot",
#         "@rome//rome/util:debugpy_util",
#     ],
# )

# py_binary(
#     name = "plot",
#     srcs = ["plot.py"],

#     deps = [
#         ":experiment_py_proto",
#         "@rome//protos:colosseum_py_proto",
#         "@rome//protos:metrics_py_proto",
#     ],
# )
