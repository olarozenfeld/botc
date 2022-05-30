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

#ifndef SRC_GAME_SAT_SOLVER_H_
#define SRC_GAME_SAT_SOLVER_H_

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>

#include "absl/strings/str_format.h"
#include "src/game_log.pb.h"
#include "src/game_state.h"
#include "src/model_wrapper.h"
#include "src/solver.pb.h"
#include "ortools/sat/cp_model.h"

namespace botc {

using operations_research::sat::CpSolverResponse;
using operations_research::sat::CpModelBuilder;
using operations_research::sat::BoolVar;
using std::cout;
using std::endl;
using std::filesystem::path;
using std::string;
using std::vector;
using std::map;
using std::pair;
using std::unordered_map;

// Compiles a GameState into a SAT model and solves it.
class GameSatSolver {
 public:
  explicit GameSatSolver(const GameState& g) : g_(g), script_(g.GetScript()) {
    CompileSatModel();
  }
  // Solves the game and returns all valid worlds.
  SolverResponse Solve() { return Solve(SolverRequest()); }
  // Solves the game using options from the request.
  SolverResponse Solve(const SolverRequest& request);
  // Returns whether a valid world exists.
  bool IsValidWorld() { return IsValidWorld(SolverRequest()); }
  // Returns whether a valid world exists given all assumptions in the request.
  bool IsValidWorld(const SolverRequest& request) {
    SolverRequest r = request;
    r.set_stop_after_first_solution(true);
    return Solve(r).worlds_size() > 0;
  }
  void WriteModelToFile(const path& filename) const {
    model_.WriteToFile(filename);
  }
  void WriteModelVariablesToFile(const path& filename) const {
    model_.WriteVariablesToFile(filename);
  }

 private:
  typedef void (GameSatSolver::*AddRoleConstraints)();

  const AddRoleConstraints kAddRoleConstraints[Role_ARRAYSIZE] = {
    &GameSatSolver::Noop,  // ROLE_UNSPECIFIED
    // Trouble Brewing roles:
    &GameSatSolver::AddWasherwomanConstraints,  // WASHERWOMAN
    &GameSatSolver::AddLibrarianConstraints,  // LIBRARIAN
    &GameSatSolver::AddInvestigatorConstraints,  // INVESTIGATOR
    &GameSatSolver::AddChefConstraints,  // CHEF
    &GameSatSolver::AddEmpathConstraints,  // EMPATH
    &GameSatSolver::AddFortuneTellerConstraints,  // FORTUNE_TELLER
    &GameSatSolver::AddUndertakerConstraints,  // UNDERTAKER
    &GameSatSolver::Noop,  // MONK (handled by the Imp logic)
    &GameSatSolver::AddRavenkeeperConstraints,  // RAVENKEEPER
    &GameSatSolver::AddVirginConstraints,  // VIRGIN
    &GameSatSolver::AddSlayerConstraints,  // SLAYER
    &GameSatSolver::Noop,  // SOLDIER (handled by the Imp logic)
    &GameSatSolver::Noop,  // MAYOR (handled by the Imp logic)
    &GameSatSolver::Noop,  // BUTLER (ignored)
    &GameSatSolver::Noop,  // DRUNK (handled by every Townsfolk role logic)
    &GameSatSolver::Noop,  // RECLUSE (handled by info roles logic)
    &GameSatSolver::Noop,  // SAINT (handled by game end logic)
    &GameSatSolver::AddPoisonerConstraints,  // POISONER
    &GameSatSolver::AddSpyConstraints,  // SPY
    &GameSatSolver::Noop,  // SCARLET_WOMAN (handled by propagation logic)
    &GameSatSolver::Noop,  // BARON (handled by role setup logic)
    &GameSatSolver::AddImpConstraints,  // IMP
  };

  void CompileSatModel();

  // Compiling role constraints.
  void Noop() {}  // Already covered by other methods.
  void AddWasherwomanConstraints();
  void AddLibrarianConstraints();
  void AddInvestigatorConstraints();
  void AddChefConstraints();
  void AddEmpathConstraints();
  void AddFortuneTellerConstraints();
  void AddUndertakerConstraints();
  void AddMonkConstraints();
  void AddRavenkeeperConstraints();
  void AddVirginConstraints();
  void AddSlayerConstraints();
  void AddPoisonerConstraints();
  void AddSpyConstraints();
  void AddImpConstraints();

