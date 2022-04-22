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

  // Solves the game and returns whether worlds exist.
  bool IsValid() const;
  const CpModelBuilder& Model() const { return model_; }  // for debugging

 private:
  void InitRoleVars();
  void InitHelperVars();
  void InitRedHerring(const string& name);
  void AddDay(int count);
  void AddNight(int count);
  void AddStoryTellerInteraction(const StorytellerInteraction& interaction);
  void AddNomination(const Nomination& nomination);
  void AddExecution(const string& name);
  void AddDeath(const string& name);
  void AddClaim(const Claim& claim);
  void AddVictory(Team victory);
  void AddGameNotOverConstraints();
  void AddGoodWonConstraints();
  void AddEvilWonConstraints();
  void InitNextNightRoleVars();
  void InitNextNightHelperVars();
  void PropagateRoles(const vector<vector<BoolVar>>& from,
                      const vector<vector<BoolVar>>& to,
                      absl::Span<const Role> roles,
                      const string& from_name,
                      const string& to_name);
  BoolVar NewVarRoleInPlay(Role role);

  // Constraints implementing particular roles.
  void AddBaronConstraints();
  void AddScarletWomanConstraints();

  Perspective perspective_;
  vector<string> players_;
  unordered_map<string, int> player_index_;
  int num_players_, num_outsiders_, num_minions_;
  Time cur_time_;
  vector<bool> is_alive_;  // Is player currently alive.
  int num_alive_;
  vector<Nomination> nominations_;  // Last day nominations.
  int execution_;  // A player index (or kNoPlayer) for last executee.
  // In TB, executees always die, so we don't need to track deaths separately.
  int slayer_death_;  // A player index (or kNoPlayer) for Slayer kill.
  int night_death_;  // A player index (or kNoPlayer) for last night kill.
  Team victory_;

  // These variables are only used in the storyteller perspective.
  vector<Role> st_player_roles_;  // Current roles.
  int st_red_herring_;
  int st_poisoner_pick_;  // A player index (or kNoPlayer) for last night pick.
  int st_imp_pick_;  // A player index (or kNoPlayer) for last night Imp pick.

  // OR-Tools related variables: compiling BOTC to SAT.
  CpModelBuilder model_;
  vector<vector<vector<BoolVar>>> day_roles_;  // x day x player x role.
  vector<vector<vector<BoolVar>>> night_roles_;  // x night x player x role.
  vector<vector<BoolVar>> shown_token_;  // x player x role (night 1 only).
  vector<BoolVar> red_herring_;  // x player.
  vector<vector<BoolVar>> imp_pick_;  // x night x player, starting night 2.
  vector<vector<BoolVar>> poisoner_pick_;  // x night x player.
  vector<BoolVar> is_evil_;  // x player.
};

}  // namespace botc

#endif  // SRC_GAME_H_
