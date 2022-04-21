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

#ifndef SRC_GAME_H_
#define SRC_GAME_H_

#include <string>
#include <vector>
#include <unordered_map>

#include "absl/strings/str_format.h"
#include "src/game_log.pb.h"
#include "ortools/sat/cp_model.h"

namespace botc {

using operations_research::sat::CpModelBuilder;
using operations_research::sat::BoolVar;
using std::string;
using std::vector;
using std::unordered_map;

// In-game current time.
struct Time {
  bool IsDay = false;
  int Count = 0;
};

// This contains an instance of a BOTC game on a particular time.
class GameState {
 public:
  GameState(Perspective perspective, const Setup& setup);
  explicit GameState(const GameLog& game_log);

  void AddEvent(const Event& event);
  GameLog ToProto() const;

  // Solves the game and returns true whether worlds exist.
  bool IsValid() const;
  const CpModelBuilder& Model() const { return model_; }  // for debugging

 private:
  void InitRoleVars();
  void AddNextRoleVars();
  void InitHelperVars();
  void InitRedHerring(const string& name);

  Perspective perspective_;
  vector<string> players_;
  unordered_map<string, int> player_index_;
  int num_players_, num_outsiders_, num_minions_;
  Time cur_time;
  vector<Event> events_;
  vector<bool> is_alive_;  // Is player currently alive.
  vector<vector<Nomination>> nominations_;  // Nominations per day.
  vector<int> executions_;  // One player index (or kNoPlayer) per day.
  vector<int> night_deaths_;  // One player index (or kNoPlayer) per night.
  vector<vector<int>> day_deaths_;  // Due to Slayer, we can have more than one.
  vector<Role> player_roles_;  // Usually only known in storyteller perspective.
  int red_herring_;  // Usually only known in storyteller perspective.

  CpModelBuilder model_;
  vector<vector<vector<BoolVar>>> player_roles_day_;
  vector<vector<vector<BoolVar>>> player_roles_night_;
  vector<BoolVar> player_is_evil_;
};

void SimpleCpProgram();

}  // namespace botc

#endif  // SRC_GAME_H_
