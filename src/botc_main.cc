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

#include <fcntl.h>
#include <fstream>
#include <iostream>

#include "src/game.h"

#include "absl/flags/flag.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/text_format.h"

using std::string;

// All files below are in text proto format.
ABSL_FLAG(string, game_log, "", "Game log file path.");
ABSL_FLAG(string, solver_parameters, "", "Solver parameters file path.");
ABSL_FLAG(string, output_model, "", "Optional SAT model output file.");
ABSL_FLAG(string, output_model_vars, "",
    "Optional SAT model variables output file.");
ABSL_FLAG(string, output_solution, "", "Optional solution output file.");

namespace botc {
using google::protobuf::io::FileInputStream;
using google::protobuf::io::FileOutputStream;
using google::protobuf::Message;
using google::protobuf::TextFormat;
using std::ofstream;

void ReadProtoFromFile(const string& filename, Message* msg) {
  int fi = open(filename.c_str(), O_RDONLY);
  CHECK_NE(fi, -1) << "File not found: " << filename;
  FileInputStream fstream(fi);
  TextFormat::Parse(&fstream, msg);
  close(fi);
}

void WriteProtoToFile(const string& filename, const Message& msg) {
  int fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  CHECK_NE(fd, -1) << "File not found: " << filename;
  FileOutputStream* output = new FileOutputStream(fd);
  TextFormat::Print(msg, output);
  output->Flush();
  close(fd);
}

GameState SampleGame() {
  GameState g = GameState::FromPlayerPerspective(
      {"P1", "P2", "P3", "P4", "P5", "P6", "P7"});
  g.AddNight(1);
  g.AddShownToken("P5", SCARLET_WOMAN);
  g.AddMinionInfo("P5", NewMinionInfo("P1"));  // P5 SW, P1 Imp
  g.AddDay(1);
  g.AddClaim("P1", SOLDIER);  // Imp lies
  g.AddClaim("P2", MAYOR);
  g.AddClaim("P3", CHEF);
  g.AddClaim("P4", VIRGIN);
  g.AddClaim("P5", FORTUNE_TELLER);
  g.AddClaim("P6", SLAYER);
  g.AddClaim("P7", RAVENKEEPER);
  g.AddSlayerAction("P6", "P1");
  g.AddDeath("P1");
  g.AddNight(2);

  return g;
}

void Run() {
  string game_log = absl::GetFlag(FLAGS_game_log);
  CHECK(!game_log.empty()) << "--game_log should be a valid path";
  GameLog log;
  ReadProtoFromFile(game_log, &log);
  GameState g = GameState::FromProto(log);

  SolverRequest request;  // If file present, read from file.
  string solver_parameters = absl::GetFlag(FLAGS_solver_parameters);
  if (!solver_parameters.empty()) {
    ReadProtoFromFile(solver_parameters, &request);
  }

  string output_model = absl::GetFlag(FLAGS_output_model);
  const auto& model_pb = g.SatModel().Build();
  if (!output_model.empty()) {
    WriteProtoToFile(output_model, model_pb);
  }
  string output_model_vars = absl::GetFlag(FLAGS_output_model_vars);
  if (!output_model_vars.empty()) {
    ofstream f;
    f.open(output_model_vars);
      for (int i = 0; i < model_pb.variables_size(); ++i) {
        f << i << ": " << VarDebugString(model_pb, i) << "\n";
      }
    f.close();
  }

  SolverResponse solution = g.SolveGame(request);
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
