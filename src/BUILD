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
        "@com_google_ortools//ortools/base",
        "@com_google_protobuf//:protobuf",
    ],
    hdrs = ["util.h"],
)

cc_library(
    name = "model_wrapper_lib",
    srcs = ["model_wrapper.cc"],
    deps = [
        ":util_lib",
        "@com_google_absl//absl/types:span",
        "@com_google_ortools//ortools/base",
        "@com_google_ortools//ortools/sat:cp_model",
    ],
    hdrs = ["model_wrapper.h"],
)

cc_test(
    name = "model_wrapper_test",
    srcs = ["model_wrapper_test.cc"],
    deps = [
        ":model_wrapper_lib",
        "@com_google_absl//absl/types:span",
        "@com_google_googletest//:gtest",
    ],
)

cc_library(
    name = "game_sat_solver_lib",
    srcs = ["game_sat_solver.cc"],
    deps = [
        ":game_state_lib",
        ":model_wrapper_lib",
        ":solver_cc_proto",
        ":util_lib",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/types:span",
        "@com_google_ortools//ortools/base",
        "@com_google_ortools//ortools/sat:cp_model",
        "@com_google_ortools//ortools/sat:cp_model_solver",
    ],
    hdrs = ["game_sat_solver.h"],
)

cc_test(
    name = "game_sat_solver_test",
    srcs = ["game_sat_solver_test.cc"],
    data = glob(["examples/**"]),
    deps = [
        ":game_sat_solver_lib",
        ":game_state_lib",
        "@com_google_absl//absl/types:span",
        "@com_google_googletest//:gtest",
    ],
)

cc_library(
    name = "game_state_lib",
    srcs = ["game_state.cc"],
    deps = [
        ":game_log_cc_proto",
        ":util_lib",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings:strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/types:span",
        "@com_google_ortools//ortools/base",
    ],
    hdrs = ["game_state.h"],
)

cc_test(
    name = "game_state_test",
    srcs = ["game_state_test.cc"],
    deps = [
        ":game_state_lib",
        "@com_google_absl//absl/types:span",
        "@com_google_googletest//:gtest",
    ],
)

cc_binary(
    name = "botc",
    srcs = ["main.cc"],
    deps = [
        ":game_sat_solver_lib",
        ":game_state_lib",
        ":util_lib",
        "@com_google_absl//absl/flags:flag",
        "@com_google_protobuf//:protobuf",
    ],
)

# This is not a part of the BOTC solver. It is used for reference.
cc_binary(
    name = "ortools_example",
    srcs = ["ortools_example.cc"],
    deps = [
        ":util_lib",
        "@com_google_absl//absl/flags:flag",
        "@com_google_ortools//ortools/base",
        "@com_google_ortools//ortools/sat:cp_model",
        "@com_google_ortools//ortools/sat:cp_model_solver",
        "@com_google_ortools//ortools/util:sorted_interval_list",
        "@com_google_absl//absl/types:span",
    ],
)