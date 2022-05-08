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

#include <string>

#include "absl/flags/flag.h"
#include "src/game.h"
#include "src/util.h"

using std::string;

// All files below are in text proto format.
ABSL_FLAG(string, game_log, "", "Game log file path.");
ABSL_FLAG(bool, sample_game, "", "Use the sample game in code instead of "
          "reading the game log from file.");
ABSL_FLAG(string, solver_parameters, "", "Solver parameters file path.");
ABSL_FLAG(string, output_model, "", "Optional SAT model output file.");
ABSL_FLAG(string, output_model_vars, "",
          "Optional SAT model variables output file.");
ABSL_FLAG(string, output_solution, "", "Optional solution output file.");

namespace botc {

GameState SampleGame() {
  GameState g = GameState::FromPlayerPerspective(
      {"P1", "P2", "P3", "P4", "P5", "P6", "P7"});
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2"}, {EMPATH, MAYOR, FORTUNE_TELLER});
  g.AddDay(1);
  g.AddAllClaims(
      {MAYOR, RAVENKEEPER, BUTLER, SAINT, SOLDIER, SLAYER, MONK}, "P1");
  g.AddNight(2);
  g.AddImpAction("P1", "P1");
  g.AddDay(2);
  g.AddDeath("P1");
  g.AddNoStorytellerAnnouncement();
  return g;
}

void Run() {
  bool sample = absl::GetFlag(FLAGS_sample_game);
  string game_log = absl::GetFlag(FLAGS_game_log);
  CHECK(sample || !game_log.empty())
      << "Either set --sample_game or set --game_log to a valid path";
  GameState g = (sample ? SampleGame() : GameState::ReadFromFile(game_log));

  SolverRequest request;  // If file present, read from file.
  string solver_parameters = absl::GetFlag(FLAGS_solver_parameters);
  if (!solver_parameters.empty()) {
    ReadProtoFromFile(solver_parameters, &request);
  }

  string output_model = absl::GetFlag(FLAGS_output_model);
  if (!output_model.empty()) {
    g.WriteModelToFile(output_model);
  }
  string output_model_vars = absl::GetFlag(FLAGS_output_model_vars);
  if (!output_model_vars.empty()) {
    g.WriteModelVariablesToFile(output_model_vars);
  }

  SolverResponse solution = g.Solve(request);
  LOG(INFO) << "Solve response:\n" << solution.DebugString();
  string output_solution = absl::GetFlag(FLAGS_output_solution);
  if (!output_solution.empty()) {
    WriteProtoToFile(output_solution, solution);
  }
}
}  // namespace botc

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  botc::Run();
  return 0;
}
