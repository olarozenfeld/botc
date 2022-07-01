# Blood On The Clocktower Solver Tools

This repository contains a solver for the [Blood On The Clocktower](https://bloodontheclocktower.com) social deduction game. For now, it is an executable that accepts a description (log) of the game from a certain perspective, and outputs all the mechanically possible worlds. It is also possible to restrict solutions to certain subsets, for example solutions where "Jackie is Evil" or "there is no Poisoner in play", or "Nadine was healthy on night 3".

Currently, only the [Trouble Brewing](https://wiki.bloodontheclocktower.com/Trouble_Brewing) script is supported. Also, we currently do not assign any likelihoods to the mechanically possible worlds, although it is a feature we hope to implement at some point.

## Usage

The main binary takes a [game log](https://github.com/olarozenfeld/botc/blob/master/src/game_log.proto) as input in the protobuf text format, calls the solver, and outputs the solutions (also in protobuf text format) to STDOUT or optionally to a file.

Examples:

```
bazel-bin/src/botc --game_log=src/examples/tb/virgin.pbtxt
```

```
bazel-bin/src/botc --game_log=src/examples/tb/virgin.pbtxt --output_solution=solution.pbtxt
```

To provide additional solver parameters (see [solver.proto](https://github.com/olarozenfeld/botc/blob/master/src/solver.proto) for options), use the `--solver_parameters` flag (also a file path to the parameters in protobuf text format).

This allows adding assumptions before solving, setting `debug_mode` to output the SAT model and the individual SAT solver responses and solutions, and more.

In general, a game is solvable if it contains all the role claims and the role action claims up until the current time. Usually, this happens in final 3, where players provide this information in a round-robin; in some games, this can happen before final 3 as well. Therefore, all soft claims, propagations of claims by others, and whisper tracking are not required for mechanically solving, and are currently ignored. In the future, we hope to use this data to assign probabilities to the possible worlds and implement player strategies.

## Development

We use [Bazel](https://bazel.build) version 4.0 or higher.

You must compile using C++20:

```sh
bazel build --cxxopt=-std=c++20 //src:botc
```

Note: on Windows, this option becomes `--cxxopt=-std:c++20`.

You may run tests using:

```sh
bazel test --cxxopt=-std=c++20 //...:all
```

We use the [Google C++ style guide](https://google.github.io/styleguide/cppguide.html). To check style guide complicance, we use [cpplint]():

```
cpplint src/*.h src/*.cc
```
