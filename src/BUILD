package(default_visibility = ["//visibility:public"])

proto_library(
    name = "game_log_proto",
    srcs = ["game_log.proto"],
)

cc_proto_library(
    name = "game_log_cc_proto",
    deps = [":game_log_proto"],
)

proto_library(
    name = "solver_proto",
    srcs = ["solver.proto"],
    deps = [":game_log_proto"],
)

cc_proto_library(
    name = "solver_cc_proto",
    deps = [":solver_proto"],
)

cc_library(
    name = "util_lib",
    srcs = ["util.cc"],
    deps = [
        "@com_github_google_glog//:glog",
        "@com_google_protobuf//:protobuf",
    ],
    hdrs = ["util.h"],
)

cc_library(
    name = "game_lib",
    srcs = ["game.cc"],
    deps = [
        ":game_log_cc_proto",
        ":solver_cc_proto",
        ":util_lib",
        "@com_google_absl//absl/types:span",
        "@com_google_ortools//ortools/base",
        "@com_google_ortools//ortools/sat:cp_model",
        "@com_google_ortools//ortools/sat:cp_model_solver",
    ],
    hdrs = ["game.h"],
)

cc_test(
    name = "game_test",
    srcs = ["game_test.cc"],
    deps = [
        ":game_lib",
        "@com_google_absl//absl/types:span",
        "@com_google_googletest//:gtest",
    ],
)

cc_binary(
    name = "botc",
    srcs = ["main.cc"],
    deps = [
        ":game_lib",
        ":util_lib",
        "@com_google_absl//absl/flags:flag",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_binary(
    name = "ortools_example",
    srcs = ["ortools_example.cc"],
    deps = [
        "@com_google_ortools//ortools/base",
        "@com_google_ortools//ortools/sat:cp_model",
        "@com_google_ortools//ortools/sat:cp_model_solver",
        "@com_google_ortools//ortools/util:sorted_interval_list",
        "@com_google_absl//absl/types:span",
    ],
)