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

// We use a few simplifying assumptions for the Blood On The Clocktower solver:
// * The game needs to be fully claimed before solving. Meaning, it should be
//   akin to a final 3 situation, where player's most recent claims about their
//   role and info are trusted, meaning they are either honest or Evil.
// * The Minions (e.g. Spy or Scarlet Woman) cannot be poisoned. (It would
//   always be strictly bad for Evil to poison a fellow minion, so we ignore
//   this option).
// * Demon bluffs will not be shown to the Drunk.
// * The game can only be solved at certain time points. Namely:
//   * It must be daytime.
//   * Nominations must be resolved (the full nominate-vote-execution-death
//     path must be completed as applicable).
//   * All pertinent Storyteller announcements are assumed to have been made
//     (such as night deaths, or victory announcements, or the result of the
//     latest execution or Slayer shot attempt).
//   * When solving during final 3 day, with no-one on the block, we assume that
//     nominations are not yet closed for the day. Meaning, we assume that a
//     Mayor win might still be possible, just wasn't announced yet.
#include "src/game_sat_solver.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <unordered_set>

#include "ortools/base/logging.h"
#include "ortools/sat/cp_model.pb.h"
#include "ortools/sat/cp_model_solver.h"
#include "ortools/sat/model.h"
#include "ortools/sat/sat_parameters.pb.h"
#include "src/util.h"

