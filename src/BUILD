package(default_visibility = ["//visibility:public"])

proto_library(
    name = "game_log_proto",
    srcs = ["game_log.proto"],
)

cc_proto_library(
    name = "game_log_cc_proto",
    deps = [":game_log_proto"],
)

cc_library(
    name = "game",
    srcs = ["game.cc"],
    deps = [
        ":game_log_cc_proto",
        "@com_google_ortools//ortools/base",
        "@com_google_ortools//ortools/constraint_solver:cp",
    ],
    hdrs = ["game.h"],
)

cc_test(
    name = "game_test",
    srcs = ["game_test.cc"],
    deps = [":game"],
)

cc_binary(
    name = "botc",
    srcs = ["botc_main.cc"],
    deps = [":game"],
)