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

using operations_research::sat::CpSolverResponse;
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
  void AddClaimWasherwomanInfo(const string& player, const LearnRoleInfo& info);
  void AddClaimWasherwomanInfo(
      const string& player, const string& ping1, const string& ping2,
      Role role);
  void AddClaimLibrarianInfo(const string& player, const LearnRoleInfo& info);
  void AddClaimLibrarianInfo(
      const string& player, const string& ping1, const string& ping2,
      Role role);
  void AddClaimInvestigatorInfo(
      const string& player, const LearnRoleInfo& info);
  void AddClaimInvestigatorInfo(
      const string& player, const string& ping1, const string& ping2,
      Role role);
  void AddClaimChefInfo(const string& player, int chef_info);
  void AddClaimEmpathInfo(const string& player, int empath_info);
  void AddClaimFortuneTellerAction(
      const string& player, const FortuneTellerAction& fortuneteller_action);
  void AddClaimFortuneTellerAction(
      const string& player, const string& pick1, const string& pick2, bool yes);
  void AddClaimMonkAction(const string& player, const string& monk_action);
  void AddClaimButlerAction(const string& player, const string& butler_action);
  void AddClaimRavenkeeperInfo(
      const string& player, const RavenkeeperInfo& info);
  void AddClaimRavenkeeperInfo(
      const string& player, const string& pick, Role role);
  void AddClaimUndertakerInfo(const string& player, Role undertaker_info);

  // We have no Slayer action claim, because Slayer actions are public
  // storyteller interactions.

  // The open Spy play.
  void AddClaimSpyInfo(const string& player, const SpyInfo& spy_info);

  // Claiming Poisoner does not convey information that couldn't be conveyed
  // simpler by claiming anything else, so we leave it out.

  // May theoretically occur after Recluse starpass.
  void AddClaimImpAction(const string& player, const string& imp_action);

  void AddVictory(Team victory);

  // An event to signal no event. Most common use-case is pre-solve, to
  // indicate there was no night death, or the game isn't over.
  void AddNoStorytellerAnnouncement();

  void AddShownToken(const string& player, Role role);
  void AddMinionInfo(const string& player,
                     const MinionInfo& minion_info);
  void AddMinionInfo(const string& player, const string& demon,
                     absl::Span<const string> minions);
  void AddDemonInfo(const string& player, const DemonInfo& demon_info);
  void AddDemonInfo(const string& player, absl::Span<const string> minions,
                    absl::Span<const Role> bluffs);
  void AddRoleAction(const string& player, const RoleAction& role_action);
  void AddWasherwomanInfo(const string& player,
                          const LearnRoleInfo& info);
  void AddWasherwomanInfo(const string& player, const string& ping1,
                          const string& ping2, Role role);
  void AddLibrarianInfo(const string& player, const LearnRoleInfo& info);
  void AddLibrarianInfo(const string& player, const string& ping1,
                          const string& ping2, Role role);
  void AddInvestigatorInfo(const string& player,
                           const LearnRoleInfo& info);
  void AddInvestigatorInfo(const string& player, const string& ping1,
                          const string& ping2, Role role);
  void AddChefInfo(const string& player, int chef_info);
  void AddEmpathInfo(const string& player, int empath_info);
  void AddFortuneTellerAction(const string& player,
                              const FortuneTellerAction& action);
  void AddFortuneTellerAction(const string& player, const string& pick1,
                              const string& pick2, bool yes);
  void AddMonkAction(const string& player, const string& monk_action);
  void AddButlerAction(const string& player, const string& butler_action);
  void AddRavenkeeperInfo(const string& player, const RavenkeeperInfo& info);
  void AddRavenkeeperInfo(const string& player, const string& pick, Role role);
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

  Role ClaimedRole(const string& player) {
    return claim_of_player_[PlayerIndex(player)];
  }

  vector<string> ClaimingRole(Role role);

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
  Role GetRole(const string& player) const {
    CHECK_EQ(perspective_, STORYTELLER)
        << "Roles are only set in Storyteller perspective";
    return st_player_roles_[PlayerIndex(player)];
  }

  string PerspectivePlayer() const {
    return perspective_player_ == kNoPlayer
        ? "" : players_[perspective_player_];
  }

  Role ShownToken(const string& player) const;

  // Solver API

  // Solves the game and returns all valid worlds.
  SolverResponse SolveGame() const { return SolveGame(SolverRequest()); }
  SolverResponse SolveGame(const SolverRequest& request) const;
  // Returns a single valid world.
  SolverResponse ValidWorld() const { return ValidWorld(SolverRequest()); }
  SolverResponse ValidWorld(const SolverRequest& request) const {
    SolverRequest r = request;
    r.set_stop_after_first_solution(true);
    return SolveGame(r);
  }

  const CpModelBuilder& SatModel() const { return model_; }  // for debugging

 private:
  GameState(Perspective perspective, const Setup& setup);

  void InitNightRoleVars();
  void InitDayRoleVars();
  void InitVarRolesInPlay();
  void InitShownTokenVars();
  void InitIsEvilVars();
  void InitImpVars();
  void InitPoisonerVars();
  void InitMonkVars();
  void InitButlerVars();
  void InitRedHerring(const string& name);
  int PlayerIndex(const string& name) const;
  void BeforeEvent(Event::DetailsCase event_type);
  void ValidateRoleAction(const string& player, Role role);
  void AddVirginProcConstraints(bool proc);
  void AddBaronConstraints();
  void AddScarletWomanConstraints();
  void AddImpStarpassConstraints();
  void AddRoleUniquenessConstraints(
      const vector<vector<BoolVar>>& player_roles);
  void AddNoDeathConstraints();
  void AddGameNotOverConstraints();
  void AddGoodWonConstraints();
  void AddEvilWonConstraints();
  void AddLearningRoleInfoConstraints(
    const string& player, Role player_role, const string& ping1,
    const string& ping2, Role role);

  // Syntactic-sugar-type helper functions.
  vector<BoolVar> CollectRolesForPlayer(
    const vector<vector<BoolVar>>& from, int player,
    absl::Span<const Role> roles, bool only_alive) const;
  vector<BoolVar> CollectRoles(const vector<vector<BoolVar>>& from,
                               absl::Span<const Role> roles,
                               bool only_alive) const;
  vector<BoolVar> CollectRoles(const vector<vector<BoolVar>>& from,
                               absl::Span<const Role> roles) const;
  vector<BoolVar> CollectAliveRoles(const vector<vector<BoolVar>>& from,
                                    absl::Span<const Role> roles) const;
  BoolVar CreateAliveRoleVar(Role role, const internal::Time& time);
  void PropagateRoles(const vector<vector<BoolVar>>& from,
                      const vector<vector<BoolVar>>& to,
                      absl::Span<const Role> roles);
  void PropagateRolesForPlayer(int player,
                               const vector<vector<BoolVar>>& from,
                               const vector<vector<BoolVar>>& to,
                               absl::Span<const Role> roles);
  BoolVar CreatePoisonerPickedRoleVar(Role role, int night, bool only_alive);
  BoolVar CreatePoisonedRoleVar(Role role, int day, bool only_alive);

  vector<BoolVar> CollectAssumptionLiterals(const SolverRequest& request) const;
  void WriteSatSolutionToFile(const CpSolverResponse response,
                              CpModelBuilder* model,
                              const string& filename) const;

  Perspective perspective_;
  vector<string> players_;
  unordered_map<string, int> player_index_;
  int num_players_, num_outsiders_, num_minions_;
  internal::Time cur_time_;
  vector<bool> is_alive_;  // Is player currently alive.
  int num_alive_;
  vector<internal::Nomination> nominations_;  // Last day nominations.
  vector<internal::SlayerShot> slayer_shots_;  // Last day Slayer shots.
  vector<bool> player_used_slayer_shot_;  // Slayer can only shoot once.
  vector<bool> player_has_been_nominated_;  // For Virgin procs.
  int num_votes_;  // Votes on the last nomination.
  int on_the_block_;  // A player index (or kNoPlayer) for the execution block.
  int execution_;  // A player index (or kNoPlayer) for last day's executee.
  // Not the same to execution_, because executing dead players is valid.
  int execution_death_;  // A player index for last day's execution death.
  int slayer_death_;  // A player index (or kNoPlayer) for last day Slayer kill.
  int night_death_;  // A player index (or kNoPlayer) for last night kill.
  bool next_event_maybe_victory_;  // Whether the next event can be a Victory.
  bool next_event_maybe_death_;  // Whether the next event can be a death.
  bool next_event_maybe_execution_;  // Whether the next event can be execution.
  Team victory_;  // The winning team, if the game is over.
  vector<Role> claim_of_player_;  // x player, current claims.
  vector<vector<int>> players_claiming_;  // x role, inverse of claim_of_player_
  // In player perspective, the player whose perspective this is.
  int perspective_player_;
  Role perspective_player_shown_token_;
  vector<bool> night_action_used_;  // x role, true when ability was used.

  // These variables are only used in the storyteller perspective.
  vector<Role> st_player_roles_;  // Current roles.
  vector<Role> st_shown_tokens_;  // Current shown tokens.
  int st_red_herring_;
  int st_poisoner_pick_;  // A player index (or kNoPlayer) for last night pick.
  int st_imp_pick_;  // A player index (or kNoPlayer) for last night pick.
  int st_monk_pick_;  // A player index (or kNoPlayer) for last night pick.
  int st_butler_pick_;  // A player index (or kNoPlayer) for last night pick.

  // OR-Tools related variables: compiling BOTC to SAT.
  CpModelBuilder model_;
  vector<BoolVar> roles_in_play_;  // x role.
  vector<vector<vector<BoolVar>>> day_roles_;  // x day x player x role.
  vector<vector<vector<BoolVar>>> night_roles_;  // x night x player x role.
  vector<vector<BoolVar>> shown_token_;  // x player x role (night 1 only).
  vector<BoolVar> red_herring_;  // x player.
  vector<vector<BoolVar>> imp_pick_;  // x night x player, starting night 2.
  vector<vector<BoolVar>> poisoner_pick_;  // x night x player.
  vector<vector<BoolVar>> monk_pick_;  // x night x player (if Monk is claimed).
  vector<vector<BoolVar>> butler_pick_;  // x night x player (if it is claimed).
  vector<BoolVar> is_evil_;  // x player.
};

// Syntactic sugar.
SolverRequest FromCurrentRoles(const unordered_map<string, Role>& player_roles);
SolverRequest FromNotInPlayRoles(absl::Span<const Role> roles);

}  // namespace botc

#endif  // SRC_GAME_H_
