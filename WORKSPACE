workspace(name = "qplock")

local_repository(
    name = "rome",
    path = "../rome",
)

load("@rome//:dependencies.bzl", "rome_dependencies")

rome_dependencies()

load("@rome//:setup.bzl", "rome_setup")

rome_setup()

# # Protocol buffer setup.
# load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
# load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

# http_archive(
#     name = "rules_cc",
#     sha256 = "4dccbfd22c0def164c8f47458bd50e0c7148f3d92002cdb459c2a96a68498241",
#     urls = ["https://github.com/bazelbuild/rules_cc/releases/download/0.0.1/rules_cc-0.0.1.tar.gz"],
# )

# http_archive(
#     name = "rules_proto",
#     sha256 = "66bfdf8782796239d3875d37e7de19b1d94301e8972b3cbd2446b332429b4df1",
#     strip_prefix = "rules_proto-4.0.0",
#     urls = [
#         "https://github.com/bazelbuild/rules_proto/archive/refs/tags/4.0.0.tar.gz",
#     ],
# )

# load("@rules_cc//cc:repositories.bzl", "rules_cc_dependencies")

# rules_cc_dependencies()

# load("@rules_proto//proto:repositories.bzl", "rules_proto_dependencies", "rules_proto_toolchains")

# rules_proto_dependencies()

# rules_proto_toolchains()

# http_archive(
#     name = "benchmark",
#     strip_prefix = "benchmark-1.6.1",
#     urls = ["https://github.com/google/benchmark/archive/refs/tags/v1.6.1.tar.gz"],
# )

# http_archive(
#     name = "rules_python",
#     sha256 = "b6d46438523a3ec0f3cead544190ee13223a52f6a6765a29eae7b7cc24cc83a0",
#     urls = ["https://github.com/bazelbuild/rules_python/releases/download/0.1.0/rules_python-0.1.0.tar.gz"],
# )

# # Abseil
# http_archive(
#     name = "com_google_absl",
#     sha256 = "db546f89b33ebef0a397511a09c36267776fe11985a4ee35bac32c0ab516fca1",
#     strip_prefix = "abseil-cpp-a4cc270df18b47685e568e01bb5c825493f58d25",
#     urls = ["https://github.com/abseil/abseil-cpp/archive/a4cc270df18b47685e568e01bb5c825493f58d25.zip"],
# )

# # GoogleTest/GoogleMock framework. Used by most unit-tests.
# http_archive(
#     name = "com_google_googletest",  # 2021-05-19T20:10:13Z
#     sha256 = "8cf4eaab3a13b27a95b7e74c58fb4c0788ad94d1f7ec65b20665c4caf1d245e8",
#     strip_prefix = "googletest-aa9b44a18678dfdf57089a5ac22c1edb69f35da5",
#     urls = ["https://github.com/google/googletest/archive/aa9b44a18678dfdf57089a5ac22c1edb69f35da5.zip"],
# )

# # Google benchmark.
# http_archive(
#     name = "com_github_google_benchmark",
#     sha256 = "62e2f2e6d8a744d67e4bbc212fcfd06647080de4253c97ad5c6749e09faf2cb0",
#     strip_prefix = "benchmark-0baacde3618ca617da95375e0af13ce1baadea47",
#     urls = ["https://github.com/google/benchmark/archive/0baacde3618ca617da95375e0af13ce1baadea47.zip"],
# )

# # Fuzzing
# http_archive(
#     name = "rules_fuzzing",
#     sha256 = "a5734cb42b1b69395c57e0bbd32ade394d5c3d6afbfe782b24816a96da24660d",
#     strip_prefix = "rules_fuzzing-0.1.1",
#     urls = ["https://github.com/bazelbuild/rules_fuzzing/archive/v0.1.1.zip"],
# )

# # Protobuf
# load("@rules_fuzzing//fuzzing:repositories.bzl", "rules_fuzzing_dependencies")

# rules_fuzzing_dependencies()

# load("@rules_fuzzing//fuzzing:init.bzl", "rules_fuzzing_init")

# rules_fuzzing_init()

# git_repository(
#     name = "tcmalloc",
#     branch = "master",
#     remote = "https://github.com/jacnel/tcmalloc.git",
# )