  // Helper functions.
  void AddRoleSetupConstraints();
  void AddShownTokenConstraints();
  void AddRoleClaimsConstraints();
  void AddChefConstraints(int chef, int chef_number);
  void AddEmpathConstraints(int player, int number, const Time& time);
  void AddFortuneTellerConstraints(
      int player, int pick1, int pick2, bool yes, const Time& time);
  void AddVirginConstraints(
      int nominator, int nominee, const Time& time, bool virgin_proc);
  void AddRolePropagationConstraints(const Time& time);
  void AddScarletWomanProcConstraints(const Time& time);
  void AddImpStarpassConstraints(const Time& time);
  void AddImpConstraints(const Time& time,
                         const internal::RoleAction* imp_action,
                         vector<const internal::RoleAction*> imp_action_claims);
  void AddImpActionConstraints(const internal::RoleAction& ra);
  void AddImpActionClaimConstraints(const internal::RoleAction& ra);
  void AddPresolveConstraints();
  void AddPresolveRedHerringConstraints();
  void AddPresolvePoisonerConstraints();
  void AddGameEndConstraints();
  void AddNoVictoryConstraints(const Time& time);
  void AddGoodWonConstraints();
  void AddEvilWonConstraints();
  void AddDemonInfoConstraints();
  void AddMinionInfoConstraints();
  void AddLearningRoleInfoConstraints(Role role);
  void AddLearningRoleInfoConstraints(const internal::RoleAction& ra);

  // Syntactic-sugar-type helper functions.
  vector<int> AliveRolePossibilities(Role role, const Time& time) const;
  bool IsRolePossible(int player, Role role, const Time& time) const {
    return (g_.IsRolePossible(player, role, time) &&
            (!IsGoodRole(role) ||  // Good roles have to claim.
             role_claims_[player][time.count - 1] == role));
  }
  bool IsRolePossible(Role role) const {
    return !AliveRolePossibilities(role, Time::Night(1)).empty();
  }
  vector<int> PossibleMonkProtecting(int target, const Time& time) const;
  vector<BoolVar> CollectRolesForPlayer(
      const Time& time, int player,
      absl::Span<const Role> roles, bool only_alive);
  vector<BoolVar> CollectRoles(const Time& time,
                               absl::Span<const Role> roles,
                               bool only_alive);
  vector<BoolVar> CollectRoles(const Time& time,
                               absl::Span<const Role> roles) {
    return CollectRoles(time, roles, false);
  }
  vector<BoolVar> CollectAliveRoles(const Time& time,
                                    absl::Span<const Role> roles) {
    return CollectRoles(time, roles, true);
  }
  void PropagateAliveRoles(const Time& from, const Time& to,
                           absl::Span<const Role> roles);
  void PropagateDeadRoles(const Time& from, const Time& to);
  void PropagateRolesForPlayer(int player, const Time& from, const Time& to,
                               absl::Span<const Role> roles);
  // SAT variable accessors (cached by the model_).
  BoolVar AliveRoleVar(Role role, const Time& from);
  string RedHerringVarName(int player) const {
    return absl::StrFormat("red_herring_%s", g_.PlayerName(player));
  }
  BoolVar RedHerringVar(int player) {
    return model_.NewVar(RedHerringVarName(player));
  }
  BoolVar RoleVar(int player, Role role, const Time& time) {
    return model_.NewVar(absl::StrFormat("role_%s_%s_%s", g_.PlayerName(player),
                                         Role_Name(role), time));
  }
  BoolVar RoleInPlayVar(Role role);
  BoolVar StartingEvilVar(int player);
  BoolVar ShownTokenVar(int player, Role role) {  // Night 1 only.
    return model_.NewVar(absl::StrFormat(
      "shown_token_%s_%s", g_.PlayerName(player), Role_Name(role)));
  }
  string PoisonerPickVarName(int player, const Time& time) const {
    return absl::StrFormat(
        "poisoner_pick_%s_night_%d", g_.PlayerName(player), time.count);
  }
  BoolVar PoisonerPickVar(int player, const Time& time) {
    return model_.NewVar(PoisonerPickVarName(player, time));
  }
  // Poisoner picked and the poisoner is alive.
  BoolVar PoisonedVar(int player, const Time& time);