namespace botc {

using operations_research::sat::CpSolverStatus;
using operations_research::sat::LinearExpr;
using operations_research::sat::NewFeasibleSolutionObserver;
using operations_research::sat::SatParameters;
using std::ofstream;

void GameSatSolver::CompileSatModel() {
  CHECK(g_.CurrentTime().is_day) << "Can only solve during the day";
  // Solver simplifying assumptions: the game state is fully claimed, and at
  // this point everyone is either telling the truth or is Evil.
  const auto st = g_.IsFullyClaimed();
  CHECK(st.ok()) << st;

  role_claims_ = g_.GetRoleClaimsByNight();
  for (int i = 0; i < g_.NumPlayers(); ++i) {
    const auto& claims = role_claims_[i];
    for (int d = 1; d < claims.size(); ++d) {
      CHECK(claims[d - 1] == claims[d] ||
            (claims[d - 1] == RECLUSE && claims[d] == IMP))
          << absl::StrFormat("Inconsistent claims: %s claimed to be %s on day "
                             "%d and %s on %d", g_.PlayerName(i),
                             Role_Name(claims[d - 1]), d, Role_Name(claims[d]),
                             d + 1);
    }
    starting_role_claims_.push_back(
        claims.empty() ? ROLE_UNSPECIFIED : claims[0]);
  }
  role_action_claims_ = g_.GetRoleActionClaimsByNight();
  AddRoleSetupConstraints();
  AddShownTokenConstraints();
  AddRoleClaimsConstraints();
  for (Role role : AllRoles(script_)) {
    (this->*(kAddRoleConstraints[role]))();
  }
  AddGameEndConstraints();
  AddPresolveConstraints();
}

void GameSatSolver::AddWasherwomanConstraints() {
  AddLearningRoleInfoConstraints(WASHERWOMAN);
}

void GameSatSolver::AddLibrarianConstraints() {
  AddLearningRoleInfoConstraints(LIBRARIAN);
}

void GameSatSolver::AddInvestigatorConstraints() {
  AddLearningRoleInfoConstraints(INVESTIGATOR);
}

void GameSatSolver::AddChefConstraints() {
  const auto it = role_action_claims_.find(CHEF);
  if (it == role_action_claims_.end()) {
    return;
  }
  for (const auto& actions : it->second) {
    for (const auto* ra : actions) {
      AddChefConstraints(ra->player, ra->number);
    }
  }
}

void GameSatSolver::AddChefConstraints(int chef, int chef_number) {
  const Time night1 = Time::Night(1);
  /* Puzzle: why does adding this make the BaronPerspectiveThreeMinions test
     15 times slower????
  if (!IsRolePossible(chef, CHEF, night1)) {
    return;  // We know it's a bluff.
  }*/
  vector<BoolVar> registered_evil;  // How everyone registered to the Chef.
  for (int i = 0; i < g_.NumPlayers(); ++i) {
    BoolVar reg_evil_i = (i == chef ? model_.FalseVar() :  // Chef is good
        model_.NewVar(
            absl::StrFormat("chef_%s_registered_evil_%s",
                            g_.PlayerName(chef), g_.PlayerName(i))));
    registered_evil.push_back(reg_evil_i);
    if (i != chef) {
      vector<BoolVar> evil_options_i({StartingEvilVar(i)});
      if (role_claims_[i][0] == RECLUSE) {
        evil_options_i.push_back(
            model_.NewEquivalentVarAnd(
                {RoleVar(i, RECLUSE, night1), Not(PoisonerPickVar(i, night1))},
                absl::StrFormat("healthy_recluse_%s_%s", g_.PlayerName(i),
                                night1)));
      }
      model_.AddImplicationOr(reg_evil_i, evil_options_i);
      // We assume a Spy cannot be poisoned.
      model_.AddImplicationOr(
          Not(reg_evil_i),
          {Not(StartingEvilVar(i)), RoleVar(i, SPY, night1)});
    }
  }
  vector<BoolVar> evil_pairs;
  for (int i = 0; i < g_.NumPlayers(); ++i) {
    int j = (i + 1) % g_.NumPlayers();
    evil_pairs.push_back(
        model_.NewEquivalentVarAnd(
            {registered_evil[i], registered_evil[j]},
            absl::StrFormat("chef_evil_pair_%s_%s", g_.PlayerName(i),
                            g_.PlayerName(j))));
  }
  BoolVar correct = model_.NewEquivalentVarSumEq(
      evil_pairs, chef_number, absl::StrFormat(
          "chef_%s_number_%d", g_.PlayerName(chef), chef_number));
  model_.AddOr(
    {Not(RoleVar(chef, CHEF, night1)), PoisonerPickVar(chef, night1), correct});
}

void GameSatSolver::AddEmpathConstraints() {
  const auto it = role_action_claims_.find(EMPATH);
  if (it == role_action_claims_.end()) {
    return;
  }
  for (const auto& actions : it->second) {
    for (const auto* ra : actions) {
      AddEmpathConstraints(ra->player, ra->number, ra->time);
    }
  }
}

void GameSatSolver::AddEmpathConstraints(
    int player, int number, const Time& time) {
  vector<int> alive_neighbors = g_.AliveNeighbors(player, time);  // Size 2.
  const int ping1 = alive_neighbors[0], ping2 = alive_neighbors[1];
  // We assume a Spy will not be poisoned. But Empath goes after the Imp, so
  // need to check day role.
  BoolVar ping1_regs_good = model_.NewEquivalentVarOr(
      {Not(StartingEvilVar(ping1)), RoleVar(ping1, SPY, time + 1)},
      absl::StrFormat("empath_%s_registers_%s_good_%s", g_.PlayerName(player),
                      g_.PlayerName(ping1), time));
  BoolVar ping2_regs_good = model_.NewEquivalentVarOr(
      {Not(StartingEvilVar(ping2)), RoleVar(ping2, SPY, time + 1)},
      absl::StrFormat("empath_%s_registers_%s_good_%s", g_.PlayerName(player),
                      g_.PlayerName(ping2), time));
  vector<BoolVar> ping1_regs_evil_cases({StartingEvilVar(ping1)});
  if (IsRolePossible(ping1, RECLUSE, time)) {
    ping1_regs_evil_cases.push_back(model_.NewEquivalentVarAnd(
        {RoleVar(ping1, RECLUSE, time), Not(PoisonedVar(ping1, time))},
        absl::StrFormat("healthy_recluse_%s_%s", g_.PlayerName(ping1), time)));
  }
  BoolVar ping1_regs_evil = model_.NewEquivalentVarOr(
      ping1_regs_evil_cases,
      absl::StrFormat("empath_%s_registers_%s_evil_%s", g_.PlayerName(player),
                      g_.PlayerName(ping1), time));
  vector<BoolVar> ping2_regs_evil_cases({StartingEvilVar(ping2)});
  if (IsRolePossible(ping2, RECLUSE, time)) {
    ping2_regs_evil_cases.push_back(model_.NewEquivalentVarAnd(
        {RoleVar(ping2, RECLUSE, time), Not(PoisonedVar(ping2, time))},
        absl::StrFormat("healthy_recluse_%s_%s", g_.PlayerName(ping2), time)));
  }
  BoolVar ping2_regs_evil = model_.NewEquivalentVarOr(
      ping2_regs_evil_cases,
      absl::StrFormat("empath_%s_registers_%s_evil_%s", g_.PlayerName(player),
                      g_.PlayerName(ping2), time));
  // A healthy Recluse *may* register as evil.
  // A healthy alive Spy *may* register as good.
  vector<BoolVar> cases({
      Not(RoleVar(player, EMPATH, time)), PoisonedVar(player, time)});
  switch (number) {
    case 0:
      cases.push_back(model_.NewEquivalentVarAnd(
          {ping1_regs_good, ping2_regs_good},
          absl::StrFormat(
            "empath_0_%s_on_%s_and_%s_%s", g_.PlayerName(player),
            g_.PlayerName(ping1), g_.PlayerName(ping2), time)));
      break;
    case 1:
      cases.push_back(model_.NewEquivalentVarAnd(
          {ping1_regs_good, ping2_regs_evil},
          absl::StrFormat(
            "empath_1_%s_on_%s_and_%s_%s_case1", g_.PlayerName(player),
            g_.PlayerName(ping1), g_.PlayerName(ping2), time)));
      cases.push_back(model_.NewEquivalentVarAnd(
          {ping1_regs_evil, ping2_regs_good},
          absl::StrFormat(
            "empath_1_%s_on_%s_and_%s_%s_case2", g_.PlayerName(player),
            g_.PlayerName(ping1), g_.PlayerName(ping2), time)));
      break;
    case 2:
      cases.push_back(model_.NewEquivalentVarAnd(
          {ping1_regs_evil, ping2_regs_evil},
          absl::StrFormat(
            "empath_2_%s_on_%s_and_%s_%s", g_.PlayerName(player),
            g_.PlayerName(ping1), g_.PlayerName(ping2), time)));
      break;
    default:
      // Empath is definitely lying, drunk or poisoned.
      break;
  }
  model_.AddOr(cases);
}

void GameSatSolver::AddFortuneTellerConstraints() {
  const auto it = role_action_claims_.find(FORTUNE_TELLER);
  if (it == role_action_claims_.end()) {
    return;
  }
  for (const auto& actions : it->second) {
    for (const auto* ra : actions) {
      AddFortuneTellerConstraints(
          ra->player, ra->players[0], ra->players[1], ra->yes, ra->time);
    }
  }
}

void GameSatSolver::AddFortuneTellerConstraints(
    int player, int pick1, int pick2, bool yes, const Time& time) {
  vector<BoolVar> yes_options({
      // Fortune Teller goes after the Imp, so we need to check day roles.
      RoleVar(pick1, IMP, time + 1), RedHerringVar(pick1),
      RoleVar(pick2, IMP, time + 1), RedHerringVar(pick2)});
  BoolVar poisoned_recluse;
  if (yes) {  // We can only infer Recluse possibilities from Yes answer.
    for (int pick : {pick1, pick2}) {
      if (IsRolePossible(pick, RECLUSE, time)) {
        yes_options.push_back(model_.NewEquivalentVarAnd(
          {RoleVar(pick, RECLUSE, time), Not(PoisonedVar(pick, time))},
          absl::StrFormat("healthy_recluse_%s_%s", g_.PlayerName(pick), time)));
      }
    }
  }
  vector<BoolVar> cases({
      Not(RoleVar(player, FORTUNE_TELLER, time)), PoisonedVar(player, time)});
  BoolVar is_yes = model_.NewEquivalentVarOr(
      yes_options, absl::StrFormat("fortune_teller_yes_cases_%s", time));
  cases.push_back(yes ? is_yes : Not(is_yes));
  model_.AddOr(cases);
}

void GameSatSolver::AddUndertakerConstraints() {
  AddLearningRoleInfoConstraints(UNDERTAKER);
}

void GameSatSolver::AddRavenkeeperConstraints() {
  AddLearningRoleInfoConstraints(RAVENKEEPER);
}

void GameSatSolver::AddVirginConstraints() {
  // Everyone who claimed Virgin, was nominated, was alive, and was not
  // nominated earlier could trigger a Virgin proc:
  vector<bool> nominated(g_.NumPlayers());
  for (const auto& n : g_.Nominations()) {
    const bool possible_virgin_proc = (
        g_.IsAlive(n.nominee, n.time) &&
        !nominated[n.nominee] &&
        role_claims_[n.nominee][n.time.count - 1] == VIRGIN);
    nominated[n.nominee] = true;
    if (possible_virgin_proc) {
      AddVirginConstraints(n.nominator, n.nominee, n.time, n.virgin_proc);
    }
  }
}

void GameSatSolver::AddVirginConstraints(
    int nominator, int nominee, const Time& time, bool virgin_proc) {
  vector<BoolVar> townsfolk_cases = CollectRolesForPlayer(
      time, nominator, TownsfolkRoles(script_), true);
  if (virgin_proc) {
    townsfolk_cases.push_back(RoleVar(nominator, SPY, time));
  }
  BoolVar proc_townsfolk = model_.NewEquivalentVarSum(
      townsfolk_cases,
      absl::StrFormat("%s_registers_townsfolk_to_virgin_%s",
                      g_.PlayerName(nominator), time));
  BoolVar virgin = RoleVar(nominee, VIRGIN, time);
  BoolVar poisoned = PoisonedVar(nominee, time);
  if (virgin_proc) {
    model_.AddAnd({virgin, Not(poisoned), proc_townsfolk});
  } else {
    model_.AddOr({Not(virgin), poisoned, Not(proc_townsfolk)});
  }
}

void GameSatSolver::AddSlayerConstraints() {
  for (const auto* ra : g_.GetRoleActions(SLAYER)) {
    const int target = ra->players[0], slayer = ra->player;
    const Time& time = ra->time;
    if (ra->yes) {
      vector<BoolVar> cases({RoleVar(slayer, SLAYER, time),
                             Not(PoisonedVar(slayer, time))});
      if (IsRolePossible(target, RECLUSE, time)) {
        BoolVar healthy_recluse = model_.NewEquivalentVarAnd(
            {RoleVar(target, RECLUSE, time), Not(PoisonedVar(target, time))},
            absl::StrFormat("healthy_recluse_%s_%s", g_.PlayerName(target),
                            time));
        cases.push_back(model_.NewEquivalentVarOr(
            {RoleVar(target, IMP, time), healthy_recluse},
            absl::StrFormat("healthy_recluse_or_imp_%s_%s",
                            g_.PlayerName(target), time)));
      } else {
        cases.push_back(RoleVar(target, IMP, time));
      }
      model_.AddAnd(cases);
    } else {
      model_.AddOr({Not(RoleVar(slayer, SLAYER, time)),
                    PoisonedVar(slayer, time),
                    Not(RoleVar(target, IMP, time))});
    }
  }
}

void GameSatSolver::AddPoisonerConstraints() {
  // Most of the constraints are added in pre-solve when all Poisoner pick
  // variables are created. This only takes care of the Poisoner or Storyteller
  // perspective.
  for (const auto* ra : g_.GetRoleActions(POISONER)) {
    const int target = ra->players[0];
    model_.AddEquality(PoisonerPickVar(target, ra->time), true);
    // If there are night deaths, the poisoned player cannot be an alive
    // demon (Poisoner goes before all demon roles). This is only true in
    // scripts without other possible night deaths, such as Trouble Brewing.
    if (!g_.Deaths(ra->time).empty() && g_.IsAlive(target, ra->time)) {
      for (Role role : DemonRoles(script_)) {
        if (g_.IsRolePossible(target, role, ra->time)) {
          model_.AddEquality(RoleVar(target, role, ra->time), false);
        }
      }
    }
  }
}

void GameSatSolver::AddSpyConstraints() {
  for (const auto* ra : g_.GetRoleActions(SPY)) {
    for (const auto& pi : ra->grimoire_info.player_info()) {
      const auto& tokens = pi.tokens();
      const bool is_drunk = std::find(
          tokens.begin(), tokens.end(), IS_DRUNK) != tokens.end();
      const Role role = is_drunk ? DRUNK : pi.role();
      const int i = g_.PlayerIndex(pi.player());
      // We assume the Spy cannot be poisoned, so the info is correct.
      // The info pertains to the day roles.
      model_.AddEquality(RoleVar(i, role, ra->time + 1), true);
    }
  }
}

void GameSatSolver::AddImpConstraints() {
  if (g_.CurrentTime() <= Time::Night(2)) {
    return;  // Day 2 is the first day we might get an Imp kill.
  }
  // We are dealing with Imp actions and Imp action claims symmetrically.
  // For a given time, there could be at most one Imp action, and many claims.
  const auto imp_actions = g_.GetRoleActions(IMP);
  vector<const internal::RoleAction*> imp_actions_by_night(
      g_.CurrentTime().count);
  for (auto* ra : imp_actions) {
    imp_actions_by_night[ra->time.count - 1] = ra;
  }
  const auto it = role_action_claims_.find(IMP);
  const vector<const internal::RoleAction*> empty;
  for (Time time = Time::Night(2); time < g_.CurrentTime(); time += 2) {
    AddImpConstraints(
        time, imp_actions_by_night[time.count - 1],
        it == role_action_claims_.end() ? empty : it->second[time.count - 1]);
  }
}

BoolVar GameSatSolver::RoleInPlayVar(Role role) {
  vector<BoolVar> player_is_role;
  for (int i = 0; i < g_.NumPlayers(); ++i) {
    player_is_role.push_back(RoleVar(i, role, Time::Night(1)));
  }
  return model_.NewEquivalentVarSum(
      player_is_role, absl::StrFormat("in_play_%s", Role_Name(role)));
}

BoolVar GameSatSolver::StartingEvilVar(int player) {
  vector<BoolVar> evil_roles;
  for (Role role : EvilRoles(script_)) {
    evil_roles.push_back(RoleVar(player, role, Time::Night(1)));
  }
  return model_.NewEquivalentVarSum(
      evil_roles, absl::StrFormat("starting_evil_%s", g_.PlayerName(player)));
}

BoolVar GameSatSolver::AliveRoleVar(Role role, const Time& time) {
  return model_.NewEquivalentVarSum(
      CollectAliveRoles(time, {role}),
      absl::StrFormat("alive_%s_%s", Role_Name(role), time));
}

BoolVar GameSatSolver::PoisonedVar(int player, const Time& time) {
  Time night = time.is_day ? time - 1 : time;
  BoolVar picked = PoisonerPickVar(player, time);
  const auto night_deaths = g_.Deaths(night);
  if (night_deaths.empty()) {
    return picked;
  }
  // At most one night death in TB:
  return model_.NewEquivalentVarAnd(
      {Not(RoleVar(night_deaths[0], POISONER, night)), picked},
      absl::StrFormat("poisoned_%s_%s", g_.PlayerName(player), night));
}

void GameSatSolver::AddRoleSetupConstraints() {
  vector<BoolVar> demons = CollectRoles(Time::Night(1), DemonRoles(script_));
  model_.AddEqualitySum(demons, 1);
  vector<BoolVar> minions = CollectRoles(Time::Night(1), MinionRoles(script_));
  model_.AddEqualitySum(minions, g_.NumMinions());
  const BoolVar& baron_in_play = RoleInPlayVar(BARON);
  vector<BoolVar> outsiders = CollectRoles(
      Time::Night(1), OutsiderRoles(script_));
  vector<BoolVar> townsfolk = CollectRoles(
      Time::Night(1), TownsfolkRoles(script_));
  model_.AddImplicationSum(Not(baron_in_play), outsiders, g_.NumOutsiders());
  model_.AddImplicationSum(baron_in_play, outsiders, g_.NumOutsiders() + 2);
  model_.AddImplicationSum(Not(baron_in_play), townsfolk, g_.NumTownsfolk());
  model_.AddImplicationSum(baron_in_play, townsfolk, g_.NumTownsfolk() - 2);
  for (Time time = Time::Night(1); time <= g_.CurrentTime(); ++time) {
    for (Role role : AllRoles(script_)) {
      // Each role other than IMP assigned to at most one player at a time:
      if (role != IMP) {
        model_.AddAtMostOne(CollectRoles(time, {role}));
      }
    }
    for (int i = 0; i < g_.NumPlayers(); ++i) {
      if (g_.GetPerspective() == STORYTELLER) {
        // Fix all the roles to the actual roles.
        for (Role role : AllRoles(script_)) {
          model_.AddEquality(
              RoleVar(i, role, time), role == g_.GetRole(i, time));
        }
      }
      // Each player assigned exactly one role at a time:
      model_.AddEqualitySum(
          CollectRolesForPlayer(time, i, AllRoles(script_), false), 1);
    }
    AddRolePropagationConstraints(time);
  }
  AddDemonInfoConstraints();
  AddMinionInfoConstraints();
}

void GameSatSolver::AddRolePropagationConstraints(const Time& time) {
  if (time >= g_.CurrentTime()) {
    return;  // Nothing to propagate.
  }
  PropagateDeadRoles(time, time + 1);  // In TB, dead don't change roles.
  if (time.is_day) {
    AddScarletWomanProcConstraints(time);
  } else {
    AddImpStarpassConstraints(time);
  }
}

void GameSatSolver::AddRoleClaimsConstraints() {
  if (role_claims_[0].empty()) {
    return;
  }
  // The role tokens are shown at night, but except the SW the role change
  // begins the next day.
  // We assume that role X claimed night 1 -> claimer was shown X or is evil.
  for (int i = 0; i < g_.NumPlayers(); ++i) {
    const Role role = role_claims_[i][0];
    if (role != ROLE_UNSPECIFIED) {
      model_.AddOr({ShownTokenVar(i, role), StartingEvilVar(i)});
    }
  }
  // Imp starpass claims (could happen if Recluse in play).
  for (int i = 0; i < g_.NumPlayers(); ++i) {
    for (int n = 0; n < role_claims_[i].size(); ++n) {
      const Role role = role_claims_[i][n];
      if (role != ROLE_UNSPECIFIED && IsEvilRole(role)) {
        model_.AddOr({RoleVar(i, role, Time::Day(n + 1)), StartingEvilVar(i)});
      }
    }
  }
}

void GameSatSolver::AddPresolveConstraints() {
  AddPresolveRedHerringConstraints();
  AddPresolvePoisonerConstraints();
}

void GameSatSolver::AddPresolveRedHerringConstraints() {
  if (g_.RedHerring() == kNoPlayer && !IsRolePossible(FORTUNE_TELLER)) {
    return;
  }
  // If Fortune Teller is in play, exactly one red herring. This means no
  // additional constraints if the number of red herring variables so far
  // defined is less than the number of players.
  vector<BoolVar> red_herring;
  vector<BoolVar> remaining_good;
  const BoolVar& ft_in_play = RoleInPlayVar(FORTUNE_TELLER);
  for (int i = 0; i < g_.NumPlayers(); ++i) {
    if (g_.GetPerspective() == STORYTELLER) {
      model_.AddEquality(RedHerringVar(i), g_.RedHerring() == i);
    }
    const BoolVar* red_herring_i = model_.FindVar(RedHerringVarName(i));
    if (red_herring_i == nullptr) {
      remaining_good.push_back(Not(StartingEvilVar(i)));
      continue;
    }
    red_herring.push_back(*red_herring_i);
    // Only a Good player can be a red herring.
    model_.AddImplication(*red_herring_i, Not(StartingEvilVar(i)));
    model_.AddImplication(Not(ft_in_play), Not(*red_herring_i));
  }
  // If a Fortune Teller is in play, there is exactly one red herring.
  // What this means for the case of less variables than players, is that
  // if the red herring is some other player, there must be a Good among these
  // other players. This will add no constraints unless the number of variables
  // is exactly such that it's possible to fail at this.
  if (red_herring.size() == g_.NumPlayers()) {
    model_.AddImplicationSum(ft_in_play, red_herring, 1);
  } else if (g_.NumPlayers() - red_herring.size() <= 1 + g_.NumMinions()) {
    // If FT is in play and no red herring among the existing variables, one of
    // the remaining ones must be the red herring and therefore be Good.
    vector<BoolVar> other(Not(red_herring));
    other.push_back(ft_in_play);
    BoolVar v = model_.NewEquivalentVarAnd(other, "red_herring_other");
    model_.AddImplicationOr(v, remaining_good);
  }
}

void GameSatSolver::AddPresolvePoisonerConstraints() {
  for (Time time = Time::Night(1); time <= g_.CurrentTime(); time += 2) {
    vector<BoolVar> poisoner_picks;
    for (int i = 0; i < g_.NumPlayers(); ++i) {
      const BoolVar* pick = model_.FindVar(PoisonerPickVarName(i, time));
      if (pick != nullptr) {
        poisoner_picks.push_back(*pick);
      }
    }
    if (poisoner_picks.empty()) {
      continue;
    }
    model_.AddAtMostOne(poisoner_picks);
    model_.AddImplicationAnd(
        Not(AliveRoleVar(POISONER, time)), Not(poisoner_picks));
  }
}

void GameSatSolver::AddNoVictoryConstraints(const Time& time) {
  // Exactly one alive demon at the start of the day.
  model_.AddEqualitySum(CollectRoles(time, DemonRoles(script_), true), 1);
  int num_alive = g_.NumAlive(time);
  if (num_alive <= 2) {
    model_.AddContradiction(absl::StrFormat(
        "%d players alive on %s, yet game is not over", num_alive, time));
    return;
  }
  vector<BoolVar> cases;
  // No Mayor win.
  if (num_alive - g_.Deaths(time).size() == 3 &&
      g_.Execution(time) == kNoPlayer) {
    // Mayor was not alive or poisoned.
    for (int mayor : AliveRolePossibilities(MAYOR, time)) {
      cases.push_back(model_.NewEquivalentVarOr(
          {Not(RoleVar(mayor, MAYOR, time)), PoisonedVar(mayor, time)},
          absl::StrFormat("not_healthy_mayor_%s_%s", g_.PlayerName(mayor),
                          time)));
    }
  }
  BoolVar sw_alive = model_.NewEquivalentVarOr(
      CollectAliveRoles(time, {SCARLET_WOMAN}),
      absl::StrFormat("sw_alive_%s", time));
  for (int death : g_.Deaths(time)) {
    --num_alive;
    // Did not kill the demon, or a possible Scarlet Woman proc.
    vector<BoolVar> demon_kill_cases({Not(RoleVar(death, IMP, time))});
    if (num_alive >= 4) {
      demon_kill_cases.push_back(sw_alive);
    }
    cases.push_back(model_.NewEquivalentVarOr(
        demon_kill_cases, absl::StrFormat("not_imp_%s_killed_no_sw_save_%s",
                                          g_.PlayerName(death), time)));
  }
  model_.AddAnd(cases);
  // Did not execute a healthy Saint.
  int execution_death = g_.ExecutionDeath(time);
  if (execution_death != kNoPlayer &&
      IsRolePossible(execution_death, SAINT, time)) {
    model_.AddOr({Not(RoleVar(execution_death, SAINT, time)),
                  PoisonedVar(execution_death, time)});
  }
}

void GameSatSolver::AddGoodWonConstraints() {
  Time time = g_.CurrentTime();
  vector<BoolVar> cases;
  // * Mayor win
  if (g_.NumAlive(time) == 3 && g_.Execution(time) == kNoPlayer) {
    for (int mayor : AliveRolePossibilities(MAYOR, time)) {
      cases.push_back(model_.NewEquivalentVarAnd(
          {RoleVar(mayor, MAYOR, time), Not(PoisonedVar(mayor, time))},
          absl::StrFormat("healthy_mayor_%s_%s", g_.PlayerName(mayor), time)));
    }
  }
  // Imp commited suicide and no starpass catch.
  cases.push_back(
      model_.NewEquivalentVarAnd(
          Not(CollectRoles(time, {IMP}, true)),
          absl::StrFormat("imp_suicide_evil_lose_%s", time)));
  int num_alive = g_.NumAlive(time);
  BoolVar sw_alive = model_.NewEquivalentVarOr(
      CollectAliveRoles(time, {SCARLET_WOMAN}),
      absl::StrFormat("sw_alive_%s", time));
  for (int death : g_.Deaths(time)) {
    // Killed the demon and no possible Scarlet Woman proc.
    vector<BoolVar> demon_kill_cases({RoleVar(death, IMP, time)});
    if (num_alive >= 5) {
      demon_kill_cases.push_back(Not(sw_alive));
    }
    cases.push_back(model_.NewEquivalentVarAnd(
        demon_kill_cases, absl::StrFormat(
            "imp_%s_killed_on_%d_no_sw_save_%s",
            g_.PlayerName(death), num_alive, time)));
    --num_alive;
  }
  model_.AddOr(cases);
}

void GameSatSolver::AddEvilWonConstraints() {
  Time time = g_.CurrentTime();
  int num_alive = g_.NumAlive(time) - g_.Deaths(time).size();
  int execution_death = g_.ExecutionDeath(time);
  if (execution_death != kNoPlayer && num_alive > 2) {
    if (!IsRolePossible(execution_death, SAINT, time)) {
      model_.AddContradiction(absl::StrFormat(
          "No reason for Evil victory on %d alive at %s", num_alive, time));
      return;
    }
    model_.AddAnd({RoleVar(execution_death, SAINT, time),
                   Not(PoisonedVar(execution_death, time))});
    return;
  }
  if (num_alive > 2) {
    model_.AddContradiction(absl::StrFormat(
        "No execution and %d players alive after %s, yet Evil wins",
        num_alive, time));
    return;
  }
  model_.AddEqualitySum(CollectRoles(time, DemonRoles(script_), true), 1);
}

void GameSatSolver::AddGameEndConstraints() {
  for (Time time = Time::Day(1); time < g_.CurrentTime(); time += 2) {
    AddNoVictoryConstraints(time);
  }
  Time time = g_.CurrentTime();
  if (!time.is_day) {
    return;
  }
  switch (g_.WinningTeam()) {
    case GOOD:
      AddGoodWonConstraints();
      return;
    case EVIL:
      AddEvilWonConstraints();
      return;
    default:
      AddNoVictoryConstraints(time);
      return;
  }
}

void GameSatSolver::AddDemonInfoConstraints() {
  const auto& demon_info = g_.GetDemonInfo();
  if (demon_info.player == kNoPlayer) {
    return;
  }
  for (int minion : demon_info.minions) {
    vector<BoolVar> minion_i;
    for (Role role : MinionRoles(script_)) {
      minion_i.push_back(RoleVar(minion, role, Time::Night(1)));
    }
    model_.AddEqualitySum(minion_i, 1);
  }
  for (Role bluff : demon_info.bluffs) {
    // Nobody can be shown any of the bluff roles on night 1 (in particular,
    // not the Drunk):
    for (int i = 0; i < g_.NumPlayers(); ++i) {
      model_.AddEquality(ShownTokenVar(i, bluff), false);
    }
  }
}

void GameSatSolver::AddMinionInfoConstraints() {
  const auto& minion_info = g_.GetMinionInfo();
  if (minion_info.player == kNoPlayer) {
    return;
  }
  vector<BoolVar> demon;
  for (Role role : DemonRoles(script_)) {
    demon.push_back(RoleVar(minion_info.demon, role, Time::Night(1)));
  }
  model_.AddEqualitySum(demon, 1);
  Role my_role = g_.ShownToken(minion_info.player, Time::Night(1));
  for (int minion : minion_info.minions) {
    vector<BoolVar> minion_i;
    for (Role role : MinionRoles(script_)) {
      if (role != my_role) {  // they are a different minion
        minion_i.push_back(RoleVar(minion, role, Time::Night(1)));
      }
    }
    model_.AddEqualitySum(minion_i, 1);
  }
}

void GameSatSolver::AddLearningRoleInfoConstraints(Role role) {
  const auto it = role_action_claims_.find(role);
  if (it == role_action_claims_.end()) {
    return;
  }
  for (const auto& actions : it->second) {
    for (const auto* ra : actions) {
      AddLearningRoleInfoConstraints(*ra);
    }
  }
}

void GameSatSolver::AddLearningRoleInfoConstraints(
    const internal::RoleAction& ra) {
  // Supports all info roles that learn that one of k players is one of
  // n roles (in TB, they are: Washerwoman, Librarian, Investigator, Undertaker,
  // and Ravenkeeper).
  /* TODO(olaola): add this back when I figure out why is it so much worse!!
  if (!IsRolePossible(ra.player, ra.acting, ra.time)) {
    return;  // We know it's a bluff.
  }*/
  vector<BoolVar> cases;
  cases.push_back(Not(RoleVar(ra.player, ra.acting, ra.time)));
  cases.push_back(PoisonedVar(ra.player, ra.time));
  if (ra.roles.empty()) {
    // Specifically a Librarian learning that there are no outsiders.
    vector<BoolVar> outsiders;
    for (Role role : OutsiderRoles(script_)) {
      outsiders.push_back(Not(RoleInPlayVar(role)));
    }
    cases.push_back(model_.NewEquivalentVarAnd(
        outsiders, absl::StrFormat("%s_LIBRARIAN_no_outsiders",
                                   g_.PlayerName(ra.player))));
  } else {
    for (Role role : ra.roles) {
      const Role false_trigger = IsGoodRole(role) ? SPY : RECLUSE;
      for (int ping : ra.players) {
        cases.push_back(RoleVar(ping, role, ra.time));
        if (!IsRolePossible(ping, false_trigger, ra.time)) {
          continue;
        }
        BoolVar ping_false = RoleVar(ping, false_trigger, ra.time);
        cases.push_back(IsMinionRole(false_trigger) ?
            ping_false :
            model_.NewEquivalentVarAnd(
                {ping_false, Not(PoisonedVar(ping, ra.time))},
                absl::StrFormat("%s_ping_%s_healthy_%s", Role_Name(ra.acting),
                                g_.PlayerName(ping),
                                Role_Name(false_trigger))));
      }
    }
  }
  model_.AddOr(cases);
}

void GameSatSolver::AddShownTokenConstraints() {
  for (int i = 0; i < g_.NumPlayers(); ++i) {
    vector<BoolVar> shown_token;
    for (Role role : FilterRoles(script_, [](Role r) { return r != DRUNK; })) {
      shown_token.push_back(ShownTokenVar(i, role));
    }
    // Every player was shown exactly one non-DRUNK token:
    model_.AddEqualitySum(shown_token, 1);
    // Nobody can be shown the Drunk token:
    model_.AddEquality(ShownTokenVar(i, DRUNK), false);
    // Register the roles that were shown night 1.
    Role shown = g_.ShownToken(i, Time::Night(1));
    if (shown != ROLE_UNSPECIFIED) {
      model_.AddEquality(ShownTokenVar(i, shown), true);
    }
  }
  // All shown tokens are unique:
  for (Role role : AllRoles(script_)) {
    vector<BoolVar> shown_role;
    for (int i = 0; i < g_.NumPlayers(); ++i) {
      shown_role.push_back(ShownTokenVar(i, role));
    }
    model_.AddAtMostOne(shown_role);
  }
  for (int i = 0; i < g_.NumPlayers(); ++i) {
    // Being shown a townsfolk role means you either are that role, or DRUNK.
    for (Role role : TownsfolkRoles(script_)) {
      model_.AddImplicationOr(
          ShownTokenVar(i, role),
          {RoleVar(i, role, Time::Night(1)),
           RoleVar(i, DRUNK, Time::Night(1))});
      // Importantly, if you're the Drunk, you cannot be shown an in-play token:
      model_.AddOr({Not(RoleVar(i, DRUNK, Time::Night(1))),
                    Not(ShownTokenVar(i, role)),
                    Not(RoleInPlayVar(role))});
    }
    // Being shown any other role means you are that role.
    const auto non_townsfolk_roles = FilterRoles(
        script_, [](Role r) { return r != DRUNK && !IsTownsfolkRole(r); });
    for (Role role : non_townsfolk_roles) {
      model_.AddEquality(
          ShownTokenVar(i, role), RoleVar(i, role, Time::Night(1)));
    }
  }
}

void GameSatSolver::AddScarletWomanProcConstraints(const Time& time) {
  // The Scarlet Woman becoming Imp triggers if and only if, on time:
  // * SW is alive (we assume that the SW is never poisoned)
  // * The Demon died during the day (otherwise it's a starpass)
  // * There are >=4 alive players remaining (not counting the Demon)
  vector<int> deaths = g_.Deaths(time);  // Chronological day deaths.
  // How many out of day deaths could cause an SW proc:
  const int num_candidates = g_.NumAlive(time) - 4;
  vector<int> demon_candidates;  // Dead demon possibilities.
  for (int i = 0; i < num_candidates && i < deaths.size(); ++i) {
    const int c = deaths[i];
    if (g_.IsRolePossible(c, IMP, time)) {
      demon_candidates.push_back(c);
    }
  }
  if (demon_candidates.empty()) {
    // Scarlet Woman cannot trigger, so no role changes.
    PropagateAliveRoles(time, time + 1, AllRoles(script_));
    return;
  }
  unordered_set<int> sw_candidates;  // Alive SW possibilities.
  for (int i = 0; i < g_.NumPlayers(); ++i) {
    if (g_.IsAlive(i, time + 1) && g_.IsRolePossible(i, SCARLET_WOMAN, time)) {
      sw_candidates.insert(i);
    }
  }
  if (sw_candidates.empty()) {
    // Scarlet Woman cannot trigger, so no role changes.
    PropagateAliveRoles(time, time + 1, AllRoles(script_));
    return;
  }

  // Scarlet Woman can trgger and turn into the Imp.
  PropagateAliveRoles(time, time + 1, GoodRoles(script_));
  // All roles except Scarlet Woman & Imp propagate from previous night:
  PropagateAliveRoles(time, time + 1, {POISONER, SPY, BARON});
  vector<BoolVar> dead_demon_cases;
  for (int i : demon_candidates) {
    dead_demon_cases.push_back(RoleVar(i, IMP, time));
  }
  BoolVar imp_died = model_.NewEquivalentVarOr(
      dead_demon_cases, absl::StrFormat("demon_died_%s", time));
  for (int i = 0; i < g_.NumPlayers(); ++i) {
    if (!g_.IsAlive(i, time)) {
      continue;
    }
    if (sw_candidates.find(i) == sw_candidates.end()) {
      PropagateRolesForPlayer(i, time, time + 1, {SCARLET_WOMAN, IMP});
      continue;
    }
    BoolVar day_imp_i = RoleVar(i, IMP, time);
    BoolVar day_sw_i = RoleVar(i, SCARLET_WOMAN, time);
    BoolVar night_imp_i = RoleVar(i, IMP, time + 1);
    BoolVar night_sw_i = RoleVar(i, SCARLET_WOMAN, time + 1);
    // No proc -> roles are propagated:
    model_.AddImplicationEq(Not(imp_died), day_imp_i, night_imp_i);
    model_.AddImplicationEq(Not(imp_died), day_sw_i, night_sw_i);
    // Otherwise, if SW procs:
    model_.AddImplication(imp_died, Not(night_sw_i));  // Turns into the Imp
    model_.AddImplicationEq(imp_died, day_sw_i, night_imp_i);
  }
}

void GameSatSolver::AddImpStarpassConstraints(const Time& time) {
  int dead_imp_candidate = kNoPlayer;
  for (int i : g_.Deaths(time)) {
    if (g_.IsRolePossible(i, IMP, time)) {
      dead_imp_candidate = i;  // At most one night death in TB.
    }
  }
  if (dead_imp_candidate == kNoPlayer) {
    // No possible starpass, all roles propagate.
    PropagateAliveRoles(time, time + 1, AllRoles(script_));
    return;
  }
  unordered_set<int> starpass_catch_candidates;
  for (int i = 0; i < g_.NumPlayers(); ++i) {
    if (g_.ShownToken(i, time) == IMP && g_.ShownToken(i, time - 1) != IMP) {
      // We know there was a starpass, and we found who caught it.
      for (int j = 0; j < g_.NumPlayers(); ++j) {
        if (j != i && g_.IsAlive(j, time)) {
          PropagateRolesForPlayer(j, time, time + 1, AllRoles(script_));
        }
      }
      model_.AddEquality(RoleVar(dead_imp_candidate, IMP, time), true);
      model_.AddEquality(RoleVar(i, IMP, time + 1), true);
      return;
    }
    if (g_.IsAlive(i, time + 1) && g_.IsRolePossible(i, IMP, time + 1)) {
      starpass_catch_candidates.insert(i);
    }
  }
  if (starpass_catch_candidates.empty()) {
    // No possible starpass, all roles propagate.
    PropagateAliveRoles(time, time + 1, AllRoles(script_));
    return;
  }
  // All Good roles except Recluse always propagate from night 1:
  PropagateAliveRoles(Time::Night(1), time + 1, TownsfolkRoles(script_));
  PropagateAliveRoles(Time::Night(1), time + 1, {BUTLER, DRUNK, SAINT});
  // The Minions and Recluse can catch a starpass.
  BoolVar starpass = RoleVar(dead_imp_candidate, IMP, time);

  vector<BoolVar> catch_cases;
  vector<BoolVar> eligible;
  for (int i : starpass_catch_candidates) {
    const BoolVar& night_imp_i = RoleVar(i, IMP, time);
    const BoolVar& day_imp_i = RoleVar(i, IMP, time + 1);
    model_.AddImplication(night_imp_i, day_imp_i);
    BoolVar catch_i = model_.NewEquivalentVarAnd(
        {Not(night_imp_i), day_imp_i},
        absl::StrFormat("%s_catches_starpass_%s", g_.PlayerName(i), time));
    catch_cases.push_back(catch_i);
    if (g_.ShownToken(i, time) == IMP && g_.ShownToken(i, time - 1) != IMP) {
      model_.AddEquality(catch_i, true);
    }
    // The Scarlet Woman must catch the starpass, if there are 5 living.
    if (g_.NumAlive(time) >= 5) {
      model_.AddImplicationOr(
          starpass, {Not(RoleVar(i, SCARLET_WOMAN, time)), catch_i});
    }
    const Role role_claim = role_claims_[i][time.count - 1];
    BoolVar healthy_recluse_i = role_claim == IMP ?
        model_.NewEquivalentVarAnd(
            {RoleVar(i, RECLUSE, time), Not(PoisonerPickVar(i, time))},
            absl::StrFormat("healthy_recluse_%s_%s", g_.PlayerName(i), time)) :
        model_.FalseVar();
    for (Role role : {POISONER, SPY, SCARLET_WOMAN, BARON, RECLUSE}) {
      const BoolVar& night_role_i = RoleVar(i, role, time);
      const BoolVar& day_role_i = RoleVar(i, role, time + 1);
      const BoolVar& e = role == RECLUSE ? healthy_recluse_i : night_role_i;
      eligible.push_back(e);
      model_.AddImplicationEq(Not(starpass), night_role_i, day_role_i);
      model_.AddOr({Not(starpass), Not(e), day_role_i, day_imp_i});
    }
  }
  for (int i = 0; i < g_.NumPlayers(); ++i) {
    if (starpass_catch_candidates.find(i) == starpass_catch_candidates.end() &&
        g_.IsAlive(i, time)) {
      PropagateRolesForPlayer(i, time, time + 1,
          {IMP, POISONER, SPY, SCARLET_WOMAN, BARON, RECLUSE});
    }
  }
  // starpass -> exactly one catches OR nobody was eligible:
  catch_cases.push_back(model_.NewEquivalentVarAnd(
      Not(eligible),
      absl::StrFormat("nobody_eligible_for_starpass_catch_%s", time)));
  model_.AddImplicationSum(starpass, catch_cases, 1);
}

void GameSatSolver::AddImpActionConstraints(const internal::RoleAction& ra) {
  const Time& time = ra.time;
  model_.AddEquality(RoleVar(ra.player, IMP, time), true);
  // "Imp was not poisoned" constraint is handled by the Poisoner role action.
  const auto deaths = g_.Deaths(time);
  const int imp_kill = deaths.empty() ? kNoPlayer : deaths[0];
  const int target = ra.players[0];
  if (!g_.IsAlive(target, ra.time)) {  // Sink kill on a dead player.
    if (imp_kill != kNoPlayer) {
      model_.AddContradiction(absl::StrFormat(
          "Imp %s chose to kill a dead player %s on %s, but %s died.",
          g_.PlayerName(ra.player), g_.PlayerName(target), time,
          g_.PlayerName(imp_kill)));
    }
    return;
  }
  if (imp_kill == target) {  // Kill works.
    // Target was not a healthy Soldier
    if (IsRolePossible(imp_kill, SOLDIER, time)) {
      model_.AddOr({Not(RoleVar(imp_kill, SOLDIER, time)),
                    PoisonerPickVar(imp_kill, time)});
    }
    // Target was not healthy Monk protected
    for (int monk : PossibleMonkProtecting(imp_kill, time)) {
      model_.AddOr({Not(RoleVar(monk, MONK, time)),
                    PoisonerPickVar(monk, time)});
    }
    return;
  }
  if (imp_kill == kNoPlayer) {  // Kill fails.
    vector<BoolVar> cases;
    // Imp was poisoned
    cases.push_back(PoisonerPickVar(ra.player, time));
    // Target was a healthy Soldier
    if (IsRolePossible(target, SOLDIER, time)) {
      cases.push_back(model_.NewEquivalentVarAnd(
          {RoleVar(target, SOLDIER, time),
           Not(PoisonerPickVar(target, time))},
          absl::StrFormat("healthy_SOLDIER_%s_%s", g_.PlayerName(target),
                          time)));
    }
    // Target was healthy Monk protected
    for (int monk : PossibleMonkProtecting(target, time)) {
      cases.push_back(model_.NewEquivalentVarAnd(
          {RoleVar(monk, MONK, time),
            Not(PoisonerPickVar(monk, time))},
          absl::StrFormat("healthy_MONK_%s_%s", g_.PlayerName(monk), time)));
    }
    // Target was a healthy Mayor, not healthy Monk protected, and bounced to no
    // kill.
    if (IsRolePossible(target, MAYOR, time)) {
      vector<BoolVar> mayor_bounce_no_kill(
          {RoleVar(target, MAYOR, time), Not(PoisonerPickVar(target, time))});
      for (int monk : PossibleMonkProtecting(target, time)) {
        mayor_bounce_no_kill.push_back(model_.NewEquivalentVarOr(
            {Not(RoleVar(monk, MONK, time)), PoisonerPickVar(monk, time)},
            absl::StrFormat("not_healthy_MONK_%s_%s", g_.PlayerName(monk),
                            time)));
      }
      if (g_.NumAlive(time) == g_.NumPlayers()) {
        // No possible bounce to a dead player for no kill.
        vector<BoolVar> no_kill_cases;
        for (int i = 0; i < g_.NumPlayers(); ++i) {
          // Healthy Soldier.
          if (IsRolePossible(i, SOLDIER, time)) {
            no_kill_cases.push_back(model_.NewEquivalentVarAnd(
                {RoleVar(i, SOLDIER, time), Not(PoisonerPickVar(i, time))},
                absl::StrFormat("healthy_SOLDIER_%s_%s", g_.PlayerName(i),
                                time)));
          }
          for (int monk : PossibleMonkProtecting(i, time)) {
            no_kill_cases.push_back(model_.NewEquivalentVarAnd(
                {RoleVar(monk, MONK, time), Not(PoisonerPickVar(monk, time))},
                absl::StrFormat("healthy_MONK_%s_%s", g_.PlayerName(monk),
                                time)));
          }
        }
        mayor_bounce_no_kill.push_back(model_.NewEquivalentVarOr(
            no_kill_cases, absl::StrFormat("mayor_%s_bounce_no_kill_cases_%s",
                                           g_.PlayerName(target), time)));
      }
      cases.push_back(model_.NewEquivalentVarAnd(
          mayor_bounce_no_kill, absl::StrFormat("mayor_%s_bounce_no_kill_%s",
                                                g_.PlayerName(target), time)));
    }
    model_.AddOr(cases);
    return;
  }
  // Kill bounces. Target is a healthy Mayor, not healthy Monk protected.
  if (IsRolePossible(target, MAYOR, time)) {
    model_.AddEquality(RoleVar(target, MAYOR, time), true);
    model_.AddEquality(PoisonerPickVar(target, time), false);
    for (int monk : PossibleMonkProtecting(target, time)) {
      model_.AddOr({Not(RoleVar(monk, MONK, time)),
                    PoisonerPickVar(monk, time)});
    }
    return;
  }
  model_.AddContradiction(absl::StrFormat(
      "No possible reason for Imp kill of %s to bounce to %s",
      g_.PlayerName(target), g_.PlayerName(imp_kill)));
}

void GameSatSolver::AddImpActionClaimConstraints(
    const internal::RoleAction& ra) {
  // TODO(olaola): implement this!
}

void GameSatSolver::AddImpConstraints(
    const Time& time,
    const internal::RoleAction* imp_action,
    vector<const internal::RoleAction*> imp_action_claims) {
  // An Imp action overrides any claims.
  if (imp_action != nullptr) {
    AddImpActionConstraints(*imp_action);
    return;
  }
  if (!imp_action_claims.empty()) {
    for (const auto* claim : imp_action_claims) {
      AddImpActionClaimConstraints(*claim);
    }
  }
  vector<int> deaths = g_.Deaths(time);
  if (!deaths.empty()) {
    const int imp_kill = deaths[0];
    // Target was not a healthy Soldier
    if (IsRolePossible(imp_kill, SOLDIER, time)) {
      model_.AddOr({Not(RoleVar(imp_kill, SOLDIER, time)),
                    PoisonerPickVar(imp_kill, time)});
    }
    // Target was not healthy Monk protected
    for (int monk : PossibleMonkProtecting(imp_kill, time)) {
      model_.AddOr({Not(RoleVar(monk, MONK, time)),
                    PoisonerPickVar(monk, time)});
    }
    return;
  }
  // We can only deduce info on no death if no death ever occurred, because the
  // Imp choice is mandatory (and cannot sink kill on a dead player, because
  // there is no dead player):
  if (g_.NumAlive(time) < g_.NumPlayers()) {
    return;
  }
  // A healthy Monk, Soldier, or Poisoner was alive.
  vector<BoolVar> cases;
  cases.push_back(AliveRoleVar(POISONER, time));
  for (Role role : {SOLDIER, MONK}) {
    for (int i : AliveRolePossibilities(role, time)) {
      cases.push_back(model_.NewEquivalentVarAnd(
          {RoleVar(i, role, time),
            Not(PoisonerPickVar(i, time))},
          absl::StrFormat("healthy_%s_%s_%s", Role_Name(role),
                          g_.PlayerName(i), time)));
    }
  }
  model_.AddOr(cases);
}

vector<int> GameSatSolver::AliveRolePossibilities(Role role,
                                                  const Time& time) const {
  vector<int> result;
  for (int i = 0; i < g_.NumPlayers(); ++i) {
    if (g_.IsAlive(i, time) && IsRolePossible(i, role, time)) {
      result.push_back(i);
    }
  }
  return result;
}

// Returns all the players who claimed to have protected target at time.
vector<int> GameSatSolver::PossibleMonkProtecting(
    int target, const Time& time) const {
  vector<int> result;
  const auto it = role_action_claims_.find(MONK);
  if (it == role_action_claims_.end()) {
    return result;
  }
  for (const auto* ra : it->second[time.count - 1]) {
    if (ra->players[0] == target && IsRolePossible(ra->player, MONK, time)) {
      result.push_back(ra->player);
    }
  }
  return result;
}

vector<BoolVar> GameSatSolver::CollectRolesForPlayer(
    const Time& time, int player,
    absl::Span<const Role> roles, bool only_alive) {
  vector<BoolVar> result;
  if (!only_alive || g_.IsAlive(player, time)) {
    for (Role role : roles) {
      result.push_back(RoleVar(player, role, time));
    }
  }
  return result;
}

vector<BoolVar> GameSatSolver::CollectRoles(const Time& time,
                                            absl::Span<const Role> roles,
                                            bool only_alive) {
  vector<BoolVar> result;
  for (int i = 0; i < g_.NumPlayers(); ++i) {
    for (const auto& v : CollectRolesForPlayer(time, i, roles, only_alive)) {
      result.push_back(v);
    }
  }
  return result;
}

void GameSatSolver::PropagateAliveRoles(const Time& from,
                                        const Time& to,
                                        absl::Span<const Role> roles) {
  for (int i = 0; i < g_.NumPlayers(); ++i) {
    if (g_.IsAlive(i, from)) {
      PropagateRolesForPlayer(i, from, to, roles);
    }
  }
}

void GameSatSolver::PropagateDeadRoles(const Time& from, const Time& to) {
  for (int i = 0; i < g_.NumPlayers(); ++i) {
    if (!g_.IsAlive(i, from)) {
      PropagateRolesForPlayer(i, from, to, AllRoles(script_));
    }
  }
}

void GameSatSolver::PropagateRolesForPlayer(int player,
                                            const Time& from,
                                            const Time& to,
                                            absl::Span<const Role> roles) {
  for (Role role : roles) {
    model_.AddEquality(RoleVar(player, role, from),
                       RoleVar(player, role, to));
  }
}

SolverRequestBuilder& SolverRequestBuilder::AddStartingRoles(
    const unordered_map<string, Role>& player_roles) {
  for (const auto& it : player_roles) {
    auto *pr = request_.mutable_assumptions()->add_starting_roles();
    pr->set_player(it.first);
    pr->set_role(it.second);
  }
  return *this;
}

SolverRequestBuilder& SolverRequestBuilder::AddStartingRolesNot(
    const unordered_map<string, Role>& player_roles) {
  for (const auto& it : player_roles) {
    auto *pr = request_.mutable_assumptions()->add_starting_roles();
    pr->set_player(it.first);
    pr->set_role(it.second);
    pr->set_is_not(true);
  }
  return *this;
}

SolverRequestBuilder& SolverRequestBuilder::AddCurrentRoles(
    const unordered_map<string, Role>& player_roles) {
  for (const auto& it : player_roles) {
    auto *pr = request_.mutable_assumptions()->add_current_roles();
    pr->set_player(it.first);
    pr->set_role(it.second);
  }
  return *this;
}

SolverRequestBuilder& SolverRequestBuilder::AddCurrentRolesNot(
    absl::Span<const pair<string, Role>> player_roles) {
  for (const auto& p : player_roles) {
    auto *pr = request_.mutable_assumptions()->add_current_roles();
    pr->set_player(p.first);
    pr->set_role(p.second);
    pr->set_is_not(true);
  }
  return *this;
}

SolverRequestBuilder& SolverRequestBuilder::AddRolesInPlay(
    absl::Span<const Role> roles) {
  for (Role role : roles) {
    request_.mutable_assumptions()->add_roles_in_play(role);
  }
  return *this;
}

SolverRequestBuilder& SolverRequestBuilder::AddRolesNotInPlay(
    absl::Span<const Role> roles) {
  for (Role role : roles) {
    request_.mutable_assumptions()->add_roles_not_in_play(role);
  }
  return *this;
}

SolverRequestBuilder& SolverRequestBuilder::AddGood(
    absl::Span<const string> players) {
  for (const string& player : players) {
    request_.mutable_assumptions()->add_is_good(player);
  }
  return *this;
}

SolverRequestBuilder& SolverRequestBuilder::AddEvil(
    absl::Span<const string> players) {
  for (const string& player : players) {
    request_.mutable_assumptions()->add_is_evil(player);
  }
  return *this;
}

vector<BoolVar> GameSatSolver::CollectAssumptionLiterals(
    const SolverRequest::Assumptions& assumptions) {
  vector<BoolVar> assumption_literals;
  for (const auto& pr : assumptions.current_roles()) {
    const auto& v = RoleVar(
        g_.PlayerIndex(pr.player()), pr.role(), g_.CurrentTime());
    assumption_literals.push_back(pr.is_not() ? Not(v) : v);
  }
  for (const auto& pr : assumptions.starting_roles()) {
    const auto& v = RoleVar(
        g_.PlayerIndex(pr.player()), pr.role(), Time::Night(1));
    assumption_literals.push_back(pr.is_not() ? Not(v) : v);
  }
  for (int role : assumptions.roles_in_play()) {
    assumption_literals.push_back(RoleInPlayVar(Role(role)));
  }
  for (int role : assumptions.roles_not_in_play()) {
    assumption_literals.push_back(Not(RoleInPlayVar(Role(role))));
  }
  for (const string& player : assumptions.is_evil()) {
    assumption_literals.push_back(StartingEvilVar(g_.PlayerIndex(player)));
  }
  for (const string& player : assumptions.is_good()) {
    assumption_literals.push_back(Not(StartingEvilVar(g_.PlayerIndex(player))));
  }
  for (const auto& p : assumptions.poisoned_players()) {
    const int i = g_.PlayerIndex(p.player());
    const Time time = Time::Night(p.night());
    if (model_.FindVar(PoisonerPickVarName(i, time)) == nullptr && p.is_not()) {
      continue;  // This assumption does not change anything.
    }
    BoolVar v = PoisonedVar(i, time);
    assumption_literals.push_back(p.is_not() ? Not(v) : v);
  }
  return assumption_literals;
}

int GameSatSolver::SolutionAliveDemon(const CpSolverResponse& response) {
  for (int i = 0; i < g_.NumPlayers(); ++i) {
    if (!g_.IsAlive(i)) {
      continue;
    }
    for (Role role : DemonRoles(script_)) {
      if (SolutionBooleanValue(response, RoleVar(i, role, g_.CurrentTime()))) {
        return i;
      }
    }
  }
  return kNoPlayer;
}

void GameSatSolver::FillWorldFromSolverResponse(
    const CpSolverResponse& response, SolverResponse::World* world) {
  auto* current_roles = world->mutable_current_roles();
  auto* starting_roles = world->mutable_starting_roles();
  const Time night1 = Time::Night(1), cur_time = g_.CurrentTime();
  for (int i = 0; i < g_.NumPlayers(); ++i) {
    for (Role role : AllRoles(script_)) {
      if (SolutionBooleanValue(response, RoleVar(i, role, cur_time))) {
        const string player = g_.PlayerName(i);
        const auto it = current_roles->find(player);
        CHECK(it == current_roles->end())
            << absl::StrFormat("Double current role assignment for player %s: "
                               "found both %s and %s", player,
                              Role_Name(it->second), Role_Name(role));
        (*current_roles)[player] = role;
      } else if (SolutionBooleanValue(response, RoleVar(i, role, night1))) {
        const string player = g_.PlayerName(i);
        CHECK(starting_roles->find(player) == starting_roles->end())
            << "Double starting role assignment for player " << player;
        (*starting_roles)[player] = role;
      }
    }
  }
  CHECK_EQ(current_roles->size(), g_.NumPlayers())
      << "Not all players assigned current roles.";
}

SolverResponse GameSatSolver::Solve(const SolverRequest& request) {
  SolverResponse result;
  // Making a copy to add assumptions.
  const auto assumptions = CollectAssumptionLiterals(request.assumptions());
  CpModelBuilder cp_model(model_.Model());
  cp_model.AddBoolAnd(assumptions);
  path tmp_dir = "./tmp";
  path solution_dir = tmp_dir / "solutions";
  const bool debug_mode = request.debug_mode();
  if (debug_mode) {
    // Create the ./tmp/solutions directory, if not present.
    create_directories(solution_dir);
    WriteProtoToFile(cp_model.Build(), tmp_dir / "model.pbtxt");
  }
  CpSolverResponse response;
  map<string, int> num_worlds_per_demon;
  int solutions = 0;
  operations_research::sat::Model model;
  SatParameters parameters;
  parameters.set_enumerate_all_solutions(!request.stop_after_first_solution());
  parameters.set_symmetry_level(0);  // Empirically, this is faster.
  // We only care about current (and starting?) role assignments, so add them
  // as key variables:
  for (int i = 0; i < g_.NumPlayers(); ++i) {
    for (Role role : AllRoles(script_)) {
      parameters.add_key_variables(RoleVar(i, role, g_.CurrentTime()).index());
    }
  }
  model.Add(NewSatParameters(parameters));
  auto log_progress = [&](bool force) {
    string progress = absl::StrFormat("Found %d solutions ", solutions);
    for (const auto& it : num_worlds_per_demon) {
      absl::StrAppend(
          &progress, absl::StrFormat("%s: %d ", it.first, it.second));
    }
    if (force || solutions % 50 == 0) {
      cout << progress << endl;
    }
  };
  model.Add(NewFeasibleSolutionObserver([&](const CpSolverResponse& r) {
    ++solutions;
    if (debug_mode) {
      const path resp_filename = absl::StrFormat("resp_%d.pbtxt", solutions);
      WriteProtoToFile(r, solution_dir / resp_filename);
    }
    SolverResponse::World cur_world;
    FillWorldFromSolverResponse(r, &cur_world);
    const int demon = SolutionAliveDemon(r);
    *(result.add_worlds()) = cur_world;
    const string demon_name =
        demon == kNoPlayer ? "<Dead player>" : g_.PlayerName(demon);
    num_worlds_per_demon[demon_name]++;
    log_progress(false);
    if (debug_mode) {
      const path sat_filename = absl::StrFormat("sat_solution_%d", solutions);
      model_.WriteSatSolutionToFile(r, solution_dir / sat_filename);
      const path world_filename = absl::StrFormat("world_%d.pbtxt", solutions);
      WriteProtoToFile(cur_world, solution_dir / world_filename);
    }
  }));
  SolveCpModel(cp_model.Build(), &model);
  log_progress(true);
  for (const auto& it : num_worlds_per_demon) {
    auto* ado = result.add_alive_demon_options();
    ado->set_name(it.first);
    ado->set_count(it.second);
  }
  return result;
}

// Solves the game and returns all valid worlds.
SolverResponse Solve(const GameState& g) {
  return GameSatSolver(g).Solve();
}
SolverResponse Solve(const GameState& g, const SolverRequest& request) {
  return GameSatSolver(g).Solve(request);
}
// Returns whether a valid world exists.
bool IsValidWorld(const GameState& g) {
  return GameSatSolver(g).IsValidWorld();
}
// Returns whether a valid world exists given all assumptions in the request.
bool IsValidWorld(const GameState& g, const SolverRequest& request) {
  return GameSatSolver(g).IsValidWorld(request);
}

}  // namespace botc
