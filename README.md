# Blood On The Clocktower Solver Tools

This repository contains a solver for some simple strategies for the [Blood On The Clocktower](https://bloodontheclocktower.com) social deduction game.

## Usage

The main binary takes a game log as input in the protobuf text format, calls the solver, and outputs the solution (also in protobuf text format) to STDERR or optionally to a file.

Examples:

```
bazel-bin/src/botc --game_log=src/examples/tb/virgin.pbtxt
```

```
bazel-bin/src/botc --game_log=src/examples/tb/virgin.pbtxt --output_solution=solution.pbtxt
```

To provide additional solver parameters (see [solver.proto](https://github.com/olarozenfeld/botc/blob/master/src/solver.proto) for options), use the `--solver_parameters` flag (also a file path to the parameters in protobuf text format).

This allows adding assumptions before solving, setting `debug_mode` to output the SAT model and the individual SAT solver responses and solutions, and more.

## Development

We use [Bazel](https://bazel.build) version 4.0 or higher.

You must compile using C++20:

```sh
bazel build --cxxopt=-std=c++20 //src:botc
```

You may run tests using:

```sh
bazel test --cxxopt=-std=c++20 //...:all
```

We use the [Google C++ style guide](https://google.github.io/styleguide/cppguide.html). To check style guide complicance, we use [cpplint]():

```
cpplint src/*.h src/*.cc
```