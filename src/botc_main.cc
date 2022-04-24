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

#include "src/game.h"

#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/text_format.h"

using botc::GameLog;
using botc::GameState;
using google::protobuf::io::FileOutputStream;
using google::protobuf::TextFormat;

namespace botc {
GameLog CreateGameLog() {
  GameLog log;
  log.set_perspective(STORYTELLER);

  auto* setup = log.mutable_setup();
  for (const auto& player : {"a", "b", "c", "d", "e"}) {
    setup->add_players(player);
  }
  auto *player_roles = setup->mutable_player_roles();
  (*player_roles)["a"] = IMP;
  (*player_roles)["b"] = MONK;
  (*player_roles)["c"] = SPY;
  (*player_roles)["d"] = EMPATH;
  (*player_roles)["e"] = VIRGIN;

  return log;
}

void Run() {
  GameState g(CreateGameLog());

  const char *filename = "./model.pbtxt";

  int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  CHECK_NE(fd, -1) << "File not found: " << filename;

  FileOutputStream* output = new FileOutputStream(fd);

  TextFormat::Print(g.SatModel().Build(), output);
  output->Flush();
  close(fd);
  LOG(INFO) << "Valid: " << g.CountWorlds({{"a", IMP}});
}
}  // namespace botc

int main(int argc, char** argv) {
  botc::Run();
  return 0;
}
