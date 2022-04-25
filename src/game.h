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
#include "src/solver.pb.h"
#include "ortools/sat/cp_model.h"

namespace botc {

const int kNoPlayer =  - 1;  // Used in place of player index.

using operations_research::sat::CpModelBuilder;
using operations_research::sat::BoolVar;
using std::string;
using std::vector;
using std::map;
using std::unordered_map;

namespace internal {
// In-game current time.
struct Time {
  bool IsDay = false;
  int Count = 0;
  string ToString() const;
};

struct Nomination {
  int Nominator, Nominee;
};

struct SlayerShot {
  int Slayer, Target;
};
}  // namespace internal

// This contains an instance of a BOTC game on a particular time.
class GameState {
 public:
  // Factory initialization functions.
  static GameState FromStorytellerPerspective(
    absl::Span<const string> players,
    const unordered_map<string, Role>& roles,
    const string& red_herring);
  static GameState FromStorytellerPerspective(
    absl::Span<const string> players,
    const unordered_map<string, Role>& roles);
  static GameState FromObserverPerspective(absl::Span<const string> players);
  static GameState FromPlayerPerspective(absl::Span<const string> players);
  static GameState FromProto(const GameLog& log);

  // Game events.
  void AddEvent(const Event& event);
  void AddDay(int count);
  void AddNight(int count);
  void AddStorytellerInteraction(const StorytellerInteraction& interaction);
  void AddNomination(const Nomination& nomination);
  void AddNomination(const string& nominator, const string& nominee);
  void AddVote(const Vote& vote);
  void AddVote(absl::Span<const string> votes, const string& on_the_block);
  void AddExecution(const string& name);
  void AddDeath(const string& name);
  void AddClaim(const Claim& claim);
  void AddClaim(const string& player, Role role);
  void AddClaim(const string& player, Role role, const RoleAction& info);
  void AddVictory(Team victory);
  void AddShownToken(const string& player, Role role);
  void AddMinionInfo(const string& player,
                     const MinionInfo& minion_info);
  void AddDemonInfo(const string& player, const DemonInfo& demon_info);
  void AddRoleAction(const string& player, const RoleAction& role_action);
  void AddWasherwomanInfo(const string& player,
                          const LearnRoleInfo& washerwoman_info);
  void AddLibrarianInfo(const string& player,
                        const LearnRoleInfo& librarian_info);
  void AddInvestigatorInfo(const string& player,
                           const LearnRoleInfo& investigator_info);
  void AddChefInfo(const string& player, int chef_info);
  void AddEmpathInfo(const string& player, int empath_info);
  void AddFortuneTellerAction(const string& player,
                              const FortuneTellerAction& fortuneteller_action);
  void AddMonkAction(const string& player, const string& monk_action);
  void AddButlerAction(const string& player, const string& butler_action);
  void AddRavenkeeperInfo(const string& player,
                          const RavenkeeperInfo& ravenkeeper_info);
  void AddUndertakerInfo(const string& player, Role undertaker_info);
  void AddSlayerAction(const string& player, const string& slayer_action);
  void AddPoisonerAction(const string& player, const string& poisoner_action);
  void AddImpAction(const string& player, const string& imp_action);
  void AddSpyInfo(const string& player, const SpyInfo& spy_info);

  // Public state accessors.
  bool IsDay() { return cur_time_.IsDay; }
  int TimeCount() { return cur_time_.Count; }

  int NumAlive() const { return num_alive_; }
  bool IsAlive(const string& player) const {
    return is_alive_[PlayerIndex(player)];
  }

  string OnTheBlock() const {
    return on_the_block_ == kNoPlayer ? "" : players_[on_the_block_];
  }

  string Execution() const {
    return execution_ == kNoPlayer ? "" : players_[execution_];
  }

  string ExecitionDeath() const {
    return execution_death_ == kNoPlayer ? "" : players_[execution_death_];
  }

  string SlayerDeath() const {
    return slayer_death_ == kNoPlayer ? "" : players_[slayer_death_];
  }

  string NightDeath() const {
    return night_death_ == kNoPlayer ? "" : players_[night_death_];
  }

  bool IsGameOver() const { return victory_ != TEAM_UNSPECIFIED; }
  Team WinningTeam() const { return victory_; }

  // Returns the current role (which might have changed from night 1).
  Role GetRole(const string& player) {
    CHECK_EQ(perspective_, STORYTELLER)
        << "Roles are only set in Storyteller perspective";
    return st_player_roles_[PlayerIndex(player)];
  }

