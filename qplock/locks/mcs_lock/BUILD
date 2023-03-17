cc_library(
    name = "mcs_lock",
    srcs = ["mcs_lock.cc"],
    hdrs = ["mcs_lock.h"],
    visibility = ["//visibility:public"],
    deps = ["//qplock:util",
            "@rome//rome/logging",
            "@rome//rome/util:thread_util",
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