  vector<BoolVar> CollectAssumptionLiterals(
      const SolverRequest::Assumptions& assumptions);
  void FillWorldFromSolverResponse(const CpSolverResponse& response,
                                   SolverResponse::World* world);
  int SolutionAliveDemon(const CpSolverResponse& response);

  // The old implementation of Solve. The faster one will be eventually picked.
  // SolveIteration calls sat::Solve in a loop while updating the model.
  SolverResponse SolveIteration(const SolverRequest& request);

  const GameState& g_;  // Current game state.
  const Script script_;  // Part of g_, replicated for convenience.
  // Preprocessed game state variables.
  vector<vector<Role>> role_claims_;  // x player, night
  unordered_map<Role, vector<vector<const internal::RoleAction*>>>
      role_action_claims_;
  vector<Role> starting_role_claims_;
  ModelWrapper model_;  // SAT model (caches all SAT variables).
};

// Syntactic sugar for simplifying creating SolverRequests.
class SolverRequestBuilder {
 public:
  SolverRequestBuilder() {}
  explicit SolverRequestBuilder(
      const SolverRequest& request):request_(request) {}

  SolverRequestBuilder& AddStartingRoles(const string& player, Role role) {
    return AddStartingRoles({{player, role}});
  }
  SolverRequestBuilder& AddStartingRoles(
      const unordered_map<string, Role>& player_roles);
  SolverRequestBuilder& AddStartingRolesNot(const string& player, Role role) {
    return AddStartingRolesNot({{player, role}});
  }
  SolverRequestBuilder& AddStartingRolesNot(
      const unordered_map<string, Role>& player_roles);
  SolverRequestBuilder& AddCurrentRoles(const string& player, Role role) {
    return AddCurrentRoles({{player, role}});
  }
  SolverRequestBuilder& AddCurrentRoles(
      const unordered_map<string, Role>& player_roles);
  SolverRequestBuilder& AddCurrentRolesNot(const string& player, Role role) {
    return AddCurrentRolesNot({{player, role}});
  }
  SolverRequestBuilder& AddCurrentRolesNot(
      absl::Span<const pair<string, Role>> player_roles);
  SolverRequestBuilder& AddRolesInPlay(absl::Span<const Role> roles);
  SolverRequestBuilder& AddRolesNotInPlay(absl::Span<const Role> roles);
  SolverRequestBuilder& AddGood(absl::Span<const string> players);
  SolverRequestBuilder& AddEvil(absl::Span<const string> players);
  SolverRequestBuilder& AddPoisoned(const string& player,
                                    int night,
                                    bool is_poisoned) {
    auto *p = request_.mutable_assumptions()->add_poisoned_players();
    p->set_player(player);
    p->set_night(night);
    p->set_is_not(!is_poisoned);
    return *this;
  }
  SolverRequestBuilder& AddPoisoned(const string& player, int night) {
    return AddPoisoned(player, night, true);
  }
  SolverRequestBuilder& AddHealthy(const string& player, int night) {
    return AddPoisoned(player, night, false);
  }

  SolverRequest Build() const { return request_; }

  static SolverRequest FromCurrentRoles(
      const unordered_map<string, Role>& player_roles) {
    return SolverRequestBuilder().AddCurrentRoles(player_roles).Build();
  }
  static SolverRequest FromCurrentRoles(const string& player, Role role) {
    return FromCurrentRoles({{player, role}});
  }
  static SolverRequest FromCurrentRolesNot(
      absl::Span<const pair<string, Role>> player_roles) {
    return SolverRequestBuilder().AddCurrentRolesNot(player_roles).Build();
  }
  static SolverRequest FromCurrentRolesNot(const string& player, Role role) {
    return FromCurrentRolesNot({{player, role}});
  }

 private:
  SolverRequest request_;
};

// Solves the game and returns all valid worlds.
SolverResponse Solve(const GameState& g);
// Solves the game using options from the request.
SolverResponse Solve(const GameState& g, const SolverRequest& request);
// Returns whether a valid world exists.
bool IsValidWorld(const GameState& g);
// Returns whether a valid world exists given all assumptions in the request.
bool IsValidWorld(const GameState& g, const SolverRequest& request);

}  // namespace botc

#endif  // SRC_GAME_SAT_SOLVER_H_
