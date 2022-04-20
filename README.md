# Blood On The Clocktower Solver Tools

This repository contains a solver for some simple strategies for the [Blood On The Clocktower](https://bloodontheclocktower.com) social deduction game.

## Usage

TBD

## Development

We use [Bazel](https://bazel.build) version 4.0 or higher.

You must compile using C++20:

* on UNIX:

  ```sh
  bazel build --cxxopt=-std=c++20 //src:botc
  ```
* on Windows when using MSVC:

  ```sh
  bazel build --cxxopt="-std:c++20" //src:botc
  ```

You may run tests using:

* on UNIX:

  ```sh
  bazel test --cxxopt=-std=c++20 //...:all
  ```
* on Windows when using MSVC:

  ```sh
  bazel test --cxxopt="-std:c++20" //...:all
  ```

We use the [Google C++ style guide](https://google.github.io/styleguide/cppguide.html). To check style guide complicance, we use [cpplint]():

```
cpplint src/*.h src/*.cc
```