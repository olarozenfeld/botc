package(default_visibility = ["//visibility:public"])

cc_library(
    name = "foo_lib",
    srcs = ["foo.cc"],
    hdrs = ["foo.h"],
)

cc_binary(
    name = "foo",
    srcs = ["main.cc"],
    deps = [
        ":foo_lib",
    ],
)
