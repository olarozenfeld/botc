// Copyright 2022 Ola Rozenfeld
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <filesystem>
#include <iostream>
#include <string>
#include <chrono>  // NOLINT [build/c++11]

#include "absl/flags/flag.h"
#include "src/game_sat_solver.h"
#include "src/game_state.h"
#include "src/util.h"

using std::cout;
using std::chrono::duration;
using std::chrono::steady_clock;
using std::endl;
using std::filesystem::path;
using std::string;

// All files below are in text proto format.
ABSL_FLAG(string, game_log, "", "Game log file path.");
ABSL_FLAG(string, solver_parameters, "", "Solver parameters file path.");
ABSL_FLAG(string, output_solution, "", "Optional solution output file.");

namespace botc {

void Run() {
  path game_log = absl::GetFlag(FLAGS_game_log);
  CHECK(!game_log.empty()) << "Set --game_log to a valid path";
  GameState g = GameState::ReadFromFile(game_log);
  SolverRequest request;  // If file present, read from file.
  path solver_parameters = absl::GetFlag(FLAGS_solver_parameters);
  if (!solver_parameters.empty()) {
    ReadProtoFromFile(solver_parameters, &request);
  }

  GameSatSolver s(g);
  steady_clock::time_point begin = steady_clock::now();
  SolverResponse solution = s.Solve(request);
  steady_clock::time_point end = steady_clock::now();

  path output_solution = absl::GetFlag(FLAGS_output_solution);
  if (!output_solution.empty()) {
    WriteProtoToFile(solution, output_solution);
    cout << "Solution written to " << output_solution << endl;
  } else {
    cout << "Solve response:\n" << solution.DebugString() << endl;
  }
  cout << "Solve time: " << duration<double>(end - begin).count() << "[s]\n";
}
}  // namespace botc

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  botc::Run();
  return 0;
}