  // Solver API

  // Solves the game and returns all valid worlds.
  SolverResponse SolveGame() const;
  SolverResponse SolveGame(
      const unordered_map<string, Role>& player_roles) const;
  SolverResponse SolveGame(const SolverRequest& request) const;
  // Returns a single valid world.
  SolverResponse ValidWorld() const { return ValidWorld({}); }
  SolverResponse ValidWorld(
      const unordered_map<string, Role>& player_roles) const;

  const CpModelBuilder& SatModel() const { return model_; }  // for debugging

 private:
  GameState(Perspective perspective, const Setup& setup);

  void InitVarRolesInPlay();
  void InitRoleVars();
  void InitHelperVars();
  void InitNextNightRoleVars();
  void InitNextNightHelperVars();
  void InitNextDayRoleVars();
  void InitNextDayHelperVars();
  void InitPoisonerVars();
  void InitRedHerring(const string& name);
  int PlayerIndex(const string& name) const;

  // Syntactic-sugar-type helper functions.
  vector<BoolVar> CollectRoles(const vector<vector<BoolVar>>& from,
                               absl::Span<const Role> roles,
                               bool only_alive) const;
  vector<BoolVar> CollectRoles(const vector<vector<BoolVar>>& from,
                               absl::Span<const Role> roles) const;
  vector<BoolVar> CollectAliveRoles(const vector<vector<BoolVar>>& from,
                                    absl::Span<const Role> roles) const;
  void PropagateRoles(const vector<vector<BoolVar>>& from,
                      const vector<vector<BoolVar>>& to,
                      absl::Span<const Role> roles);
  void PropagateRolesForPlayer(int player,
                               const vector<vector<BoolVar>>& from,
                               const vector<vector<BoolVar>>& to,
                               absl::Span<const Role> roles);

  // Constraints implementing particular roles.
  void AddBaronConstraints();
  void AddScarletWomanConstraints();
  void AddImpStarpassConstraints();

  // Other constraints.
  void AddRoleUniquenessConstraints(
      const vector<vector<BoolVar>>& player_roles);
  void AddGameNotOverConstraints();
  void AddGoodWonConstraints();
  void AddEvilWonConstraints();

  Perspective perspective_;
  vector<string> players_;
  unordered_map<string, int> player_index_;
  int num_players_, num_outsiders_, num_minions_;
  internal::Time cur_time_;
  vector<bool> is_alive_;  // Is player currently alive.
  int num_alive_;
  vector<internal::Nomination> nominations_;  // Last day nominations.
  vector<internal::SlayerShot> slayer_shots_;  // Last day Slayer shots.
  int num_votes_;  // Votes on the last nomination.
  int on_the_block_;  // A player index (or kNoPlayer) for the execution block.
  int execution_;  // A player index (or kNoPlayer) for last day's executee.
  // Not the same to execution_, because executing dead players is valid.
  int execution_death_;  // A player index for last day's execution death.
  int slayer_death_;  // A player index (or kNoPlayer) for last day Slayer kill.
  int night_death_;  // A player index (or kNoPlayer) for last night kill.
  bool game_maybe_over_;  // Whether the next event can be a Victory event.
  Team victory_;

  // These variables are only used in the storyteller perspective.
  vector<Role> st_player_roles_;  // Current roles.
  int st_red_herring_;
  int st_poisoner_pick_;  // A player index (or kNoPlayer) for last night pick.
  int st_imp_pick_;  // A player index (or kNoPlayer) for last night Imp pick.

  // OR-Tools related variables: compiling BOTC to SAT.
  CpModelBuilder model_;
  vector<BoolVar> roles_in_play_;  // x role.
  vector<vector<vector<BoolVar>>> day_roles_;  // x day x player x role.
  vector<vector<vector<BoolVar>>> night_roles_;  // x night x player x role.
  vector<vector<BoolVar>> shown_token_;  // x player x role (night 1 only).
  vector<BoolVar> red_herring_;  // x player.
  vector<vector<BoolVar>> imp_pick_;  // x night x player, starting night 2.
  vector<vector<BoolVar>> poisoner_pick_;  // x night x player.
  vector<BoolVar> is_evil_;  // x player.
};

// Syntactic sugar.
SolverRequest FromPlayerRoles(const unordered_map<string, Role>& player_roles);

}  // namespace botc

#endif  // SRC_GAME_H_
