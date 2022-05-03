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

#include "src/game.h"

#include <algorithm>
#include <fstream>
#include <iostream>

#include "ortools/sat/cp_model.pb.h"
#include "ortools/sat/cp_model_solver.h"
#include "ortools/sat/model.h"
#include "ortools/sat/sat_parameters.pb.h"
#include "src/util.h"

namespace botc {

namespace {
using operations_research::sat::CpSolverStatus;
using operations_research::sat::LinearExpr;
using operations_research::sat::Model;
using operations_research::sat::NewFeasibleSolutionObserver;
using operations_research::sat::SatParameters;
using std::ofstream;

const int kNumTownsfolk[] = {3, 3, 5, 5, 5, 7, 7, 7, 9, 9, 9};
const int kNumOutsiders[] = {0, 1, 0, 1, 2, 0, 1, 2, 0, 1, 2};
const int kNumMinions[] = {1, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3};
const Role kAllRoles[] = {
    WASHERWOMAN, LIBRARIAN, INVESTIGATOR, CHEF, EMPATH, FORTUNE_TELLER,
    UNDERTAKER, MONK, RAVENKEEPER, VIRGIN, SLAYER, SOLDIER, MAYOR,
    BUTLER, DRUNK, RECLUSE, SAINT, POISONER, SPY, SCARLET_WOMAN, BARON,
    IMP
};
const Role kGoodRoles[] = {
    WASHERWOMAN, LIBRARIAN, INVESTIGATOR, CHEF, EMPATH, FORTUNE_TELLER,
    UNDERTAKER, MONK, RAVENKEEPER, VIRGIN, SLAYER, SOLDIER, MAYOR,
    BUTLER, DRUNK, RECLUSE, SAINT
};
const Role kTownsfolkRoles[] = {
    WASHERWOMAN, LIBRARIAN, INVESTIGATOR, CHEF, EMPATH, FORTUNE_TELLER,
    UNDERTAKER, MONK, RAVENKEEPER, VIRGIN, SLAYER, SOLDIER, MAYOR
};
const Role kOutsiderRoles[] = {BUTLER, DRUNK, RECLUSE, SAINT};
const Role kMinionRoles[] = {POISONER, SPY, SCARLET_WOMAN, BARON};
const Role kEvilRoles[] = {POISONER, SPY, SCARLET_WOMAN, BARON, IMP};
const Role kNonTownsfolkRoles[] = {
    BUTLER, DRUNK, RECLUSE, SAINT, POISONER, SPY, SCARLET_WOMAN, BARON, IMP};
const Role kFirstNightRoles[] = {WASHERWOMAN, LIBRARIAN, INVESTIGATOR, CHEF};

Setup SetupProto(absl::Span<const string> players) {
  Setup setup;
  setup.mutable_players()->Assign(players.begin(), players.end());
  return setup;
}

Setup StorytellerSetupProto(absl::Span<const string> players,
                            const unordered_map<string, Role>& roles,
                            const string& red_herring) {
  Setup setup = SetupProto(players);
  *setup.mutable_player_roles() = google::protobuf::Map<string, Role>(
      roles.begin(), roles.end());
  setup.set_red_herring(red_herring);
  return setup;
}

bool IsRoleInRoles(Role role, absl::Span<const Role> roles) {
  for (Role r : roles) {
    if (r == role) {
      return true;
    }
  }
  return false;
}

bool IsMinionRole(Role role) {
  return IsRoleInRoles(role, kMinionRoles);
}

bool IsTownsfolkRole(Role role) {
  return IsRoleInRoles(role, kTownsfolkRoles);
}

bool IsOutsiderRole(Role role) {
  return IsRoleInRoles(role, kOutsiderRoles);
}

bool IsGoodRole(Role role) {
  return IsRoleInRoles(role, kGoodRoles);
}

bool IsFirstNightRole(Role role) {
  return IsRoleInRoles(role, kFirstNightRoles);
}

vector<BoolVar> Not(absl::Span<const BoolVar> literals) {
  vector<BoolVar> result;
  for (const auto& v : literals) {
    result.push_back(Not(v));
  }
  return result;
}

string ConstraintName(const string& separator,
                      absl::Span<const BoolVar> literals) {
  if (literals.size() == 0) {
    return "0";
  }
  string name = literals[0].Name();
  for (int i = 1; i < literals.size(); ++i) {
    absl::StrAppend(&name, absl::StrFormat(" %s %s", separator,
                                           literals[i].Name()));
  }
  return name;
}

string ConstraintName(const string& separator, const BoolVar& var,
                      absl::Span<const BoolVar> literals) {
  const string name = ConstraintName(separator, literals);
  return absl::StrFormat("%s -> %s", var.Name(), name);
}

string AndConstraintName(const BoolVar& var,
                         absl::Span<const BoolVar> literals) {
  return ConstraintName("^", var, literals);
}

string OrConstraintName(const BoolVar& var,
                        absl::Span<const BoolVar> literals) {
  return ConstraintName("V", var, literals);
}

string AndConstraintName(absl::Span<const BoolVar> literals) {
  return ConstraintName("^", literals);
}

string OrConstraintName(absl::Span<const BoolVar> literals) {
  return ConstraintName("V", literals);
}
}  // namespace

namespace internal {
string Time::ToString() const {
  return absl::StrFormat("%s_%d", IsDay ? "day" : "night", Count);
}
}  // namespace internal

// TODO(olaola):
// * Solver simplifying assumption: full round-robin role claims need to finish
//   before info claims start. Validate this!
// * Implement dead votes.
// * Decide what to do about the Butler -- maybe support the "full" voting?
// * Replace CHECKs with absl::Status everywhere for testing.
// * Validate player interactions (e.g. exactly 1 FT action per night)
// * Validate night order.
// * Unit test Imp starpass!!
// * Solver optimization ideas:
//   * Check whether I have duplicate variables or constraints (and maybe cache
//     them).
//   * Decision strategy: try to assign Imp first, then claimed roles (or
//     Minions?).

GameState GameState::FromStorytellerPerspective(
    absl::Span<const string> players, const unordered_map<string, Role>& roles,
    const string& red_herring) {
  return GameState(STORYTELLER,
                   StorytellerSetupProto(players, roles, red_herring));
}

GameState GameState::FromStorytellerPerspective(
    absl::Span<const string> players,
    const unordered_map<string, Role>& roles) {
  return FromStorytellerPerspective(players, roles, "");
}

GameState GameState::FromObserverPerspective(absl::Span<const string> players) {
  return GameState(OBSERVER, SetupProto(players));
}

GameState GameState::FromPlayerPerspective(absl::Span<const string> players) {
  return GameState(PLAYER, SetupProto(players));
}

GameState GameState::FromProto(const GameLog& log) {
  GameState g = GameState(log.perspective(), log.setup());
  for (const auto& event : log.events()) {
    g.AddEvent(event);
  }
  return g;
}

GameState GameState::ReadFromFile(const string& filename) {
  GameLog log;
  ReadProtoFromFile(filename, &log);
  return FromProto(log);
}

GameLog GameState::ToProto() const {
  return log_;
}

void GameState::WriteToFile(const string& filename) const {
  WriteProtoToFile(filename, ToProto());
}

int GameState::PlayerIndex(const string& name) const {
  const auto& it = player_index_.find(name);
  CHECK(it != player_index_.end()) << "Invalid player name: " << name;
  return it->second;
}

vector<string> GameState::ClaimingRole(Role role) const {
  vector<string> result;
  for (int i : players_claiming_[role]) {
    result.push_back(players_[i]);
  }
  return result;
}

vector<int> GameState::AlivePlayersClaiming(Role role) const {
  vector<int> result;
  for (int i : players_claiming_[role]) {
    if (is_alive_[i]) {
      result.push_back(i);
    }
  }
  return result;
}

GameState::GameState(Perspective perspective, const Setup& setup)
    : perspective_(perspective), num_players_(setup.players_size()),
      is_alive_(num_players_, true), num_alive_(num_players_),
      player_used_slayer_shot_(num_players_),
      player_has_been_nominated_(num_players_),
      num_votes_(0), on_the_block_(kNoPlayer), execution_(kNoPlayer),
      execution_death_(kNoPlayer), prev_execution_(kNoPlayer),
      slayer_death_(kNoPlayer), night_death_(kNoPlayer),
      next_event_maybe_victory_(false), next_event_maybe_death_(false),
      next_event_maybe_execution_(false), victory_(TEAM_UNSPECIFIED),
      claim_of_player_(num_players_), players_claiming_(Role_ARRAYSIZE),
      perspective_player_(kNoPlayer),
      perspective_player_shown_token_(ROLE_UNSPECIFIED),
      night_action_used_(num_players_), deferred_constraints_(num_players_),
      st_player_roles_(num_players_), st_shown_tokens_(num_players_),
      st_red_herring_(kNoPlayer), st_poisoner_pick_(kNoPlayer),
      st_imp_pick_(kNoPlayer), st_monk_pick_(kNoPlayer),
      st_butler_pick_(kNoPlayer) {
  CHECK_NE(perspective_, PERSPECTIVE_UNSPECIFIED)
      << "Need to specify perspective";
  CHECK_GE(num_players_, 5);
  CHECK_LE(num_players_, 15);
  log_.set_perspective(perspective_);
  *(log_.mutable_setup()) = setup;
  num_outsiders_ = kNumOutsiders[num_players_ - 5];
  num_minions_ = kNumMinions[num_players_ - 5];
  int player_index = 0;
  for (const auto& name : setup.players()) {
    players_.push_back(name);
    player_index_[name] = player_index++;
  }

  if (perspective_ == STORYTELLER) {
    CHECK_EQ(setup.player_roles_size(), num_players_) <<
        "Expected fully assigned player roles in storyteller perspective";
  } else {
    // Check we don't have unexpected info.
    CHECK_EQ(setup.player_roles_size(), 0)
        << "Player roles assigned in non-storyteller perspective.";
    CHECK(setup.red_herring().empty())
        << "Red-herring info in non-storyteller perspective.";
  }
  for (const auto& pr : setup.player_roles()) {
    const string& name = pr.first;
    CHECK_NE(pr.second, ROLE_UNSPECIFIED)
        << "Got unassigned role for player " << name;
    st_player_roles_[PlayerIndex(name)] = pr.second;
  }

  InitNightRoleVars();
  InitRedHerring(setup.red_herring());
}

void GameState::AddAnd(absl::Span<const BoolVar> literals) {
  model_.AddBoolAnd(literals).WithName(AndConstraintName(literals));
}

void GameState::AddOr(absl::Span<const BoolVar> literals) {
  model_.AddBoolOr(literals).WithName(OrConstraintName(literals));
}

void GameState::AddImplication(const BoolVar& v1, const BoolVar& v2) {
  AddImplicationOr(v1, {v2});
}

void GameState::AddImplicationAnd(const BoolVar& var,
                                  absl::Span<const BoolVar> literals) {
  if (literals.size() == 0) {
    model_.FixVariable(var, false);
    return;
  }
  const string name = AndConstraintName(var, literals);
  model_.AddBoolAnd(literals).OnlyEnforceIf(var).WithName(name);
}

void GameState::AddImplicationOr(const BoolVar& var,
                                 absl::Span<const BoolVar> literals) {
  if (literals.size() == 0) {
    model_.FixVariable(var, false);
    return;
  }
  const string name = OrConstraintName(var, literals);
  model_.AddBoolOr(literals).OnlyEnforceIf(var).WithName(name);
}

void GameState::AddImplicationSum(const BoolVar& var,
                                  absl::Span<const BoolVar> literals, int sum) {
  const string name = absl::StrFormat(
      "%s -> %s = %d", var.Name(), ConstraintName("+", literals), sum);
  model_.AddEquality(LinearExpr::Sum(literals), sum)
      .OnlyEnforceIf(var)
      .WithName(name);
}

void GameState::AddEquivalenceAnd(
    const BoolVar& var, absl::Span<const BoolVar> literals) {
  AddImplicationAnd(var, literals);
  AddImplicationOr(Not(var), Not(literals));
}

void GameState::AddEquivalenceOr(
    const BoolVar& var, absl::Span<const BoolVar> literals) {
  AddImplicationOr(var, literals);
  AddImplicationAnd(Not(var), Not(literals));
}

void GameState::AddEquivalenceSum(const BoolVar& var,
                                  absl::Span<const BoolVar> literals) {
  const string name = absl::StrFormat(
      "%s = %s", var.Name(), ConstraintName("+", literals));
  model_.AddEquality(LinearExpr::Sum(literals), var).WithName(name);
}

void GameState::AddEqualitySum(absl::Span<const BoolVar> literals, int sum) {
  const string name = absl::StrFormat(
      "%d = %s", sum, ConstraintName("+", literals));
  model_.AddEquality(LinearExpr::Sum(literals), sum).WithName(name);
}

void GameState::AddContradiction(const string& reason) {
  model_.AddBoolOr({model_.FalseVar()})
        .WithName(absl::StrCat("Contradiction: ", reason));
}

BoolVar GameState::CreateEquivalentVarAnd(
    absl::Span<const BoolVar> literals, const string& name) {
  if (literals.size() == 0) {
    return model_.FalseVar();
  }
  const string key = ConstraintName("^", literals);
  const auto it = var_cache_.find(key);
  if (it != var_cache_.end()) {
    return it->second;
  }
  BoolVar var = model_.NewBoolVar().WithName(name);
  AddEquivalenceAnd(var, literals);
  var_cache_[key] = var;
  return var;
}

BoolVar GameState::CreateEquivalentVarOr(
    absl::Span<const BoolVar> literals, const string& name) {
  if (literals.size() == 0) {
    return model_.FalseVar();
  }
  const string key = ConstraintName("V", literals);
  const auto it = var_cache_.find(key);
  if (it != var_cache_.end()) {
    return it->second;
  }
  BoolVar var = model_.NewBoolVar().WithName(name);
  AddEquivalenceOr(var, literals);
  var_cache_[key] = var;
  return var;
}

BoolVar GameState::CreateEquivalentVarSum(
    absl::Span<const BoolVar> literals, const string& name) {
  if (literals.size() == 0) {
    return model_.FalseVar();
  }
  const string key = ConstraintName("+", literals);
  const auto it = var_cache_.find(key);
  if (it != var_cache_.end()) {
    return it->second;
  }
  BoolVar var = model_.NewBoolVar().WithName(name);
  AddEquivalenceSum(var, literals);
  var_cache_[key] = var;
  return var;
}

void GameState::InitRedHerring(const string& name) {
  bool have_fortune_teller = false;
  for (Role role : st_player_roles_) {
    if (role == FORTUNE_TELLER) {
      have_fortune_teller = true;
      break;
    }
  }
  CHECK(name.empty() == !have_fortune_teller)
      << "Fortune teller red herring should be specified if and only if a "
      << "Fortune Teller is in play.";
  if (have_fortune_teller) {
    st_red_herring_ = PlayerIndex(name);
  }
  for (int i = 0; i < num_players_; ++i) {
    string name = absl::StrFormat("red_herring_%s", players_[i]);
    BoolVar v = model_.NewBoolVar().WithName(name);
    red_herring_.push_back(v);
    if (perspective_ == STORYTELLER) {
      model_.FixVariable(v, i == st_red_herring_);
    }
    // Only a Good player can be a red herring.
    AddImplication(v, Not(is_evil_[i]));
  }
  const BoolVar& ft_in_play = roles_in_play_[FORTUNE_TELLER];
  // If a Fortune Teller is in play, there is exactly one red herring.
  AddImplicationSum(ft_in_play, red_herring_, 1);
  AddImplicationSum(Not(ft_in_play), red_herring_, 0);
}

void GameState::InitVarRolesInPlay() {
  roles_in_play_.push_back(model_.FalseVar());  // dummy variable
  for (Role role : kAllRoles) {
    string name = absl::StrFormat("in_play_%s", Role_Name(role));
    BoolVar var = model_.NewBoolVar().WithName(name);
    vector<BoolVar> player_is_role(num_players_);
    for (int i = 0; i < num_players_; ++i) {
      player_is_role[i] = night_roles_[0][i][role];
    }
    roles_in_play_.push_back(var);
    // If roles are known, var can be fixed:
    if (perspective_ == STORYTELLER) {
      bool have_role = false;
      for (Role r : st_player_roles_) {
        if (r == role) {
          have_role = true;
          break;
        }
      }
      model_.FixVariable(var, have_role);
    }
    AddEquivalenceSum(var, player_is_role);
  }
}

void GameState::InitNightRoleVars() {
  if (cur_time_.Count == 1) {
    return;  // We initialize night 1 role variables during setup.
  }
  vector<vector<BoolVar>> night_roles(num_players_);
  for (int i = 0; i < num_players_; ++i) {
    night_roles[i].push_back(model_.FalseVar().WithName("0"));  // dummy var
    for (Role role : kAllRoles) {
      // Variable/constraint names are for debugging only.
      string name = absl::StrFormat(
        "role_%s_%s_night_%d", players_[i], Role_Name(role),
        cur_time_.Count == 0 ? 1 : cur_time_.Count);
      night_roles[i].push_back(model_.NewBoolVar().WithName(name));
    }
  }
  night_roles_.push_back(night_roles);
  AddRoleUniquenessConstraints(night_roles);
  InitPoisonerVars();
  InitButlerVars();  // TODO(olaola): optimize so these are created on demand.
  if (cur_time_.Count == 0) {
    InitShownTokenVars();
    InitVarRolesInPlay();
    InitIsEvilVars();
    // Appropriate numbers of outsiders, townsfolk and minions:
    vector<BoolVar> minions = CollectRoles(night_roles, kMinionRoles);
    AddEqualitySum(minions, num_minions_);
    AddBaronConstraints();
    if (perspective_ == STORYTELLER) {
      for (int i = 0; i < num_players_; ++i) {
        Role role = st_player_roles_[i];
        const auto& pr = night_roles[i];
        // We don't need to fix all of them, but it will be faster.
        for (Role role1 : kAllRoles) {
            model_.FixVariable(pr[role1], role1 == role);
        }
      }
    }
  } else {
    InitImpVars();
    if (AlivePlayersClaiming(MONK).size() > 0) {
      InitMonkVars();
    }
    // The Good roles propagate from night 1:
    PropagateRoles(night_roles_[0], night_roles, kGoodRoles);
    // All Evil roles except Scarlet Woman & Imp propagate from previous day:
    PropagateRoles(day_roles_.back(), night_roles, {POISONER, SPY, BARON});
    AddScarletWomanConstraints();
  }
}

void GameState::AddRoleUniquenessConstraints(
    const vector<vector<BoolVar>>& player_roles) {
  const string time = cur_time_.Count == 0 ? "night_1" : cur_time_.ToString();
  // Each player assigned exactly one role:
  for (int i = 0; i < num_players_; ++i) {
    string name = absl::StrFormat("Player %s has unique role %s", players_[i],
                                  time);
    AddEqualitySum(player_roles[i], 1);
  }
  // Each role other than IMP assigned to at most one player:
  for (Role role : kAllRoles) {
    if (role == IMP) {
      continue;
    }
    vector<BoolVar> player_is_role;
    for (int i = 0; i < num_players_; ++i) {
      player_is_role.push_back(player_roles[i][role]);
    }
    string name = absl::StrFormat("Role %s has at most one player %s",
                                  Role_Name(role), time);
    model_.AddAtMostOne(player_is_role).WithName(name);
  }
  // There needs to be exactly one alive IMP (before game ends):
  AddEqualitySum(CollectAliveRoles(player_roles, {IMP}), 1);
}

void GameState::AddBaronConstraints() {
  const BoolVar& baron_in_play = roles_in_play_[BARON];
  vector<BoolVar> outsiders = CollectRoles(night_roles_[0], kOutsiderRoles);
  AddImplicationSum(Not(baron_in_play), outsiders, num_outsiders_);
  AddImplicationSum(baron_in_play, outsiders, num_outsiders_ + 2);
  int num_townsfolk = kNumTownsfolk[num_players_ - 5];
  vector<BoolVar> townsfolk = CollectRoles(night_roles_[0], kTownsfolkRoles);;
  AddImplicationSum(Not(baron_in_play), townsfolk, num_townsfolk);
  AddImplicationSum(baron_in_play, townsfolk, num_townsfolk - 2);
}

void GameState::InitShownTokenVars() {
  for (int i = 0; i < num_players_; ++i) {
    vector<BoolVar> shown_token;
    shown_token.push_back(model_.FalseVar());  // dummy variable
    for (Role role : kAllRoles) {
      const string name = absl::StrFormat("shown_token_%s_%s",
                                          players_[i], Role_Name(role));
      shown_token.push_back(model_.NewBoolVar().WithName(name));
    }
    shown_token_.push_back(shown_token);
    // Every player was shown exactly one token:
    AddEqualitySum(shown_token, 1);
    // No one is shown the DRUNK token:
    model_.FixVariable(shown_token[DRUNK], false);
  }
  // All shown tokens are unique:
  for (Role role : kAllRoles) {
    vector<BoolVar> shown_role;
    for (int i = 0; i < num_players_; ++i) {
      shown_role.push_back(shown_token_[i][role]);
    }
    const string name = absl::StrFormat("Shown token %s to at most one player",
                                        Role_Name(role));
    model_.AddAtMostOne(shown_role).WithName(name);
    if (role == IMP) {
      // Exactly one player is shown the Imp.
      AddEqualitySum(shown_role, 1);
    }
  }
  const auto& assigned_roles = night_roles_[0];
  for (int i = 0; i < num_players_; ++i) {
    // Being shown a townsfolk role means you either are that role, or DRUNK.
    for (Role role : kTownsfolkRoles) {
      AddImplicationOr(shown_token_[i][role],
                       {assigned_roles[i][role], assigned_roles[i][DRUNK]});
    }
    // Being shown any other role means you are that role.
    for (Role role : kNonTownsfolkRoles) {
      AddImplication(shown_token_[i][role], assigned_roles[i][role]);
      if (role != DRUNK) {
        AddImplication(assigned_roles[i][role], shown_token_[i][role]);
      }
    }
  }
}

void GameState::InitIsEvilVars() {
  // A Player is Evil iff they have an Evil role on Night 1 (in TB).
  for (int i = 0; i < num_players_; ++i) {
    vector<BoolVar> evil_roles;
    for (int role : kEvilRoles) {
      evil_roles.push_back(night_roles_[0][i][role]);
    }
    is_evil_.push_back(CreateEquivalentVarSum(
        evil_roles, absl::StrFormat("evil_%s", players_[i])));
  }
}

void GameState::AddNoStorytellerAnnouncement() {
  log_.add_events()->set_no_storyteller_announcement(true);
  BeforeEvent(Event::kNoStorytellerAnnouncement);
}

void GameState::BeforeEvent(Event::DetailsCase event_type) {
  CHECK_EQ(victory_, TEAM_UNSPECIFIED) << "No events allowed after victory";
  if (event_type == Event::kClaim) {
    CHECK(cur_time_.IsDay) << "Claims can only be made during the day";
  }
  if (event_type == Event::kNight) {
    // If there are Mayor claims, 3 players are alive and there was no
    // execution, this means no Mayor win:
    if (AlivePlayersClaiming(MAYOR).size() > 0 && num_alive_ == 3 &&
        execution_ == kNoPlayer) {
      // Mayor was lying, drunk or poisoned:
      AddOr({Not(CreateAliveRoleVar(MAYOR, cur_time_)),
            CreatePoisonedRoleVar(MAYOR, cur_time_.Count, true)});
    }
    // I saw no elegant way of adding this into AddGameNotOverConstraints that
    // is guarded by next_event_maybe_victory_.
  }
  if (next_event_maybe_victory_) {
    next_event_maybe_victory_ = false;
    if (event_type != Event::kVictory) {
      AddGameNotOverConstraints();
    }
  }
  if (next_event_maybe_death_) {
    next_event_maybe_death_ = false;
    if (event_type != Event::kDeath) {
      AddNoDeathConstraints();  // No night death, or Slayer shot failed.
    }
  }
  if (next_event_maybe_execution_) {
    next_event_maybe_execution_ = false;
    if (event_type != Event::kExecution) {
      AddVirginProcConstraints(false);
    }
  }
}

void GameState::AddEvent(const Event& event) {
  switch (event.details_case()) {
    case Event::kDay:
      AddDay(event.day());
      break;
    case Event::kNight:
      AddNight(event.night());
      break;
    case Event::kStorytellerInteraction:
      AddStorytellerInteraction(event.storyteller_interaction());
      break;
    case Event::kNomination:
      AddNomination(event.nomination());
      break;
    case Event::kVote:
      AddVote(event.vote());
      break;
    case Event::kExecution:
      AddExecution(event.execution());
      break;
    case Event::kDeath:
      AddDeath(event.death());
      break;
    case Event::kClaim:
      AddClaim(event.claim());
      break;
    case Event::kVictory:
      AddVictory(event.victory());
      break;
    case Event::kNoStorytellerAnnouncement:
      AddNoStorytellerAnnouncement();
      break;
    default:
      CHECK(false) << "Expected a valid event details, got: "
                    << event.details_case();
  }
}

void GameState::AddDay(int count) {
  log_.add_events()->set_day(count);
  BeforeEvent(Event::kDay);
  CHECK(!cur_time_.IsDay)
      << "Trying to begin another day during day " << cur_time_.Count;
  cur_time_.IsDay = true;
  CHECK_EQ(count, cur_time_.Count) << absl::StrFormat(
      "Day %d needs to follow night %d", cur_time_.Count, cur_time_.Count);
  InitDayRoleVars();
  nominations_.clear();
  slayer_shots_.clear();
  if (count > 1) {
    next_event_maybe_death_ = true;  // Night death announcements.
  }
  num_votes_ = 0;
  prev_execution_ = execution_death_;
  on_the_block_ = execution_ = execution_death_ = slayer_death_ = night_death_ =
      kNoPlayer;
}

void GameState::AddNight(int count) {
  log_.add_events()->set_night(count);
  BeforeEvent(Event::kNight);
  CHECK(cur_time_.IsDay || cur_time_.Count == 0)
    << "Trying to begin another night during night " << cur_time_.Count;
  cur_time_.IsDay = false;
  if (cur_time_.Count == 0) {
    CHECK_EQ(count, 1) << "The first game night needs to be night 1";
    cur_time_.Count = 1;
    return;
  }
  CHECK_EQ(count, cur_time_.Count + 1) << absl::StrFormat(
      "Night %d needs to follow day %d", cur_time_.Count + 1, cur_time_.Count);
  cur_time_.Count++;
  InitNightRoleVars();
  night_death_ = st_poisoner_pick_ = st_imp_pick_ = st_monk_pick_ =
      st_butler_pick_ = kNoPlayer;
  if (perspective_ == STORYTELLER) {
    for (int i = 0; i < num_players_; ++i) {
      if (!IsFirstNightRole(st_shown_tokens_[i])) {
        night_action_used_[i] = false;
      }
    }
  }
  if (perspective_ == PLAYER) {
    if (!IsFirstNightRole(perspective_player_shown_token_)) {
      night_action_used_[perspective_player_] = false;
    }
  }
}

void GameState::InitImpVars() {
  const string time = cur_time_.ToString();
  vector<BoolVar> imp_picks;
  for (int i = 0; i < num_players_; ++i) {
    string name = absl::StrFormat("imp_picks_%s_%s", players_[i],
                                  cur_time_.Count == 0 ? "1" : time);
    imp_picks.push_back(model_.NewBoolVar().WithName(name));
  }
  imp_pick_.push_back(imp_picks);
  // There is an alive Imp, or the game would have ended.
  // An alive Imp can pick exactly one picks per night starting night 1:
  AddEqualitySum(imp_picks, cur_time_.Count != 0);
}

void GameState::InitMonkVars() {
  // As an optimization for the open strategy, we introduce these only for
  // when Monk has been claimed day 1. This is called starting night 2:
  vector<BoolVar> monk_picks;
  const string time = cur_time_.ToString();
  for (int i = 0; i < num_players_; ++i) {
    monk_picks.push_back(model_.NewBoolVar().WithName(
        absl::StrFormat("monk_picks_%s_%s", players_[i], time)));
  }
  monk_pick_.push_back(monk_picks);
  BoolVar alive_monk = CreateAliveRoleVar(MONK, cur_time_);
  // Alive monk <-> 1 monk pick (and monk goes before the Imp):
  AddEquivalenceSum(alive_monk, monk_picks);
  // A monk cannot pick themselves:
  for (int i = 0; i < num_players_; ++i) {
    AddImplication(monk_picks[i], Not(night_roles_[0][i][MONK]));
  }
  BoolVar poisoned_monk = CreatePoisonerPickedRoleVar(
      MONK, cur_time_.Count, true);
  vector<BoolVar> healthy_monk_protected;
  for (int i = 0; i < num_players_; ++i) {
    healthy_monk_protected.push_back(CreateEquivalentVarAnd(
        {Not(poisoned_monk), monk_picks[i]},
        absl::StrFormat("healthy_monk_picks_%s_%s", players_[i], time)));
  }
  healthy_monk_protected_.push_back(healthy_monk_protected);
}

void GameState::InitPoisonerVars() {
  vector<BoolVar> poisoner_picks;
  const string night = absl::StrFormat(
      "night_%d", cur_time_.Count == 0 ? 1 : cur_time_.Count);
  for (int i = 0; i < num_players_; ++i) {
    string name = absl::StrFormat("poisoner_picks_%s_%s", players_[i], night);
    poisoner_picks.push_back(model_.NewBoolVar().WithName(name));
  }
  poisoner_pick_.push_back(poisoner_picks);
  BoolVar alive_poisoner = CreateAliveRoleVar(POISONER, cur_time_);
  // Alive Poisoner <-> 1 Poisoner pick (and Poisoner goes before the Imp):
  AddEquivalenceSum(alive_poisoner, poisoner_picks);
}

void GameState::InitButlerVars() {
  vector<BoolVar> butler_picks;
  const string night = absl::StrFormat(
      "night_%d", cur_time_.Count == 0 ? 1 : cur_time_.Count);
  for (int i = 0; i < num_players_; ++i) {
    string name = absl::StrFormat("butler_picks_%s_%s", players_[i], night);
    butler_picks.push_back(model_.NewBoolVar().WithName(name));
  }
  butler_pick_.push_back(butler_picks);
  BoolVar alive_butler = CreateAliveRoleVar(BUTLER, cur_time_);
  // Alive Butler <-> 1 Butler pick. TODO(olaola): what if Imp kills Butler?
  AddEquivalenceSum(alive_butler, butler_picks);
  // A Butler cannot pick themselves:
  for (int i = 0; i < num_players_; ++i) {
    AddImplication(butler_picks[i], Not(night_roles_[0][i][BUTLER]));
  }
}

void GameState::InitDayRoleVars() {
  vector<vector<BoolVar>> day_roles(num_players_);
  for (int i = 0; i < num_players_; ++i) {
    day_roles[i].push_back(model_.FalseVar());  // dummy variable
    for (Role role : kAllRoles) {
      string name = absl::StrFormat(
        "role_%s_%s_%s", players_[i], Role_Name(role), cur_time_.ToString());
      day_roles[i].push_back(model_.NewBoolVar().WithName(name));
    }
  }
  AddRoleUniquenessConstraints(day_roles);
  day_roles_.push_back(day_roles);
  if (cur_time_.Count == 1) {
    // No possible starpass on night 1, roles propagate.
    PropagateRoles(night_roles_[0], day_roles, kAllRoles);
    return;
  }
  // All Good roles except Recluse always propagate from night 1:
  PropagateRoles(night_roles_[0], day_roles, kTownsfolkRoles);
  PropagateRoles(night_roles_[0], day_roles, {BUTLER, DRUNK, SAINT});
  // The Minions and Recluse can catch a starpass.
  AddImpStarpassConstraints();
}

void GameState::AddScarletWomanConstraints() {
  // The Scarlet Woman becoming Imp triggers if and only if, on previous day:
  // * SW is alive
  // * SW is not poisoned
  // * The IMP died (executed or Slayer shot)
  // * There are >=4 alive players remaining (not counting the Imp)
  const auto& night_roles = night_roles_.back();
  const auto& day_roles = day_roles_.back();
  if (perspective_ == STORYTELLER) {
    // This is optimization only + sanity check code -- we can compute whether
    // the SW procs and fix the SW/IMP role variables:
    int sw_player = kNoPlayer;
    for (int i = 0; i < num_players_; ++i) {
      if (st_player_roles_[i] == SCARLET_WOMAN) {
        sw_player = i;
        break;
      }
    }
    bool imp_died = (
        (execution_death_ != kNoPlayer &&
         st_player_roles_[execution_death_] == IMP) ||
        (slayer_death_ != kNoPlayer && st_player_roles_[slayer_death_] == IMP));
    if (num_alive_ >= 4 && imp_died && sw_player != kNoPlayer &&
        is_alive_[sw_player] && st_poisoner_pick_ != sw_player) {
      st_player_roles_[sw_player] = IMP;
    }
    for (int i = 0; i < num_players_; ++i) {
      for (Role role : {SCARLET_WOMAN, IMP}) {
        model_.FixVariable(night_roles[i][role], st_player_roles_[i] == role);
      }
    }
    // We do not return here, because we still want to add all the general SW
    // constraints, even though we just fixed the variables, as a sanity check.
  }
  if (num_alive_ < 4 ||
      (execution_death_ == kNoPlayer && slayer_death_ == kNoPlayer)) {
    // Then we know for sure SW can't trigger.
    PropagateRoles(day_roles, night_roles, {SCARLET_WOMAN, IMP});
    return;
  }
  int death = slayer_death_ != kNoPlayer ? slayer_death_ : execution_death_;
  const BoolVar& imp_died = day_roles[death][IMP];
  BoolVar sw_alive = CreateAliveRoleVar(
      SCARLET_WOMAN, {.IsDay = true, .Count = cur_time_.Count - 1});
  BoolVar sw_poisoned = CreatePoisonedRoleVar(
      SCARLET_WOMAN, cur_time_.Count - 1, true);
  BoolVar sw_proc = CreateEquivalentVarAnd(
      {imp_died, sw_alive, Not(sw_poisoned)},
      absl::StrFormat("sw_proc_%s", cur_time_.ToString()));
  for (int i = 0; i < num_players_; ++i) {
    BoolVar night_imp_i = night_roles[i][IMP];
    BoolVar night_sw_i = night_roles[i][SCARLET_WOMAN];
    BoolVar day_imp_i = day_roles[i][IMP];
    BoolVar day_sw_i = day_roles[i][SCARLET_WOMAN];
    AddImplication(day_imp_i, night_imp_i);  // The Imp remains an Imp
    AddImplicationOr(sw_proc, {Not(day_sw_i), night_imp_i});
    AddImplicationOr(Not(sw_proc), {Not(day_sw_i), night_sw_i});
    AddImplicationAnd(night_sw_i, {Not(day_sw_i), sw_proc});
    AddImplicationOr(night_imp_i, {day_imp_i, sw_proc});
  }
}

void GameState::AddImpStarpassConstraints() {
  // If an alive, non-poisoned Imp picks themselves, any alive Minion or (very
  // rarely) a non-poisoned Recluse can catch a starpass. I *don't* want to meta
  // the storyteller here and enforce that a Recluse starpass can only happen
  // if there are no alive minions - it is a possibility regardless.
  const auto& day_roles = day_roles_.back();
  const auto& night_roles = night_roles_.back();
  const string night_name = absl::StrFormat("night_%d", cur_time_.Count);
  // TODO(olaola): add Storyteller perspective, to fix st_role vars.
  // Starpass trigger: an alive, non-poisoned Imp picks themselves.
  vector<BoolVar> player_starpass;
  for (int i = 0; i < num_players_; ++i) {
    if (is_alive_[i]) {
      BoolVar starpass_i = CreateEquivalentVarAnd(
          {night_roles[i][IMP], Not(poisoner_pick_.back()[i]),
           imp_pick_.back()[i]},
          absl::StrFormat("%s_starpass_%s", players_[i], night_name));
      player_starpass.push_back(starpass_i);
    }
  }
  BoolVar starpass = CreateEquivalentVarSum(
      player_starpass,
      absl::StrFormat("starpass_triggers_%s", night_name));
  vector<BoolVar> player_catch;
  vector<BoolVar> eligible;
  BoolVar nobody_eligible = model_.NewBoolVar().WithName(
      absl::StrFormat("nobody_eligible_for_starpass_%s", night_name));
  for (int i = 0; i < num_players_; ++i) {
    // The Imp remains an Imp, dead or alive.
    const BoolVar& day_imp_i = day_roles[i][IMP];
    const BoolVar& night_imp_i = night_roles[i][IMP];
    AddImplication(night_imp_i, day_imp_i);
    if (!is_alive_[i]) {
      // Dead players can't catch a starpass.
      PropagateRolesForPlayer(i, night_roles, day_roles, kMinionRoles);
      PropagateRolesForPlayer(i, night_roles, day_roles, {RECLUSE});
    } else {
      BoolVar catch_i = CreateEquivalentVarAnd(
          {Not(night_imp_i), day_imp_i},
          absl::StrFormat("%s_catches_starpass_%s", players_[i], night_name));
      BoolVar healthy_recluse_i = CreateEquivalentVarAnd(
          {night_roles[i][RECLUSE], Not(poisoner_pick_.back()[i])},
          absl::StrFormat("healthy_recluse_%s_%s", players_[i], night_name));
      vector<BoolVar> eligible_i;
      // eligible_i <-> i is minion or healthy Recluse
      for (Role role : {POISONER, SPY, SCARLET_WOMAN, BARON, RECLUSE}) {
        const BoolVar& night_role_i = night_roles[i][role];
        const BoolVar& day_role_i = day_roles[i][role];
        const BoolVar& e = role == RECLUSE ? healthy_recluse_i : night_role_i;
        eligible_i.push_back(e);
        eligible.push_back(e);
        AddImplication(e, Not(nobody_eligible));
        model_.AddEquality(night_role_i, day_role_i)
              .OnlyEnforceIf(Not(catch_i))
              .WithName(absl::StrFormat(
                  "%s -> %s = %s", Not(catch_i).Name(), night_role_i.Name(),
                  day_role_i.Name()));
      }
      AddImplicationSum(catch_i, eligible_i, 1);
    }
  }
  AddImplicationSum(nobody_eligible, eligible, 0);
  // !starpass -> exactly zero catch
  AddImplicationSum(Not(starpass), player_catch, 0);
  // starpass -> exactly one catches OR nobody eligible
  player_catch.push_back(nobody_eligible);
  AddImplicationSum(starpass, player_catch, 1);
}

vector<BoolVar> GameState::CollectRolesForPlayer(
    const vector<vector<BoolVar>>& from, int player,
    absl::Span<const Role> roles, bool only_alive) const {
  vector<BoolVar> result;
  if (!only_alive || is_alive_[player]) {
    for (Role role : roles) {
      // Optimization for the open strategy: for good roles, we only need to
      // consider players who claimed the role. This, of course, applies only
      // starting day 1 (we rely on everyone claiming on start of day 1):
      if (IsGoodRole(role) && (cur_time_.IsDay || cur_time_.Count > 1) &&
          claim_of_player_[player] != role) {
        continue;
      }
      result.push_back(from[player][role]);
    }
  }
  return result;
}

vector<BoolVar> GameState::CollectRoles(const vector<vector<BoolVar>>& from,
                                        absl::Span<const Role> roles,
                                        bool only_alive) const {
  vector<BoolVar> result;
  for (int i = 0; i < num_players_; ++i) {
    for (const auto& v : CollectRolesForPlayer(from, i, roles, only_alive)) {
      result.push_back(v);
    }
  }
  return result;
}

vector<BoolVar> GameState::CollectRoles(const vector<vector<BoolVar>>& from,
                                        absl::Span<const Role> roles)  const {
  return CollectRoles(from, roles, false);
}

vector<BoolVar> GameState::CollectAliveRoles(
    const vector<vector<BoolVar>>& from, absl::Span<const Role> roles)  const {
  return CollectRoles(from, roles, true);
}

void GameState::PropagateRoles(const vector<vector<BoolVar>>& from,
                               const vector<vector<BoolVar>>& to,
                               absl::Span<const Role> roles) {
  for (int i = 0; i < num_players_; ++i) {
    PropagateRolesForPlayer(i, from, to, roles);
  }
}

BoolVar GameState::CreateAliveRoleVar(Role role, const internal::Time& time) {
  // At current time:
  const auto& roles = time.IsDay ? day_roles_ : night_roles_;
  const int count = time.Count == 0 ? 0 : time.Count - 1;
  return CreateEquivalentVarSum(
      CollectAliveRoles(roles[count], {role}),
      absl::StrFormat("alive_%s_%s", Role_Name(role), time.ToString()));
}

void GameState::PropagateRolesForPlayer(
    int player,
    const vector<vector<BoolVar>>& from,
    const vector<vector<BoolVar>>& to,
    absl::Span<const Role> roles) {
  for (Role role : roles) {
    const BoolVar& v_from = from[player][role];
    const BoolVar& v_to = to[player][role];
    string name = absl::StrFormat(
        "Player %s role %s propagation: %s = %s", players_[player],
        Role_Name(role), v_from.Name(), v_to.Name());
    model_.AddEquality(v_from, v_to).WithName(name);
    // Optimization: for storyteller perspective, we can fix these vars:
    if (perspective_ == STORYTELLER) {
      model_.FixVariable(to[player][role], st_player_roles_[player] == role);
    }
  }
}

void GameState::AddStorytellerInteraction(
    const StorytellerInteraction& interaction) {
  switch (interaction.details_case()) {
    case StorytellerInteraction::kShownToken:
      AddShownToken(interaction.player(), interaction.shown_token());
      break;
    case StorytellerInteraction::kMinionInfo:
      AddMinionInfo(interaction.player(), interaction.minion_info());
      break;
    case StorytellerInteraction::kDemonInfo:
      AddDemonInfo(interaction.player(), interaction.demon_info());
      break;
    case StorytellerInteraction::kRoleAction:
      AddRoleAction(interaction.player(), interaction.role_action());
      break;
    default:
      CHECK(false) << "Expected a valid interaction details, got: "
                   << interaction.details_case();
  }
}

void GameState::AddNomination(const Nomination& nomination) {
  AddNomination(nomination.nominator(), nomination.nominee());
}

void GameState::AddNomination(const string& nominator, const string& nominee) {
  auto* nomination_pb = log_.add_events()->mutable_nomination();
  nomination_pb->set_nominator(nominator);
  nomination_pb->set_nominee(nominee);
  BeforeEvent(Event::kNomination);
  CHECK(cur_time_.IsDay) << "Nominations can only occur during the day.";
  const int nominator_index = PlayerIndex(nominator);
  const int nominee_index = PlayerIndex(nominee);
  CHECK(is_alive_[nominator_index])
      << nominator << " is dead and cannot nominate.";
  for (const auto& nom : nominations_) {
    CHECK_NE(nom.Nominator, nominator_index)
      << nominator << " has already nominated today.";
    CHECK_NE(nom.Nominee, nominee_index)
        << nominee << " has already been nominated today.";
  }
  nominations_.push_back(
      {.Nominator = nominator_index, .Nominee = nominee_index});
  if (!player_has_been_nominated_[nominee_index]) {
    next_event_maybe_execution_ = true;
  }
  player_has_been_nominated_[nominee_index] = true;
}

void GameState::AddVote(const Vote& vote) {
  // I should be able to use the votes repeated field as absl::Span, but I
  // failed at that, so I'll just copy it to a vector:
  vector<string> votes(vote.votes().begin(), vote.votes().end());
  AddVote(votes, vote.on_the_block());
}

void GameState::AddVote(absl::Span<const string> votes,
                        const string& on_the_block) {
  Vote* vote_pb = log_.add_events()->mutable_vote();
  vote_pb->mutable_votes()->Assign(votes.begin(), votes.end());
  vote_pb->set_on_the_block(on_the_block);
  BeforeEvent(Event::kVote);
  CHECK(!nominations_.empty()) << "A vote must have a preceding nomination.";
  const auto& nomination = nominations_.back();
  // TODO(olaola): validate vote correctness better!
  for (const string& name : votes) {
    CHECK_NE(PlayerIndex(name), kNoPlayer) << "Invalid voter " << name;
  }
  int cur_block = kNoPlayer;
  if (!on_the_block.empty()) {
    cur_block = PlayerIndex(on_the_block);
  }
  int cur_votes = votes.size();
  if (on_the_block_ == kNoPlayer) {
    int votes_required =
        num_votes_ == 0 ? (num_alive_ + 1) / 2 : num_votes_ + 1;
    if (cur_votes < votes_required) {
      // Vote fails, nothing changed.
      CHECK_EQ(cur_block, on_the_block_)
          << absl::StrFormat("Needed %d votes to put %s on the block, got %d",
                             votes_required, players_[nomination.Nominee],
                             cur_votes);
    } else {
      // Vote succeeds.
      num_votes_ = cur_votes;
      CHECK_EQ(cur_block, nomination.Nominee)
          << absl::StrFormat("%s expected to go on the block, got: %s",
                             players_[nomination.Nominee], on_the_block);
    }
  } else {
    if (cur_votes < num_votes_) {
      // Vote fails, nothing changed.
      CHECK_EQ(cur_block, on_the_block_)
          << absl::StrFormat("Needed %d votes to put %s on the block, got %d",
                             num_votes_ + 1, players_[nomination.Nominee],
                             cur_votes);
    } else if (cur_votes == num_votes_) {
      // Tied vote, no one on the block.
      CHECK_EQ(cur_block, kNoPlayer)
          << absl::StrFormat("Tied vote, no one goes on the block, got: %s",
                             on_the_block);
    } else {
      // Vote succeeds.
      num_votes_ = cur_votes;
      CHECK_EQ(cur_block, nomination.Nominee)
          << absl::StrFormat("%s expected to go on the block, got: %s",
                             players_[nomination.Nominee], on_the_block);
    }
  }
  on_the_block_ = cur_block;
  // TODO(olaola): add Butler constraints here (could only vote with master).
}

void GameState::AddExecution(const string& name) {
  log_.add_events()->set_execution(name);
  BeforeEvent(Event::kExecution);
  CHECK(cur_time_.IsDay) << "Executions can only occur during the day.";
  const int executee = PlayerIndex(name);
  CHECK_EQ(execution_, kNoPlayer) << "More than one execution attempted.";
  CHECK(!nominations_.empty()) << "Execution must have a preceding nomination.";

  if (executee != on_the_block_) {
    const auto& nomination = nominations_.back();
    CHECK_EQ(executee, nomination.Nominator)
        << absl::StrFormat("Execution needs to be either of %s (who is on the "
                            "block), or of %s who is last to nominate, got %s",
                            (on_the_block_ == kNoPlayer ? "nobody"
                              : players_[on_the_block_]),
                            players_[nomination.Nominator], name);
    AddVirginProcConstraints(true);
  }
  execution_ = executee;
}

void GameState::AddDeath(const string& name) {
  log_.add_events()->set_death(name);
  BeforeEvent(Event::kDeath);
  // Deaths are Storyteller announcements of deaths, hence they only occur
  // during the day.
  CHECK(cur_time_.IsDay) << "Death annoucements can only occur during the day.";
  const int death = PlayerIndex(name);
  CHECK(is_alive_[death]) << "What is dead may never die: " << name;
  // Deaths are either an announced night death, Slayer shots, or executions,
  // in that order.
  const string time = cur_time_.ToString();
  if (execution_ != kNoPlayer) {
    // This can only be an execution death, due to order.
    CHECK_EQ(death, execution_)
        << absl::StrFormat("Expected death of executee %s, got %s.",
                           players_[execution_], name);
    execution_death_ = death;
  } else if (!slayer_shots_.empty()) {
    // Must be a Slayer kill, due to order.
    slayer_death_ = death;
    const auto& shot = slayer_shots_.back();
    int target = shot.Target, slayer = shot.Slayer;
    CHECK_EQ(death, target)
        << absl::StrFormat("Expected death of Slayer shot %s, got %s.",
                           players_[target], name);
    CHECK(is_alive_[slayer])
        << "Slayer " << players_[slayer] << " needs to be alive to proc.";
    // We can deduce:
    // * Slayer was actually the Slayer
    // * Slayer was not poisoned
    // * Target was the Imp OR a non-poisoned Recluse
    const BoolVar& is_slayer = day_roles_.back()[slayer][SLAYER];
    const BoolVar& imp = day_roles_.back()[target][IMP];
    const BoolVar poisoned = CreatePoisonedRoleVar(
        SLAYER, cur_time_.Count, true);
    // Open strategy optimization: the actual Recluse would hardclaim day 1.
    if (claim_of_player_[target] == RECLUSE) {
      BoolVar poisoned_recluse = CreatePoisonedRoleVar(
          RECLUSE, cur_time_.Count, true);
      BoolVar healthy_recluse = CreateEquivalentVarAnd(
        {day_roles_.back()[target][RECLUSE], Not(poisoned_recluse)},
        absl::StrFormat("%s_healthy_recluse", players_[target]));
      BoolVar target_proc = CreateEquivalentVarSum(
          {healthy_recluse, imp},
          absl::StrFormat("%s_healthy_recluse_or_imp", players_[target]));
      AddAnd({is_slayer, target_proc, Not(poisoned)});
    } else {
      AddAnd({is_slayer, imp, Not(poisoned)});
    }
  } else {
    CHECK_EQ(night_death_, kNoPlayer)
        << "No two night deaths in Trouble Brewing";
    // Must be a night death, which is either an Imp pick, or a Mayor bounce.
    night_death_ = death;
    const auto& poisoner_pick = poisoner_pick_.back();
    // The Imp cannot be poisoned:
    for (int i = 0; i < num_players_; ++i) {
      if (is_alive_[i]) {
        AddImplication(night_roles_.back()[i][IMP], Not(poisoner_pick[i]));
      }
    }
    const bool possible_monk = AlivePlayersClaiming(MONK).size() > 0;
    // The target cannot be Monk-protected by a healthy Monk:
    if (possible_monk) {
      model_.FixVariable(healthy_monk_protected_.back()[death], false);
    }
    // The target cannot be a non-poisoned Soldier:
    AddOr({poisoner_pick[death], Not(night_roles_[0][death][SOLDIER])});

    vector<BoolVar> picks;
    picks.push_back(imp_pick_.back()[death]);
    for (int i : AlivePlayersClaiming(MAYOR)) {
      if (death != i) {  // Possible Mayor bounce
        // If Imp picked, Mayor needs to be non-poisoned and not protected by
        // a healthy Monk:
        const BoolVar& picked = imp_pick_.back()[i];
        picks.push_back(picked);
        vector<BoolVar> reqs(
            {night_roles_[0][i][MAYOR], Not(poisoner_pick[i])});
        if (possible_monk) {
          reqs.push_back(Not(healthy_monk_protected_.back()[i]));
        }
        AddImplicationAnd(picked, reqs);
      }
    }
    AddEqualitySum(picks, 1);
  }
  is_alive_[death] = false;
  --num_alive_;
  next_event_maybe_victory_ = true;
}

void GameState::AddAllClaims(absl::Span<const Role> roles,
                             const string& starting_player) {
  int i = PlayerIndex(starting_player);
  for (Role role : roles) {
    AddClaim(players_[i], role);
    i = (i + 1) % num_players_;
  }
}

void GameState::AddClaim(const string& player, Role role) {
  Claim* claim_pb = log_.add_events()->mutable_claim();
  claim_pb->set_player(player);
  claim_pb->set_role(role);
  BeforeEvent(Event::kClaim);
  CHECK(cur_time_.IsDay) << "Claims can only be made during the day";
  // The open strategy assumes that only Evil lies. Hence:
  // claim_role_X -> shown_token_X V is_evil
  // The only exception is a Recluse Imp claim after a Recluse starpass (which
  // still technically means they were shown the Imp token upon starpass, but we
  // only encode the night 1 shown tokens).
  const int p_index = PlayerIndex(player);
  claim_of_player_[p_index] = role;
  players_claiming_[role].push_back(p_index);
  if (role == IMP) {
    // Recluse starpass exception. Someone claiming Imp is either currently
    // the Good Imp, or some sort of starting Evil.
    AddOr({day_roles_.back()[p_index][role], is_evil_[p_index]});
    return;
  }
  AddOr({shown_token_[p_index][role], is_evil_[p_index]});
}

void GameState::AddClaim(const Claim& claim) {
  const string& player = claim.player();
  if (!claim.has_info()) {
    AddClaim(player, claim.role());
    return;
  }
  switch (claim.info().details_case()) {
    case RoleAction::kWasherwomanInfo:
      AddClaimWasherwomanInfo(player, claim.info().washerwoman_info());
      break;
    case RoleAction::kLibrarianInfo:
      AddClaimLibrarianInfo(player, claim.info().librarian_info());
      break;
    case RoleAction::kInvestigatorInfo:
      AddClaimInvestigatorInfo(player, claim.info().investigator_info());
      break;
    case RoleAction::kChefInfo:
      AddClaimChefInfo(player, claim.info().chef_info());
      break;
    case RoleAction::kEmpathInfo:
      AddClaimEmpathInfo(player, claim.info().empath_info());
      break;
    case RoleAction::kFortunetellerAction:
      AddClaimFortuneTellerAction(player, claim.info().fortuneteller_action());
      break;
    case RoleAction::kMonkAction:
      AddClaimMonkAction(player, claim.info().monk_action());
      break;
    case RoleAction::kButlerAction:
      AddClaimButlerAction(player, claim.info().butler_action());
      break;
    case RoleAction::kRavenkeeperAction:
      AddClaimRavenkeeperAction(player, claim.info().ravenkeeper_action());
      break;
    case RoleAction::kUndertakerInfo:
      AddClaimUndertakerInfo(player, claim.info().undertaker_info());
      break;
    case RoleAction::kSlayerAction:
      CHECK(false)
          << "Only role claims and claims about used night abilities are "
          << "allowed. Claim SLAYER with no action instead.";
      break;
    case RoleAction::kPoisonerAction:
      CHECK(false)
          << "Claiming poisoner action does not convey info. Claim POISONER "
          << "with no info instead";
      break;
    case RoleAction::kImpAction:
      AddClaimImpAction(player, claim.info().imp_action());
      break;
    case RoleAction::kSpyInfo:
      AddClaimSpyInfo(player, claim.info().spy_info());
      break;
    default:
      CHECK(false) << "Expected a valid claim info details, got: "
                   << claim.info().details_case();
  }
}

void GameState::AddClaimWasherwomanInfo(
    const string& player, const string& ping1, const string& ping2,
    Role role) {
  Claim* claim_pb = log_.add_events()->mutable_claim();
  claim_pb->set_player(player);
  LearnRoleInfo* info_pb = claim_pb->mutable_info()->mutable_washerwoman_info();
  info_pb->set_ping1(ping1);
  info_pb->set_ping2(ping2);
  info_pb->set_role(role);
  BeforeEvent(Event::kClaim);
  CHECK_EQ(ClaimedRole(player), WASHERWOMAN)  // because it's clearer, imo
      << player << " needs to claim WASHERWOMAN before claiming info";
  CHECK(IsTownsfolkRole(role));
  AddLearningRoleInfoConstraints(player, WASHERWOMAN, ping1, ping2, role);
}

void GameState::AddClaimWasherwomanInfo(
    const string& player, const LearnRoleInfo& info) {
  AddClaimWasherwomanInfo(player, info.ping1(), info.ping2(), info.role());
}

void GameState::AddClaimLibrarianInfo(
    const string& player, const string& ping1, const string& ping2,
    Role role) {
  Claim* claim_pb = log_.add_events()->mutable_claim();
  claim_pb->set_player(player);
  LearnRoleInfo* info_pb = claim_pb->mutable_info()->mutable_librarian_info();
  info_pb->set_ping1(ping1);
  info_pb->set_ping2(ping2);
  info_pb->set_role(role);
  BeforeEvent(Event::kClaim);
  CHECK_EQ(ClaimedRole(player), LIBRARIAN)  // because it's clearer, imo
      << player << " needs to claim LIBRARIAN before claiming info";
  CHECK(IsOutsiderRole(role));
  AddLearningRoleInfoConstraints(player, LIBRARIAN, ping1, ping2, role);
}

void GameState::AddClaimLibrarianInfo(
    const string& player, const LearnRoleInfo& info) {
  AddClaimLibrarianInfo(player, info.ping1(), info.ping2(), info.role());
}

void GameState::AddClaimInvestigatorInfo(
    const string& player, const string& ping1, const string& ping2, Role role) {
  BeforeEvent(Event::kClaim);
  Claim* claim_pb = log_.add_events()->mutable_claim();
  claim_pb->set_player(player);
  LearnRoleInfo* info_pb =
      claim_pb->mutable_info()->mutable_investigator_info();
  info_pb->set_ping1(ping1);
  info_pb->set_ping2(ping2);
  info_pb->set_role(role);
  CHECK_EQ(ClaimedRole(player), INVESTIGATOR)  // because it's clearer, imo
      << player << " needs to claim INVESTIGATOR before claiming info";
  CHECK(IsMinionRole(role));
  AddLearningRoleInfoConstraints(player, INVESTIGATOR, ping1, ping2, role);
}

void GameState::AddClaimInvestigatorInfo(
    const string& player, const LearnRoleInfo& info) {
  AddClaimInvestigatorInfo(player, info.ping1(), info.ping2(), info.role());
}

void GameState::AddClaimChefInfo(const string& player, int chef_info) {
  Claim* claim_pb = log_.add_events()->mutable_claim();
  claim_pb->set_player(player);
  claim_pb->mutable_info()->set_chef_info(chef_info);
  BeforeEvent(Event::kClaim);
}

vector<int> GameState::AliveNeighbors(int player) const {
  CHECK_GE(num_alive_, 3) << "Less than 3 alive players, game didn't end";
  vector<int> result;
  int i = (player + 1) % num_players_;
  while (!is_alive_[i]) {
    i = (i + 1) % num_players_;
  }
  result.push_back(i);
  i = (player + num_players_ - 1) % num_players_;
  while (!is_alive_[i]) {
    i = (i  + num_players_ - 1) % num_players_;
  }
  result.push_back(i);
  return result;
}

void GameState::AddClaimEmpathInfo(const string& player, int empath_info) {
  // We don't check that empath_info is in [0,2], because in rare cases the
  // Storyteller technically could give a higher number to inform the Empath
  // that they are drunk or poisoned (if Good really needs help).
  Claim* claim_pb = log_.add_events()->mutable_claim();
  claim_pb->set_player(player);
  claim_pb->mutable_info()->set_empath_info(empath_info);
  CHECK_GE(empath_info, 0) << "Expected non-negative Empath info";
  BeforeEvent(Event::kClaim);
  const int i = PlayerIndex(player);
  const string time = cur_time_.ToString();
  vector<int> alive_neighbors = AliveNeighbors(i);  // Size 2.
  const int ping1 = alive_neighbors[0], ping2 = alive_neighbors[1];
  const BoolVar& is_empath = day_roles_.back()[i][EMPATH];
  BoolVar poisoned_i = CreatePoisonedRoleVar(EMPATH, cur_time_.Count, true);
  BoolVar poisoned_spy = CreatePoisonedRoleVar(SPY, cur_time_.Count, true);
  const bool possible_recluse = (claim_of_player_[ping1] == RECLUSE ||
                                 claim_of_player_[ping2] == RECLUSE);
  BoolVar poisoned_recluse = (possible_recluse ?
      model_.FalseVar() :
      CreatePoisonedRoleVar(RECLUSE, cur_time_.Count, true));
  BoolVar ping1_healthy_spy = CreateEquivalentVarAnd(
      {day_roles_.back()[ping1][SPY], Not(poisoned_spy)},
      absl::StrFormat("healthy_spy_%s_%s", players_[ping1], time));
  BoolVar ping2_healthy_spy = CreateEquivalentVarAnd(
      {day_roles_.back()[ping2][SPY], Not(poisoned_spy)},
      absl::StrFormat("healthy_spy_%s_%s", players_[ping2], time));
  BoolVar ping1_healthy_recluse = (claim_of_player_[ping1] != RECLUSE ?
      model_.FalseVar() :
      CreateEquivalentVarAnd(
        {day_roles_.back()[ping1][RECLUSE], Not(poisoned_recluse)},
        absl::StrFormat("healthy_recluse_%s_%s", players_[ping1], time)));
  BoolVar ping2_healthy_recluse = (claim_of_player_[ping2] != RECLUSE ?
      model_.FalseVar() :
      CreateEquivalentVarAnd(
        {day_roles_.back()[ping2][RECLUSE], Not(poisoned_recluse)},
        absl::StrFormat("healthy_recluse_%s_%s", players_[ping2], time)));
  BoolVar ping1_regs_good = CreateEquivalentVarOr(
      {Not(is_evil_[ping1]), ping1_healthy_spy},
      absl::StrFormat("empath_%s_registers_%s_good_%s", player, players_[ping1],
                      time));
  BoolVar ping2_regs_good = CreateEquivalentVarOr(
      {Not(is_evil_[ping2]), ping2_healthy_spy},
      absl::StrFormat("empath_%s_registers_%s_good_%s", player, players_[ping2],
                      time));
  BoolVar ping1_regs_evil = CreateEquivalentVarOr(
      {is_evil_[ping1], ping1_healthy_recluse},
      absl::StrFormat("empath_%s_registers_%s_evil_%s", player, players_[ping1],
                      time));
  BoolVar ping2_regs_evil = CreateEquivalentVarOr(
      {is_evil_[ping2], ping2_healthy_recluse},
      absl::StrFormat("empath_%s_registers_%s_evil_%s", player, players_[ping2],
                      time));
  // A healthy alive Recluse *may* register as evil.
  // A healthy alive Spy *may* register as good.
  vector<BoolVar> cases({{Not(is_empath), poisoned_i}});
  switch (empath_info) {
    case 0:
      cases.push_back(CreateEquivalentVarAnd(
          {ping1_regs_good, ping2_regs_good},
          absl::StrFormat(
            "empath_0_%s_on_%s_and_%s_%s", player, players_[ping1],
            players_[ping2], time)));
      break;
    case 1:
      cases.push_back(CreateEquivalentVarAnd(
          {ping1_regs_good, ping2_regs_evil},
          absl::StrFormat(
            "empath_1_%s_on_%s_and_%s_%s_case1", player, players_[ping1],
            players_[ping2], time)));
      cases.push_back(CreateEquivalentVarAnd(
          {ping1_regs_evil, ping2_regs_good},
          absl::StrFormat(
            "empath_1_%s_on_%s_and_%s_%s_case2", player, players_[ping1],
            players_[ping2], time)));
      break;
    case 2:
      cases.push_back(CreateEquivalentVarAnd(
          {ping1_regs_evil, ping2_regs_evil},
          absl::StrFormat(
            "empath_2_%s_on_%s_and_%s_%s", player, players_[ping1],
            players_[ping2], time)));
      break;
    default:
      // Empath is definitely lying, drunk or poisoned.
      break;
  }
  AddOr(cases);
  deferred_constraints_[PlayerIndex(player)] = false;
}

void GameState::AddClaimFortuneTellerAction(
    const string& player, const string& pick1, const string& pick2, bool yes) {
  Claim* claim_pb = log_.add_events()->mutable_claim();
  claim_pb->set_player(player);
  FortuneTellerAction* action_pb =
      claim_pb->mutable_info()->mutable_fortuneteller_action();
  action_pb->set_pick1(pick1);
  action_pb->set_pick2(pick2);
  action_pb->set_yes(yes);
  CHECK_NE(pick1, pick2)
      << "Fortune Teller needs to pick two different players";
  BeforeEvent(Event::kClaim);
  const int i = PlayerIndex(player);
  const int p1 = PlayerIndex(pick1), p2 = PlayerIndex(pick2);
  vector<BoolVar> yes_options({night_roles_.back()[p1][IMP], red_herring_[p1],
                               night_roles_.back()[p2][IMP], red_herring_[p2]});
  BoolVar poisoned_recluse;
  // This optimization is why we want to finish the role claims before claming
  // role info.
  if (AlivePlayersClaiming(RECLUSE).size() > 0) {
    poisoned_recluse = CreatePoisonedRoleVar(RECLUSE, cur_time_.Count, true);
  }
  if (yes) {  // We can only infer Recluse possibilities from Yes answer.
    for (int ping : {p1, p2}) {
      if (is_alive_[ping] && claim_of_player_[ping] == RECLUSE) {
        yes_options.push_back(CreateEquivalentVarAnd(
          {night_roles_.back()[ping][RECLUSE], Not(poisoned_recluse)},
          absl::StrFormat("healthy_recluse_%s_%s", players_[ping],
                          cur_time_.ToString())));
      }
    }
  }
  vector<BoolVar> cases({
      Not(night_roles_.back()[i][FORTUNE_TELLER]),
      CreatePoisonedRoleVar(FORTUNE_TELLER, cur_time_.Count, true)});
  BoolVar is_yes = CreateEquivalentVarOr(
    yes_options,
    absl::StrFormat("fortune_teller_yes_cases_%s", cur_time_.ToString()));
  cases.push_back(yes ? is_yes : Not(is_yes));
  AddOr(cases);
  deferred_constraints_[i] = false;
}

void GameState::AddClaimFortuneTellerAction(
    const string& player, const FortuneTellerAction& action) {
  AddClaimFortuneTellerAction(
      player, action.pick1(), action.pick2(), action.yes());
}

void GameState::AddClaimMonkAction(const string& player,
                                   const string& monk_action) {
  Claim* claim_pb = log_.add_events()->mutable_claim();
  claim_pb->set_player(player);
  claim_pb->mutable_info()->set_monk_action(monk_action);
  CHECK_NE(player, monk_action) << "Monk cannot pick themselves";
  BeforeEvent(Event::kClaim);
  const int i = PlayerIndex(player), target = PlayerIndex(monk_action);
  AddImplication(night_roles_.back()[i][MONK], monk_pick_.back()[target]);
  deferred_constraints_[i] = false;
}

void GameState::AddClaimButlerAction(const string& player,
                                     const string& butler_action) {
  Claim* claim_pb = log_.add_events()->mutable_claim();
  claim_pb->set_player(player);
  claim_pb->mutable_info()->set_butler_action(butler_action);
  CHECK_NE(player, butler_action) << "Butler cannot pick themselves";
  BeforeEvent(Event::kClaim);
  const int i = PlayerIndex(player), target = PlayerIndex(butler_action);
  AddImplication(night_roles_.back()[i][BUTLER], butler_pick_.back()[target]);
  deferred_constraints_[i] = false;
}

void GameState::AddClaimRavenkeeperAction(
    const string& player, const string& pick, Role role) {
  Claim* claim_pb = log_.add_events()->mutable_claim();
  claim_pb->set_player(player);
  RavenkeeperAction* action_pb =
      claim_pb->mutable_info()->mutable_ravenkeeper_action();
  action_pb->set_pick(pick);
  action_pb->set_role(role);
  CHECK_NE(role, ROLE_UNSPECIFIED) << "Ravenkeeper needs to learn a valid role";
  BeforeEvent(Event::kClaim);
  const int i = PlayerIndex(player), target = PlayerIndex(pick);
  CHECK_EQ(i, night_death_)
      << player << " didn't die at night, Ravenkeeper wouldn't trigger.";
  AddOr({Not(night_roles_.back()[i][RAVENKEEPER]), poisoner_pick_.back()[i],
         night_roles_.back()[target][role]});
  deferred_constraints_[i] = false;
}

void GameState::AddClaimRavenkeeperAction(const string& player,
                                        const RavenkeeperAction& info) {
  AddClaimRavenkeeperAction(player, info.pick(), info.role());
}

void GameState::AddClaimUndertakerInfo(const string& player, Role info) {
  Claim* claim_pb = log_.add_events()->mutable_claim();
  claim_pb->set_player(player);
  claim_pb->mutable_info()->set_undertaker_info(info);
  BeforeEvent(Event::kClaim);
  CHECK_EQ(ClaimedRole(player), UNDERTAKER)
      << player << " needs to claim UNDERTAKER before claiming info";
  CHECK_NE(prev_execution_, kNoPlayer);  // Otherwise noone to exhume.
  CHECK_NE(info, ROLE_UNSPECIFIED) << "Expected valid Undertaker info";
  const int i = PlayerIndex(player);
  BoolVar poisoned = CreatePoisonedRoleVar(
      UNDERTAKER, cur_time_.Count - 1, true);
  const BoolVar& correct = night_roles_.back()[prev_execution_][info];
  AddImplicationOr(night_roles_[0][i][UNDERTAKER], {poisoned, correct});
}

// The open Spy play.
void GameState::AddClaimSpyInfo(const string& player, const SpyInfo& spy_info) {
  Claim* claim_pb = log_.add_events()->mutable_claim();
  claim_pb->set_player(player);
  *(claim_pb->mutable_info()->mutable_spy_info()) = spy_info;
  BeforeEvent(Event::kClaim);
}

// Useless, but may theoretically occur after Recluse starpass.
void GameState::AddClaimImpAction(const string& player,
                                  const string& imp_action) {
  Claim* claim_pb = log_.add_events()->mutable_claim();
  claim_pb->set_player(player);
  claim_pb->mutable_info()->set_imp_action(imp_action);
  BeforeEvent(Event::kClaim);
}

void GameState::AddVictory(Team victory) {
  log_.add_events()->set_victory(victory);
  BeforeEvent(Event::kVictory);
  CHECK_NE(victory, TEAM_UNSPECIFIED) << "Victory needs to be GOOD or EVIL";
  CHECK_EQ(victory_, TEAM_UNSPECIFIED)
      << "Team " << Team_Name(victory_) << " has already won.";
  CHECK(cur_time_.IsDay) << "Victory can only be announced during the day.";
  victory_ = victory;
  if (victory_ == GOOD) {
    AddGoodWonConstraints();
  } else {
    AddEvilWonConstraints();
  }
}

// Syntactic sugar for the Storyteller perspective.
void GameState::AddAllShownTokens(absl::Span<const Role> roles) {
  CHECK_EQ(perspective_, STORYTELLER)
      << "Only the Storyteller perspective can show all tokens";
  int i = 0;
  for (Role role : roles) {
    AddShownToken(players_[i++], role);
  }
}

void GameState::AddShownToken(const string& player, Role role) {
  auto* si_pb = log_.add_events()->mutable_storyteller_interaction();
  si_pb->set_player(player);
  si_pb->set_shown_token(role);
  BeforeEvent(Event::kStorytellerInteraction);
  CHECK_NE(role, ROLE_UNSPECIFIED) << "Need to specify a role";
  CHECK_NE(role, DRUNK) << "No one can be shown the DRUNK token";
  CHECK_NE(perspective_, OBSERVER) << "Observer cannot be shown tokens";
  CHECK(!cur_time_.IsDay) << "Tokens are only shown at night";
  CHECK(cur_time_.Count == 1 || role == IMP)
      << "Tokens other than Imp are only shown on night 1";
  const int p_index = PlayerIndex(player);
  if (perspective_ == STORYTELLER) {
    // TODO(olaola): validate role change.
    st_shown_tokens_[p_index] = role;
  } else {  // PLAYER
    CHECK(perspective_player_ == kNoPlayer || perspective_player_ == p_index)
        << "Only " << player << " can be shown a token in player perspective";
    perspective_player_ = p_index;  // Deducing the perspective player.
    // TODO(olaola): validate role change.
    perspective_player_shown_token_ = role;
  }
  if (cur_time_.Count == 1) {
    model_.FixVariable(shown_token_[p_index][role], true);
  } else {  // role is IMP
    // TODO(olaola): fill this case (starpass/SW proc occurred).
  }
}

Role GameState::ShownToken(const string& player) const {
  switch (perspective_) {
    case OBSERVER:
      return ROLE_UNSPECIFIED;
    case PLAYER:
      return perspective_player_shown_token_;
    case STORYTELLER:
      return st_shown_tokens_[PlayerIndex(player)];
    default:
      CHECK(false) << "Invalid game state: unset perspective";
  }
}

void GameState::AddMinionInfo(const string& player, const string& demon,
                              absl::Span<const string> minions) {
  auto* si_pb = log_.add_events()->mutable_storyteller_interaction();
  si_pb->set_player(player);
  MinionInfo* info_pb = si_pb->mutable_minion_info();
  info_pb->set_demon(demon);
  info_pb->mutable_minions()->Assign(minions.begin(), minions.end());
  BeforeEvent(Event::kStorytellerInteraction);
  CHECK_GE(num_players_, 7) << "Minion info unavailable for < 7 players";
  CHECK(perspective_ == STORYTELLER || perspective_ == PLAYER)
      << "Minion info can only be shown in player or storyteller perspective";
  CHECK(!cur_time_.IsDay && cur_time_.Count == 1)
      << "Minion info is only shown on night 1";
  const int p_index = PlayerIndex(player);
  Role shown_token = perspective_ == STORYTELLER
      ? st_shown_tokens_[p_index] : perspective_player_shown_token_;
  CHECK(IsMinionRole(shown_token))
      << "Player " << player
      << " needs to be shown a minion token in order to get minion info";
  // Minion info is always correct in TB:
  const auto& assigned_roles = night_roles_[0];
  const int demon_index = PlayerIndex(demon);
  model_.FixVariable(assigned_roles[demon_index][IMP], true);
  for (const string& minion_name : minions) {
    vector<BoolVar> minion_i;
    for (Role role : kMinionRoles) {
      if (role != shown_token) {  // they are a different minion
        minion_i.push_back(assigned_roles[PlayerIndex(minion_name)][role]);
      }
    }
    AddEqualitySum(minion_i, 1);
  }
}

void GameState::AddMinionInfo(const string& player,
                              const MinionInfo& minion_info) {
  const auto& minions = minion_info.minions();
  AddMinionInfo(player, minion_info.demon(),
                vector<string>(minions.begin(), minions.end()));
}

void GameState::AddDemonInfo(const string& player,
                             absl::Span<const string> minions,
                             absl::Span<const Role> bluffs) {
  auto* si_pb = log_.add_events()->mutable_storyteller_interaction();
  si_pb->set_player(player);
  DemonInfo* info_pb = si_pb->mutable_demon_info();
  info_pb->mutable_minions()->Assign(minions.begin(), minions.end());
  info_pb->mutable_bluffs()->Assign(bluffs.begin(), bluffs.end());
  BeforeEvent(Event::kStorytellerInteraction);
  CHECK_GE(num_players_, 7) << "Demon info unavailable for < 7 players";
  CHECK(perspective_ == STORYTELLER || perspective_ == PLAYER)
      << "Demon info can only be shown in player or storyteller perspective";
  CHECK(!cur_time_.IsDay && cur_time_.Count == 1)
      << "Demon info is only shown on night 1";
  const int p_index = PlayerIndex(player);
  Role shown_token = perspective_ == STORYTELLER
      ? st_shown_tokens_[p_index] : perspective_player_shown_token_;
  CHECK_EQ(shown_token, IMP)
      << "Player " << player
      << " needs to be shown the IMP token in order to get demon info";
  // Demon info is always correct in TB:
  const auto& assigned_roles = night_roles_[0];
  CHECK_EQ(minions.size(), num_minions_)
      << "Demon info should have " << num_minions_ << " minions";
  for (const string& minion_name : minions) {
    vector<BoolVar> minion_i;
    for (Role role : kMinionRoles) {
      minion_i.push_back(assigned_roles[PlayerIndex(minion_name)][role]);
    }
    AddEqualitySum(minion_i, 1);
  }
  CHECK_EQ(bluffs.size(), 3) << "Demon info should have 3 bluffs";
  for (Role bluff : bluffs) {
    CHECK(IsGoodRole(bluff))
        << "Expected demon bluffs good roles only, got " << Role_Name(bluff);
    model_.FixVariable(roles_in_play_[bluff], false);
  }
}

void GameState::AddDemonInfo(const string& player,
                             const DemonInfo& demon_info) {
  const auto& minions = demon_info.minions();
  vector<Role> bluffs;
  for (int bluff : demon_info.bluffs()) {
    bluffs.push_back(Role(bluff));
  }
  AddDemonInfo(player, vector<string>(minions.begin(), minions.end()), bluffs);
}

void GameState::AddRoleAction(const string& player,
                              const RoleAction& role_action) {
  switch (role_action.details_case()) {
    case RoleAction::kWasherwomanInfo:
      AddWasherwomanInfo(player, role_action.washerwoman_info());
      break;
    case RoleAction::kLibrarianInfo:
      AddLibrarianInfo(player, role_action.librarian_info());
      break;
    case RoleAction::kInvestigatorInfo:
      AddInvestigatorInfo(player, role_action.investigator_info());
      break;
    case RoleAction::kChefInfo:
      AddChefInfo(player, role_action.chef_info());
      break;
    case RoleAction::kEmpathInfo:
      AddEmpathInfo(player, role_action.empath_info());
      break;
    case RoleAction::kFortunetellerAction:
      AddFortuneTellerAction(player, role_action.fortuneteller_action());
      break;
    case RoleAction::kMonkAction:
      AddMonkAction(player, role_action.monk_action());
      break;
    case RoleAction::kButlerAction:
      AddButlerAction(player, role_action.butler_action());
      break;
    case RoleAction::kRavenkeeperAction:
      AddRavenkeeperAction(player, role_action.ravenkeeper_action());
      break;
    case RoleAction::kUndertakerInfo:
      AddUndertakerInfo(player, role_action.undertaker_info());
      break;
    case RoleAction::kSlayerAction:
      AddSlayerAction(player, role_action.slayer_action());
      break;
    case RoleAction::kPoisonerAction:
      AddPoisonerAction(player, role_action.poisoner_action());
      break;
    case RoleAction::kImpAction:
      AddImpAction(player, role_action.imp_action());
      break;
    case RoleAction::kSpyInfo:
      AddSpyInfo(player, role_action.spy_info());
      break;
    default:
      CHECK(false) << "Expected a valid role action details, got: "
                   << role_action.details_case();
  }
}

void GameState::AddLearningRoleInfoConstraints(
    const string& player_name, Role player_role, const string& ping1_name,
    const string& ping2_name, Role role) {
  const int player = PlayerIndex(player_name);
  const int ping1 = PlayerIndex(ping1_name);
  const int ping2 = PlayerIndex(ping2_name);
  CHECK_NE(ping1, ping2)
      << Role_Name(player_role) << " pings need to be different.";
  const string role_name = Role_Name(player_role);
  // If player is player_role ^ is not poisoned, then either:
  // * ping1 is either role or not poisoned Spy/Recluse
  // * ping2 is either role or not poisoned Spy/Recluse
  // Because this is night 1, not poisoned <-> not Poisoner picked.
  const Role false_trigger = IsGoodRole(role) ? SPY : RECLUSE;
  const BoolVar& is_player_role = night_roles_[0][player][player_role];
  const BoolVar& ping1_role = night_roles_[0][ping1][role];
  const BoolVar& ping2_role = night_roles_[0][ping2][role];
  const BoolVar& ping1_false = night_roles_[0][ping1][false_trigger];
  const BoolVar& ping2_false = night_roles_[0][ping2][false_trigger];
  const BoolVar& player_poisoned = poisoner_pick_[0][player];
  const BoolVar& ping1_poisoned = poisoner_pick_[0][ping1];
  const BoolVar& ping2_poisoned = poisoner_pick_[0][ping2];
  BoolVar ping1_healthy_false = CreateEquivalentVarAnd(
      {ping1_false, Not(ping1_poisoned)},
      absl::StrFormat("%s_ping1_%s_healthy_%s", role_name,
                      ping1_name, Role_Name(false_trigger)));
  BoolVar ping2_healthy_false = CreateEquivalentVarAnd(
      {ping2_false, Not(ping2_poisoned)},
      absl::StrFormat("%s_ping2_%s_healthy_%s", role_name,
                      ping1_name, Role_Name(false_trigger)));
  AddOr({Not(is_player_role), player_poisoned, ping1_role,
         ping2_role, ping1_healthy_false, ping2_healthy_false});
  deferred_constraints_[player] = false;
}

void GameState::AddWasherwomanInfo(const string& player, const string& ping1,
                                   const string& ping2, Role role) {
  auto* si_pb = log_.add_events()->mutable_storyteller_interaction();
  si_pb->set_player(player);
  LearnRoleInfo* info_pb =
      si_pb->mutable_role_action()->mutable_washerwoman_info();
  info_pb->set_ping1(ping1);
  info_pb->set_ping2(ping2);
  info_pb->set_role(role);
  ValidateRoleAction(player, WASHERWOMAN);
  CHECK(IsTownsfolkRole(role));
  BeforeEvent(Event::kStorytellerInteraction);
  // We defer compiling the info until public claim.
  deferred_constraints_[PlayerIndex(player)] = true;
}

void GameState::AddWasherwomanInfo(
    const string& player, const LearnRoleInfo& info) {
  AddWasherwomanInfo(player, info.ping1(), info.ping2(), info.role());
}

void GameState::AddLibrarianInfo(const string& player, const string& ping1,
                                 const string& ping2, Role role) {
  auto* si_pb = log_.add_events()->mutable_storyteller_interaction();
  si_pb->set_player(player);
  LearnRoleInfo* info_pb =
      si_pb->mutable_role_action()->mutable_librarian_info();
  info_pb->set_ping1(ping1);
  info_pb->set_ping2(ping2);
  info_pb->set_role(role);
  ValidateRoleAction(player, LIBRARIAN);
  CHECK(IsOutsiderRole(role));
  BeforeEvent(Event::kStorytellerInteraction);
  // We defer compiling the info until public claim.
  deferred_constraints_[PlayerIndex(player)] = true;
}

void GameState::AddLibrarianInfo(
    const string& player, const LearnRoleInfo& info) {
  AddLibrarianInfo(player, info.ping1(), info.ping2(), info.role());
}

void GameState::AddInvestigatorInfo(const string& player, const string& ping1,
                                    const string& ping2, Role role) {
  auto* si_pb = log_.add_events()->mutable_storyteller_interaction();
  si_pb->set_player(player);
  LearnRoleInfo* info_pb =
      si_pb->mutable_role_action()->mutable_investigator_info();
  info_pb->set_ping1(ping1);
  info_pb->set_ping2(ping2);
  info_pb->set_role(role);
  ValidateRoleAction(player, INVESTIGATOR);
  CHECK(IsMinionRole(role));
  BeforeEvent(Event::kStorytellerInteraction);
  // We defer compiling the info until public claim.
  deferred_constraints_[PlayerIndex(player)] = true;
}

void GameState::AddInvestigatorInfo(
    const string& player, const LearnRoleInfo& info) {
  AddInvestigatorInfo(player, info.ping1(), info.ping2(), info.role());
}

void GameState::AddChefInfo(const string& player, int chef_info) {
  auto* si_pb = log_.add_events()->mutable_storyteller_interaction();
  si_pb->set_player(player);
  si_pb->mutable_role_action()->set_chef_info(chef_info);
  BeforeEvent(Event::kStorytellerInteraction);
}

void GameState::AddEmpathInfo(const string& player, int empath_info) {
  auto* si_pb = log_.add_events()->mutable_storyteller_interaction();
  si_pb->set_player(player);
  si_pb->mutable_role_action()->set_empath_info(empath_info);
  ValidateRoleAction(player, EMPATH);
  // We don't check that empath_info is in [0,2], because in rare cases the
  // Storyteller technically could give a higher number to inform the Empath
  // that they are drunk or poisoned (if Good really needs help).
  CHECK_GE(empath_info, 0) << "Expected non-negative Empath info";
  BeforeEvent(Event::kStorytellerInteraction);
  // We defer compiling the info until public claim.
  deferred_constraints_[PlayerIndex(player)] = true;
}

void GameState::AddFortuneTellerAction(
    const string& player, const string& pick1, const string& pick2, bool yes) {
  auto* si_pb = log_.add_events()->mutable_storyteller_interaction();
  si_pb->set_player(player);
  FortuneTellerAction* action_pb =
      si_pb->mutable_role_action()->mutable_fortuneteller_action();
  action_pb->set_pick1(pick1);
  action_pb->set_pick2(pick2);
  action_pb->set_yes(yes);
  ValidateRoleAction(player, FORTUNE_TELLER);
  CHECK_NE(pick1, pick2)
      << "Fortune Teller needs to pick two different players";
  BeforeEvent(Event::kStorytellerInteraction);
  deferred_constraints_[PlayerIndex(player)] = true;
}

void GameState::AddFortuneTellerAction(
    const string& player, const FortuneTellerAction& action) {
  AddFortuneTellerAction(player, action.pick1(), action.pick2(), action.yes());
}

void GameState::AddMonkAction(const string& player, const string& monk_action) {
  auto* si_pb = log_.add_events()->mutable_storyteller_interaction();
  si_pb->set_player(player);
  si_pb->mutable_role_action()->set_monk_action(monk_action);
  ValidateRoleAction(player, MONK);
  CHECK_NE(player, monk_action) << "Monk cannot pick themselves";
  BeforeEvent(Event::kStorytellerInteraction);
  const int i = PlayerIndex(player), target = PlayerIndex(monk_action);
  if (perspective_ == STORYTELLER && st_player_roles_[i] == MONK) {
    st_monk_pick_ = target;
  }
  deferred_constraints_[i] = true;
}

void GameState::AddButlerAction(const string& player,
                                const string& butler_action) {
  auto* si_pb = log_.add_events()->mutable_storyteller_interaction();
  si_pb->set_player(player);
  si_pb->mutable_role_action()->set_butler_action(butler_action);
  ValidateRoleAction(player, BUTLER);
  CHECK_NE(player, butler_action) << "Butler cannot pick themselves";
  BeforeEvent(Event::kStorytellerInteraction);
  const int i = PlayerIndex(player), target = PlayerIndex(butler_action);
  if (perspective_ == STORYTELLER && st_player_roles_[i] == BUTLER) {
    st_butler_pick_ = target;
  }
  AddImplication(night_roles_.back()[i][BUTLER], butler_pick_.back()[target]);
}

void GameState::AddRavenkeeperAction(const string& player, const string& pick,
                                   Role role) {
  auto* si_pb = log_.add_events()->mutable_storyteller_interaction();
  si_pb->set_player(player);
  RavenkeeperAction* action_pb =
      si_pb->mutable_role_action()->mutable_ravenkeeper_action();
  action_pb->set_pick(pick);
  action_pb->set_role(role);
  ValidateRoleAction(player, RAVENKEEPER);
  CHECK_NE(role, ROLE_UNSPECIFIED) << "Ravenkeeper needs to learn a valid role";
  BeforeEvent(Event::kStorytellerInteraction);
  deferred_constraints_[PlayerIndex(player)] = true;
}

void GameState::AddRavenkeeperAction(
    const string& player, const RavenkeeperAction& action) {
  AddRavenkeeperAction(player, action.pick(), action.role());
}

void GameState::AddUndertakerInfo(const string& player, Role info) {
  auto* si_pb = log_.add_events()->mutable_storyteller_interaction();
  si_pb->set_player(player);
  si_pb->mutable_role_action()->set_undertaker_info(info);
  BeforeEvent(Event::kStorytellerInteraction);
  ValidateRoleAction(player, UNDERTAKER);
  CHECK_NE(execution_death_, kNoPlayer);  // Otherwise noone to exhume.
  CHECK_NE(info, ROLE_UNSPECIFIED) << "Expected valid Undertaker info";
  const int i = PlayerIndex(player);
  BoolVar poisoned = CreatePoisonedRoleVar(
      UNDERTAKER, cur_time_.Count - 1, true);
  const BoolVar& correct = night_roles_.back()[execution_death_][info];
  AddImplicationOr(night_roles_[0][i][UNDERTAKER], {poisoned, correct});
}

void GameState::AddSlayerAction(const string& player,
                                const string& slayer_action) {
  auto* si_pb = log_.add_events()->mutable_storyteller_interaction();
  si_pb->set_player(player);
  si_pb->mutable_role_action()->set_slayer_action(slayer_action);
  BeforeEvent(Event::kStorytellerInteraction);
  CHECK(cur_time_.IsDay) << "Slayer only allowed to shoot during the day";
  const int slayer = PlayerIndex(player);
  const int target = PlayerIndex(slayer_action);
  CHECK(!player_used_slayer_shot_[slayer])
      << player << " already used a Slayer shot";
  player_used_slayer_shot_[slayer] = true;
  slayer_shots_.push_back(
      {.Slayer = slayer, .Target = target});
  next_event_maybe_death_ = true;
}

void GameState::ValidateRoleAction(const string& player, Role role) {
  const string role_name = Role_Name(role);
  const int player_index = PlayerIndex(player);
  CHECK(!cur_time_.IsDay) << role_name << " actions only occur at night";
  CHECK(is_alive_[player_index]) << "Dead players don't get role actions";
  CHECK_NE(perspective_, OBSERVER)
      << absl::StrFormat("Observer cannot see %s actions", role_name);
  if (perspective_ == STORYTELLER) {
    const Role st_player_role = st_shown_tokens_[player_index];
    CHECK_EQ(st_player_role, role)
        << absl::StrFormat("%s needs to be the %s, got %s", player,
                           role_name, Role_Name(st_player_role));
  } else {  // Player perspective.
    CHECK_EQ(player_index, perspective_player_)
        << absl::StrFormat("Only the %s or Storyteller perspective can see "
                           "%s actions", role_name, role_name);
    CHECK_EQ(perspective_player_shown_token_, role)
        << absl::StrFormat("%s needs to be the %s, got %s", player,
                           role_name,
                           Role_Name(perspective_player_shown_token_));
  }
  CHECK(!night_action_used_[player_index]) << player << " already used ability";
  night_action_used_[player_index] = true;
}

void GameState::AddPoisonerAction(const string& player,
                                  const string& poisoner_action) {
  auto* si_pb = log_.add_events()->mutable_storyteller_interaction();
  si_pb->set_player(player);
  si_pb->mutable_role_action()->set_poisoner_action(poisoner_action);
  ValidateRoleAction(player, POISONER);
  BeforeEvent(Event::kStorytellerInteraction);
  const int target = PlayerIndex(poisoner_action);
  if (perspective_ == STORYTELLER) {
    st_poisoner_pick_ = target;
  }
  model_.FixVariable(poisoner_pick_.back()[target], true);
}

void GameState::AddImpAction(const string& player, const string& imp_action) {
  auto* si_pb = log_.add_events()->mutable_storyteller_interaction();
  si_pb->set_player(player);
  si_pb->mutable_role_action()->set_imp_action(imp_action);
  ValidateRoleAction(player, IMP);
  BeforeEvent(Event::kStorytellerInteraction);
  const int target = PlayerIndex(imp_action);
  CHECK_NE(perspective_, OBSERVER) << "Observer cannot see Imp actions";
  if (perspective_ == STORYTELLER) {
    st_imp_pick_ = target;
  }
  model_.FixVariable(imp_pick_.back()[target], true);
}

void GameState::AddSpyInfo(const string& player, const SpyInfo& spy_info) {
  auto* si_pb = log_.add_events()->mutable_storyteller_interaction();
  si_pb->set_player(player);
  *(si_pb->mutable_role_action()->mutable_spy_info()) = spy_info;
  BeforeEvent(Event::kStorytellerInteraction);
}

void GameState::AddVirginProcConstraints(bool proc) {
  // Virgin proc is equal to AND of:
  // * Nominee is alive
  // * Nominee is the Virgin
  // * Nominee is not poisoned
  // * Nominator is a townsfolk or non-poisoned Spy (the Spy is optional)
  const auto& nomination = nominations_.back();
  if (!proc) {
    if (!is_alive_[nomination.Nominee]) {
      return;
    }
  } else {
    CHECK(is_alive_[nomination.Nominee])
        << "Virgin " << players_[nomination.Nominee]
        << " needs to be alive to proc.";
  }
  const BoolVar& virgin = day_roles_.back()[nomination.Nominee][VIRGIN];
  BoolVar poisoned = CreatePoisonedRoleVar(VIRGIN, cur_time_.Count, true);
  BoolVar proc_townsfolk = model_.NewBoolVar().WithName(
      absl::StrFormat("%s_is_townsfolk", players_[nomination.Nominator]));
  vector<BoolVar> townsfolk_cases = CollectRolesForPlayer(
      day_roles_.back(), nomination.Nominator, kTownsfolkRoles, true);

  if (!proc) {
    AddEquivalenceSum(proc_townsfolk, townsfolk_cases);
    AddOr({Not(virgin), poisoned, Not(proc_townsfolk)});
    return;
  }
  // In case of a proc, we need to add the non-poisoned Spy option to the
  // townsfolk cases:
  const BoolVar& spy = day_roles_.back()[nomination.Nominator][SPY];
  BoolVar poisoned_spy = CreatePoisonedRoleVar(SPY, cur_time_.Count, true);
  BoolVar healthy_spy = CreateEquivalentVarAnd(
      {spy, Not(poisoned_spy)},
      absl::StrFormat("%s_is_healthy_spy", players_[nomination.Nominator]));
  townsfolk_cases.push_back(healthy_spy);
  AddEquivalenceSum(proc_townsfolk, townsfolk_cases);
  AddAnd({virgin, Not(poisoned), proc_townsfolk});
}

void GameState::AddGoodWonConstraints() {
  // Good can only win if either:
  // * Mayor win (no execution with 3 alive).
  // * The Imp died today (executed OR Slayer shot) AND
  //    * There are <5 alive players, OR
  //    * SW is not alive, OR
  //    * SW is poisoned
  // OR (silly case, but needs to be addressed):
  // * The Imp killed themselves at night, AND
  // * No-one could catch the starpass (no alive minion - Recluse is optional)
  const auto& day_roles = day_roles_.back();
  const string time = cur_time_.ToString();
  if (execution_death_ == kNoPlayer && slayer_death_ == kNoPlayer) {
    // Mayor win option:
    BoolVar mayor_win = model_.FalseVar();
    if (AlivePlayersClaiming(MAYOR).size() > 0 && num_alive_ == 3) {
      BoolVar alive = CreateAliveRoleVar(MAYOR, cur_time_);
      BoolVar poisoned = CreatePoisonedRoleVar(MAYOR, cur_time_.Count, true);
      mayor_win = CreateEquivalentVarAnd(
          {alive, Not(poisoned)}, absl::StrFormat("mayor_win_%s", time));
    }
    if (night_death_ == kNoPlayer) {
      model_.FixVariable(mayor_win, true);
      return;
    }
    // The silly suicide case:
    vector<BoolVar> imp_suicide_reqs({day_roles[night_death_][IMP]});
    for (int i = 0; i < num_players_; ++i) {
      if (is_alive_[i]) {
        // Otherwise they would catch the starpass.
        imp_suicide_reqs.push_back(Not(is_evil_[i]));
      }
    }
    BoolVar imp_suicide = CreateEquivalentVarAnd(
        imp_suicide_reqs, absl::StrFormat("imp_suicide_good_wins_%s", time));
    AddOr({mayor_win, imp_suicide});
    return;
  }
  int death = slayer_death_ != kNoPlayer ? slayer_death_ : execution_death_;
  model_.FixVariable(day_roles[death][IMP], true);
  if (num_alive_ >= 4) {
    // SW is not alive or is poisoned.
    const auto& poisoner_pick = poisoner_pick_.back();
    for (int i = 0; i < num_players_; ++i) {
      BoolVar day_sw_i = day_roles[i][SCARLET_WOMAN];
      if (is_alive_[i]) {
        AddImplication(day_sw_i, poisoner_pick[i]);
      }
    }
  }
}

void GameState::AddEvilWonConstraints() {
  // Evil wins only if either:
  // * Non-poisoned Saint was executed, OR
  // * 2 players are alive, one of them the Imp.
  if (execution_death_ != kNoPlayer) {
    AddAnd({day_roles_.back()[execution_death_][SAINT],
            Not(CreatePoisonedRoleVar(SAINT, cur_time_.Count, false))});
    return;
  }
  if (num_alive_ > 2) {
    AddContradiction("No execution and >=3 players alive, yet Evil wins");
    return;
  }
  AddEqualitySum(CollectAliveRoles(day_roles_.back(), {IMP}), 1);
}

BoolVar GameState::CreatePoisonerPickedRoleVar(
    Role role, int night, bool only_alive) {
  // Returns a new variable for whether a role was Poisoner picked during night.
  // Note this is not quite the same as poisoned, because Poisoner might have
  // since died (see CreatePoisonedAliveRoleVar for that amendment).
  const string name = absl::StrFormat(
      "poisoner_picked_%s_night_%d", Role_Name(role), night);
  const auto it = var_cache_.find(name);
  if (it != var_cache_.end()) {
    return it->second;
  }
  vector<BoolVar> picked_role_players;
  vector<int> players_to_check;  // Open strategy optimization.
  if (IsGoodRole(role)) {
    players_to_check = players_claiming_[role];
  } else {  // Might be anyone.
    for (int i = 0; i < num_players_; ++i) {
      players_to_check.push_back(i);
    }
  }
  for (int i : players_to_check) {
    if (!only_alive || is_alive_[i]) {
      const BoolVar& role_i = night_roles_[night - 1][i][role];
      const BoolVar& picked_i = poisoner_pick_[night - 1][i];
      BoolVar picked_role_i = CreateEquivalentVarAnd(
          {role_i, picked_i},
          absl::StrFormat("poisoner_picked_%s_%s_night_%d", Role_Name(role),
                          players_[i], night));
      picked_role_players.push_back(picked_role_i);
    }
  }
  BoolVar role_picked = model_.NewBoolVar().WithName(name);
  if (picked_role_players.size() == 0) {
    model_.FixVariable(role_picked, false);
  } else {
    AddEquivalenceSum(role_picked, picked_role_players);
  }
  var_cache_[name] = role_picked;
  return role_picked;
}

BoolVar GameState::CreatePoisonedRoleVar(Role role, int day, bool only_alive) {
  // A role is poisoned during the day (and remainder of the night, too) if:
  // * The role is in play, and:
  //   * Poisoner picked them last night
  //   * Poisoner survived the night
  BoolVar picked = CreatePoisonerPickedRoleVar(role, day, only_alive);
  if (night_death_ == kNoPlayer) {
    return picked;
  }
  BoolVar poisoned = CreateEquivalentVarAnd(
      {picked, Not(day_roles_.back()[night_death_][POISONER])},
      absl::StrFormat("poisoned_%s_day_%d", Role_Name(role), day));
  return poisoned;
}

void GameState::AddNoDeathConstraints() {
  // This happens after new day or Slayer shot when there was no death.
  // In case of Slayer shot, the shot failed:
  if (slayer_shots_.size() > 0) {
    // This can be either:
    // * Slayer is not actually the Slayer
    // * Target is not the Imp
    // * Slayer was poisoned
    const auto& shot = slayer_shots_.back();
    AddOr({Not(day_roles_.back()[shot.Slayer][SLAYER]),
           Not(day_roles_.back()[shot.Target][IMP]),
           CreatePoisonedRoleVar(SLAYER, cur_time_.Count, true)});
    return;
  }
  // In case of new day, the Imp did not kill at night. This could be either:
  // * Imp chose to sink a kill on a dead player (Imp pick is mandatory)
  // * Imp was poisoned
  // * Target was an alive and healthy Soldier
  // * Imp picked an alive-healthy-Monk protected target
  // * Mayor bounce to no kill. This means all of:
  //    * Imp targeted Mayor
  //    * Mayor was alive and healthy
  //    * Mayor was not alive-healthy-Monk protected (otherwise no bounce)
  //    * Mayor bounce was to another player j, s.t. either:
  //      * j is dead
  //      * j is a healthy soldier
  //      * j is protected by an alive and healthy Monk
  const bool possible_alive_monk = AlivePlayersClaiming(MONK).size() > 0;
  vector<int> alive_mayor_options = AlivePlayersClaiming(MAYOR);
  vector<BoolVar> cases;
  const string time = absl::StrFormat("night_%d", cur_time_.Count);
  const auto& imp_pick = imp_pick_.back();
  for (int i = 0; i < num_players_; ++i) {
    if (!is_alive_[i]) {
      cases.push_back(imp_pick[i]);  // Sink kill.
    }
  }
  BoolVar imp_poisoned = CreatePoisonerPickedRoleVar(
      IMP, cur_time_.Count, true);
  cases.push_back(imp_poisoned);
  // Soldier protection: (optimized for open strategy):
  const auto& poisoner_pick = poisoner_pick_.back();
  for (int i : AlivePlayersClaiming(SOLDIER)) {
    cases.push_back(CreateEquivalentVarAnd(
      {imp_pick[i], night_roles_[0][i][SOLDIER], Not(poisoner_pick[i])},
      absl::StrFormat("imp_picks_healthy_soldier_%s_%s", players_[i], time)));
  }
  // Monk protection:
  if (possible_alive_monk) {
    for (int i = 0; i < num_players_; ++i) {
      if (is_alive_[i]) {
        cases.push_back(CreateEquivalentVarAnd(
            {healthy_monk_protected_.back()[i], imp_pick[i]},
            absl::StrFormat("imp_pick_healthy_monk_protected_%s_%s",
                            players_[i], time)));
      }
    }
  }
  // Mayor bounce to no kill.
  vector<BoolVar> bounce_cases(num_players_);
  if (alive_mayor_options.size() > 0) {
    for (int j = 0; j < num_players_; ++j) {
      if (!is_alive_[j]) {
        continue;
      }
      // * j is a healthy soldier, OR
      // * j is protected by an alive and healthy Monk
      BoolVar healthy_soldier = CreateEquivalentVarAnd(
          {night_roles_[0][j][SOLDIER], Not(poisoner_pick[j])},
          absl::StrFormat("healthy_soldier_%s_%s", players_[j], time));
      bounce_cases[j] = (!possible_alive_monk ?
          healthy_soldier :
          CreateEquivalentVarOr(
            {healthy_soldier, healthy_monk_protected_.back()[j]},
            absl::StrFormat("mayor_bounce_to_%s_%s", players_[j], time)));
    }
  }
  for (int i : alive_mayor_options) {
    vector<BoolVar> valid_bounce_cases;
    for (int j = 0; j < num_players_; ++j) {
      if (is_alive_[j] && j != i) {
        valid_bounce_cases.push_back(bounce_cases[j]);
      }
    }
    string name = absl::StrFormat(
        "mayor_%s_bounce_no_kill_valid_%s", players_[i], time);
    BoolVar mayor_bounce_valid = (num_alive_ < num_players_ ?
        model_.TrueVar().WithName("1") :
        CreateEquivalentVarOr(valid_bounce_cases, name));
    vector<BoolVar> reqs({imp_pick[i], night_roles_[0][i][MAYOR],
                          Not(poisoner_pick[i]), mayor_bounce_valid});
    if (possible_alive_monk) {
      reqs.push_back(Not(healthy_monk_protected_.back()[i]));
    }
    name = absl::StrFormat("mayor_%s_bounce_no_kill_%s", players_[i], time);
    cases.push_back(CreateEquivalentVarAnd(reqs, name));
  }
  AddOr(cases);
}

void GameState::AddGameNotOverConstraints() {
  // This is during the day, after a death. It means !(GOOD won) && !(EVIL won).
  //
  // Good wins iff there is no alive Imp and the SW cannot become an Imp.
  //
  // Evil wins iff either:
  // * A non-poisoned Saint was executed, OR
  // * 2 players are alive, one of them the Imp.
  if (num_alive_ <= 2) {
    AddContradiction(absl::StrFormat(
        "%d players alive on %s, yet game is not over",
        num_alive_, cur_time_.ToString()));
    return;
  }
  const auto& roles = day_roles_.back();
  vector<BoolVar> alive_imp = CollectAliveRoles(roles, {IMP});
  if (num_alive_ >=4) {
    BoolVar alive_sw = CreateAliveRoleVar(SCARLET_WOMAN, cur_time_);
    BoolVar poisoned_sw = CreatePoisonedRoleVar(
        SCARLET_WOMAN, cur_time_.Count, true);
    // Scarlet Woman can become Imp at night.
    alive_imp.push_back(CreateEquivalentVarAnd(
        {Not(poisoned_sw), alive_sw},
        absl::StrFormat("healthy_scarlet_woman_%s", cur_time_.ToString())));
  }
  AddOr(alive_imp);
  if (execution_death_ != kNoPlayer) {
    AddOr({Not(roles[execution_death_][SAINT]),
           CreatePoisonedRoleVar(SAINT, cur_time_.Count, false)});
  }
}

SolverRequest FromCurrentRoles(
    const unordered_map<string, Role>& player_roles) {
  SolverRequest request;
  for (const auto& it : player_roles) {
    auto *pr = request.mutable_current_assumptions()->add_roles();
    pr->set_player(it.first);
    pr->set_role(it.second);
  }
  return request;
}

SolverRequest FromStartingRoles(
    const unordered_map<string, Role>& player_roles) {
  SolverRequest request;
  for (const auto& it : player_roles) {
    auto *pr = request.mutable_starting_assumptions()->add_roles();
    pr->set_player(it.first);
    pr->set_role(it.second);
  }
  return request;
}

SolverRequest FromNotInPlayRoles(absl::Span<const Role> roles) {
  SolverRequest request;
  for (Role role : roles) {
    request.mutable_starting_assumptions()->add_roles_not_in_play(role);
  }
  return request;
}

void GameState::WriteSatSolutionToFile(const CpSolverResponse response,
                                       CpModelBuilder* model,
                                       const string& filename) const {
  ofstream f;
  f.open(filename);
    for (int i = 0; i < response.solution_size(); ++i) {
      operations_research::sat::IntVar v = model->GetIntVarFromProtoIndex(i);
      f << v.Name() << ": " << SolutionIntegerValue(response, v) << "\n";
    }
  f.close();
}

void GameState::WriteModelToFile(const string& filename) const {
  WriteProtoToFile(filename, model_.Build());
}

void GameState::WriteModelVariablesToFile(const string& filename) const {
  const auto& model_pb = model_.Build();
  ofstream f;
  f.open(filename);
    for (int i = 0; i < model_pb.variables_size(); ++i) {
      f << i << ": " << VarDebugString(model_pb, i) << "\n";
    }
  f.close();
}

vector<BoolVar> GameState::CollectAssumptionLiterals(
    const SolverRequest& request) const {
  vector<BoolVar> assumption_literals;
  const auto& current_roles =
    (cur_time_.IsDay ? day_roles_ : night_roles_).back();
  const auto& current_assumptions = request.current_assumptions();
  for (const auto& pr : current_assumptions.roles()) {
    const int player = PlayerIndex(pr.player());
    const auto& v = current_roles[player][pr.role()];
    assumption_literals.push_back(pr.is_not() ? Not(v) : v);
  }
  // TODO(olaola): support roles_in_play for current.
  const auto& starting_roles = night_roles_[0];
  const auto& starting_assumptions = request.starting_assumptions();
  for (const auto& pr : starting_assumptions.roles()) {
    const int player = PlayerIndex(pr.player());
    const auto& v = starting_roles[player][pr.role()];
    assumption_literals.push_back(pr.is_not() ? Not(v) : v);
  }
  for (int role : starting_assumptions.roles_in_play()) {
    assumption_literals.push_back(roles_in_play_[role]);
  }
  for (int role : starting_assumptions.roles_not_in_play()) {
    assumption_literals.push_back(Not(roles_in_play_[role]));
  }
  return assumption_literals;
}

// This function will be very slow in low-info game states. Consider adding
// more assumptions for these cases.
SolverResponse GameState::SolveGame(const SolverRequest& request) const {
  for (int i = 0; i < num_players_; ++i) {
    CHECK(!deferred_constraints_[i])
        << "Some night roles' actions had their constraint evaluation deferred "
        << "until public claim. Please call SolveGame after all public claims.";
  }
  SolverResponse result;
  CpModelBuilder model(model_);  // Making a copy to add assumptions repeatedly.
  vector<BoolVar> assumption_literals = CollectAssumptionLiterals(request);
  model.AddAssumptions(assumption_literals);
  CpSolverResponse response;
  const auto& current_roles =
    (cur_time_.IsDay ? day_roles_ : night_roles_).back();
  while (true) {
    response = Solve(model.Build());
    CHECK(response.status() != CpSolverStatus::MODEL_INVALID);
    CHECK(response.status() != CpSolverStatus::UNKNOWN);
    if (response.status() == CpSolverStatus::INFEASIBLE) {
      break;
    }
    // Translate the response back to role assignment.
    auto world = result.add_worlds();
    auto* roles = world->mutable_roles();
    vector<BoolVar> current;
    for (int i = 0; i < num_players_; ++i) {
      for (Role role : kAllRoles) {
        if (SolutionBooleanValue(response, current_roles[i][role])) {
          current.push_back(current_roles[i][role]);
          const string player = players_[i];
          CHECK(roles->find(player) == roles->end())
              << "Double role assignment for player " << player;
          (*roles)[player] = role;
        }
      }
    }
    CHECK_EQ(roles->size(), num_players_) << "Not all players assigned roles.";
    if (!request.output_sat_model_solutions_dir().empty()) {
      const string filename = absl::StrFormat(
          "%s/sat_solution_%d", request.output_sat_model_solutions_dir(),
          result.worlds_size());
      WriteSatSolutionToFile(response, &model, filename);
    }
    if (request.stop_after_first_solution()) {
      break;
    }
    // Limit further solutions to different role assignments:
    model.AddBoolOr(Not(current));  // At least one literal is different.
  }
  return result;
}
}  // namespace botc
