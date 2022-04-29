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
#include <iostream>
#include <fstream>

#include "ortools/sat/cp_model.pb.h"
#include "ortools/sat/cp_model_solver.h"
#include "ortools/sat/model.h"
#include "ortools/sat/sat_parameters.pb.h"

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

Setup SetupProto(absl::Span<const string> players) {
  Setup setup;
  for (const string& player : players) {
    setup.add_players(player);
  }
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

bool IsMinionRole(Role role) {
  for (Role r : kMinionRoles) {
    if (r == role) {
      return true;
    }
  }
  return false;
}

bool IsGoodRole(Role role) {
  for (Role r : kGoodRoles) {
    if (r == role) {
      return true;
    }
  }
  return false;
}
}  // namespace

namespace internal {
string Time::ToString() const {
  return absl::StrFormat("%s_%d", IsDay ? "day" : "night", Count);
}
}  // namespace internal

// TODO(olaola): replace CHECKs with absl::Status everywhere.
// TODO(olaola): fix the day poisoned bug (alive poisoner should exist).
// TODO(olaola): validate day 1 hard-claims (because we need them).

MinionInfo NewMinionInfo(const string& demon) {
  MinionInfo mi;
  mi.set_demon(demon);
  return mi;
}

DemonInfo NewDemonInfo(absl::Span<const string> minions,
                       absl::Span<const Role> bluffs) {
  DemonInfo di;
  for (const auto& minion : minions) {
    di.add_minions(minion);
  }
  for (Role bluff : bluffs) {
    di.add_bluffs(bluff);
  }
  return di;
}

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

int GameState::PlayerIndex(const string& name) const {
  const auto& it = player_index_.find(name);
  CHECK(it != player_index_.end()) << "Invalid player name: " << name;
  return it->second;
}

vector<string> GameState::ClaimingRole(Role role) {
  vector<string> result;
  for (int i : players_claiming_[role]) {
    result.push_back(players_[i]);
  }
  return result;
}

GameState::GameState(Perspective perspective, const Setup& setup)
    : perspective_(perspective), num_players_(setup.players_size()),
      is_alive_(num_players_, true), num_alive_(num_players_),
      player_used_slayer_shot_(num_players_),
      player_has_been_nominated_(num_players_),
      num_votes_(0), on_the_block_(kNoPlayer), execution_(kNoPlayer),
      execution_death_(kNoPlayer), slayer_death_(kNoPlayer),
      night_death_(kNoPlayer), next_event_maybe_victory_(false),
      next_event_maybe_death_(false), next_event_maybe_execution_(false),
      victory_(TEAM_UNSPECIFIED), claim_of_player_(num_players_),
      players_claiming_(Role_ARRAYSIZE), perspective_player_(kNoPlayer),
      perspective_player_shown_token_(ROLE_UNSPECIFIED),
      st_player_roles_(num_players_), st_shown_tokens_(num_players_),
      st_red_herring_(kNoPlayer), st_poisoner_pick_(kNoPlayer),
      st_imp_pick_(kNoPlayer), st_monk_pick_(kNoPlayer),
      st_butler_pick_(kNoPlayer) {
  CHECK_NE(perspective_, PERSPECTIVE_UNSPECIFIED)
      << "Need to specify perspective";
  CHECK_GE(num_players_, 5);
  CHECK_LE(num_players_, 15);
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
    model_.AddImplication(v, Not(is_evil_[i]))
        .WithName(absl::StrFormat("%s implies %s is good", name, players_[i]));
  }
  const BoolVar& ft_in_play = roles_in_play_[FORTUNE_TELLER];
  // If a Fortune Teller is in play, there is exactly one red herring.
  model_.AddEquality(LinearExpr::Sum(red_herring_), 1)
      .OnlyEnforceIf(ft_in_play)
      .WithName("ft_in_play -> 1 red herring");
  // If a Fortune Teller is not in play, there is no red herring.
  model_.AddEquality(LinearExpr::Sum(red_herring_), 0)
      .OnlyEnforceIf(Not(ft_in_play))
      .WithName("!ft_in_play -> no red herring");
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
    model_.AddEquality(LinearExpr::Sum(player_is_role), var)
        .WithName(absl::StrFormat("%s definition", name));
  }
}

void GameState::InitNightRoleVars() {
  if (cur_time_.Count == 1) {
    return;  // We initialize night 1 role variables during setup.
  }
  vector<vector<BoolVar>> night_roles(num_players_);
  for (int i = 0; i < num_players_; ++i) {
    night_roles[i].push_back(model_.FalseVar());  // dummy variable
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
    model_.AddEquality(LinearExpr::Sum(minions), num_minions_)
        .WithName(absl::StrFormat("Exactly %d minions", num_minions_));
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
    if (players_claiming_[MONK].size() > 0) {
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
    model_.AddExactlyOne(player_roles[i]).WithName(name);
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
  vector<BoolVar> alive_imp = CollectAliveRoles(player_roles, {IMP});
  const string name = absl::StrFormat("Exactly 1 alive Imp on %s", time);
  model_.AddExactlyOne(alive_imp).WithName(name);
}

void GameState::AddBaronConstraints() {
  const BoolVar& baron_in_play = roles_in_play_[BARON];
  vector<BoolVar> outsiders = CollectRoles(night_roles_[0], kOutsiderRoles);
  model_.AddEquality(LinearExpr::Sum(outsiders), num_outsiders_)
      .OnlyEnforceIf(Not(baron_in_play))
      .WithName(
        absl::StrFormat("!baron_in_play -> %d outsiders", num_outsiders_));
  model_.AddEquality(LinearExpr::Sum(outsiders), num_outsiders_ + 2)
      .OnlyEnforceIf(baron_in_play)
      .WithName(
        absl::StrFormat("baron_in_play -> %d outsiders", num_outsiders_ + 2));
  int num_townsfolk = kNumTownsfolk[num_players_ - 5];
  vector<BoolVar> townsfolk = CollectRoles(night_roles_[0], kTownsfolkRoles);;
  model_.AddEquality(LinearExpr::Sum(townsfolk), num_townsfolk)
      .OnlyEnforceIf(Not(baron_in_play))
      .WithName(
        absl::StrFormat("!baron_in_play -> %d townsfolk", num_townsfolk));
  model_.AddEquality(LinearExpr::Sum(townsfolk), num_townsfolk - 2)
      .OnlyEnforceIf(baron_in_play)
      .WithName(
        absl::StrFormat("baron_in_play -> %d townsfolk", num_townsfolk - 2));
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
    model_.AddExactlyOne(shown_token)
          .WithName(absl::StrFormat("Player %s shown exactly one token",
                                    players_[i]));
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
      model_.AddExactlyOne(shown_role).WithName("One player is shown IMP");
    }
  }
  const auto& assigned_roles = night_roles_[0];
  for (int i = 0; i < num_players_; ++i) {
    // Being shown a townsfolk role means you either are that role, or DRUNK.
    for (Role role : kTownsfolkRoles) {
      model_.AddBoolOr({assigned_roles[i][role], assigned_roles[i][DRUNK]})
            .OnlyEnforceIf(shown_token_[i][role])
            .WithName(absl::StrFormat("%s -> %s V %s",
                                      shown_token_[i][role].Name(),
                                      assigned_roles[i][role].Name(),
                                      assigned_roles[i][DRUNK].Name()));
    }
    // Being shown any other role means you are that role.
    for (Role role : kNonTownsfolkRoles) {
      model_.AddImplication(shown_token_[i][role], assigned_roles[i][role])
            .WithName(absl::StrFormat("%s -> %s",
                                      shown_token_[i][role].Name(),
                                      assigned_roles[i][role].Name()));
    }
  }
}

void GameState::InitIsEvilVars() {
  // A Player is Evil iff they have an Evil role on Night 1 (in TB).
  for (int i = 0; i < num_players_; ++i) {
    BoolVar is_evil = model_.NewBoolVar()
        .WithName(absl::StrFormat("evil_%s", players_[i]));
    is_evil_.push_back(is_evil);
    vector<BoolVar> evil_roles;
    for (int role : kEvilRoles) {
      evil_roles.push_back(night_roles_[0][i][role]);
    }
    model_.AddEquality(LinearExpr::Sum(evil_roles), is_evil)
        .WithName(absl::StrFormat("evil_%s definition", players_[i]));
  }
}

void GameState::AddNoStorytellerAnnouncement() {
  BeforeEvent(Event::DETAILS_NOT_SET);
}

void GameState::BeforeEvent(Event::DetailsCase event_type) {
  CHECK_EQ(victory_, TEAM_UNSPECIFIED) << "No events allowed after victory";
  if (event_type == Event::kClaim) {
    CHECK(cur_time_.IsDay) << "Claims can only be made during the day";
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
  on_the_block_ = execution_ = execution_death_ = slayer_death_ = night_death_ =
      kNoPlayer;
}

void GameState::AddNight(int count) {
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
  const int kills = cur_time_.Count != 0;
  model_.AddEquality(LinearExpr::Sum(imp_picks), kills)
        .WithName(absl::StrFormat("%d imp picks %s", kills, time));
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
  // No alive monk means no monk picks (and monk goes before the Imp):
  model_.AddEquality(LinearExpr::Sum(monk_picks), 0)
        .OnlyEnforceIf(Not(alive_monk))
        .WithName(absl::StrFormat("!%s -> 0 monk picks", alive_monk.Name()));
  // An alive monk can pick exactly one picks per night:
  model_.AddEquality(LinearExpr::Sum(monk_picks), 1)
        .OnlyEnforceIf(alive_monk)
        .WithName(absl::StrFormat("%s -> 1 monk picks", alive_monk.Name()));
  // A monk cannot pick themselves:
  for (int i = 0; i < num_players_; ++i) {
    const BoolVar& monk_i = night_roles_[0][i][MONK];
    model_.AddImplication(monk_picks[i], Not(monk_i))
          .WithName(absl::StrFormat("%s -> !%s", monk_picks[i].Name(),
                                    monk_i.Name()));
  }
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
  // No alive poisoner means no poisoner picks:
  model_.AddEquality(LinearExpr::Sum(poisoner_picks), 0)
        .OnlyEnforceIf(Not(alive_poisoner))
        .WithName(absl::StrFormat("!%s -> 0 poisoner picks",
                                  alive_poisoner.Name()));
  // An alive Poisoner can pick exactly one picks per night:
  model_.AddEquality(LinearExpr::Sum(poisoner_picks), 1)
        .OnlyEnforceIf(alive_poisoner)
        .WithName(absl::StrFormat("%s -> 1 poisoner picks",
                                  alive_poisoner.Name()));
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
  // No alive butler means no Butler picks:
  model_.AddEquality(LinearExpr::Sum(butler_picks), 0)
        .OnlyEnforceIf(Not(alive_butler))
        .WithName(absl::StrFormat("!%s -> 0 butler picks",
                                  alive_butler.Name()));
  // An alive Butler can pick exactly one picks per night:
  model_.AddEquality(LinearExpr::Sum(butler_picks), 1)
        .OnlyEnforceIf(alive_butler)
        .WithName(absl::StrFormat("%s -> 1 butler picks",
                                  alive_butler.Name()));
  // A Butler cannot pick themselves:
  for (int i = 0; i < num_players_; ++i) {
    const BoolVar& butler_i = night_roles_[0][i][BUTLER];
    model_.AddImplication(butler_picks[i], Not(butler_i))
          .WithName(absl::StrFormat("%s -> !%s", butler_picks[i].Name(),
                                    butler_i.Name()));
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
  BoolVar sw_proc = model_.NewBoolVar().WithName(
      absl::StrFormat("sw_proc_%s", cur_time_.ToString()));
  model_.AddBoolAnd({imp_died, sw_alive, Not(sw_poisoned)})
    .OnlyEnforceIf(sw_proc)
    .WithName(absl::StrFormat("%s -> %s ^ %s ^ !%s", sw_proc.Name(),
                              imp_died.Name(), sw_alive.Name(),
                              sw_poisoned.Name()));
  model_.AddBoolOr({Not(imp_died), Not(sw_alive), sw_poisoned})
    .OnlyEnforceIf(Not(sw_proc))
    .WithName(absl::StrFormat("!%s -> !%s V %s V %s", sw_proc.Name(),
                              imp_died.Name(), sw_alive.Name(),
                              sw_poisoned.Name()));
  for (int i = 0; i < num_players_; ++i) {
    BoolVar night_imp_i = night_roles[i][IMP];
    BoolVar night_sw_i = night_roles[i][SCARLET_WOMAN];
    BoolVar day_imp_i = day_roles[i][IMP];
    BoolVar day_sw_i = day_roles[i][SCARLET_WOMAN];
    // The Imp remains an Imp, even dead.
    model_.AddImplication(day_imp_i, night_imp_i)
          .WithName(absl::StrFormat("%s -> %s", day_imp_i.Name(),
                                    night_imp_i.Name()));
    model_.AddImplication(day_sw_i, night_imp_i)
          .OnlyEnforceIf(sw_proc)
          .WithName(absl::StrFormat("%s -> (%s -> %s)", sw_proc.Name(),
                                    day_sw_i.Name(), night_imp_i.Name()));
    model_.AddImplication(day_sw_i, night_sw_i)
          .OnlyEnforceIf(Not(sw_proc))
          .WithName(absl::StrFormat("!%s -> (%s -> %s)", sw_proc.Name(),
                                    day_sw_i.Name(), night_sw_i.Name()));
    model_.AddBoolAnd({Not(day_sw_i), sw_proc})
          .OnlyEnforceIf(night_sw_i)
          .WithName(absl::StrFormat("%s -> %s ^ !%s", night_sw_i.Name(),
                                    day_sw_i.Name(), sw_proc.Name()));
    model_.AddBoolOr({day_imp_i, sw_proc})
          .OnlyEnforceIf(night_imp_i)
          .WithName(absl::StrFormat("%s -> %s V %s", night_imp_i.Name(),
                                    day_imp_i.Name(), sw_proc.Name()));
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
      const BoolVar& imp_i = night_roles[i][IMP];
      const BoolVar& poisoned_i = poisoner_pick_.back()[i];
      const BoolVar& imp_pick_i = imp_pick_.back()[i];
      BoolVar starpass_i = model_.NewBoolVar().WithName(
          absl::StrFormat("%s_starpass_%s", players_[i], night_name));
      player_starpass.push_back(starpass_i);
      string name = absl::StrFormat(
          "!%s -> !%s V %s V !%s", starpass_i.Name(), imp_i.Name(),
          poisoned_i.Name(), imp_pick_i.Name());
      model_.AddBoolOr({Not(imp_i), poisoned_i, Not(imp_pick_i)})
            .OnlyEnforceIf(Not(starpass_i))
            .WithName(name);
      name = absl::StrFormat(
          "%s -> %s ^ !%s ^ %s", starpass_i.Name(), imp_i.Name(),
          poisoned_i.Name(), imp_pick_i.Name());
      model_.AddBoolAnd({imp_i, Not(poisoned_i), imp_pick_i})
            .OnlyEnforceIf(starpass_i)
            .WithName(name);
    }
  }
  BoolVar starpass = model_.NewBoolVar().WithName(
      absl::StrFormat("starpass_triggers_%s", night_name));
  model_.AddEquality(LinearExpr::Sum(player_starpass), starpass)
      .WithName(absl::StrFormat("%s <-> Exactly one alive player starpasses.",
                                starpass.Name()));

  vector<BoolVar> player_catch;
  vector<BoolVar> eligible;
  BoolVar nobody_eligible = model_.NewBoolVar().WithName(
      absl::StrFormat("nobody_eligible_for_starpass_%s", night_name));
  for (int i = 0; i < num_players_; ++i) {
    // The Imp remains an Imp, dead or alive.
    const BoolVar& day_imp_i = day_roles[i][IMP];
    const BoolVar& night_imp_i = night_roles[i][IMP];
    model_.AddImplication(night_imp_i, day_imp_i);
    if (!is_alive_[i]) {
      // Dead players can't catch a starpass.
      PropagateRolesForPlayer(i, night_roles, day_roles, kMinionRoles);
      PropagateRolesForPlayer(i, night_roles, day_roles, {RECLUSE});
    } else {
      BoolVar catch_i = model_.NewBoolVar().WithName(
          absl::StrFormat("%s_catches_starpass_%s", players_[i], night_name));
      player_catch.push_back(catch_i);
      // catch_i <-> !night_imp_i ^ day_imp_i
      model_.AddBoolAnd({Not(night_imp_i), day_imp_i})
          .OnlyEnforceIf(catch_i)
          .WithName(absl::StrFormat("%s -> !%s ^ %s", catch_i.Name(),
                                    night_imp_i.Name(), day_imp_i.Name()));
      model_.AddBoolOr({night_imp_i, Not(day_imp_i)})
          .OnlyEnforceIf(Not(catch_i))
          .WithName(absl::StrFormat("!%s -> %s V !%s", catch_i.Name(),
                                    night_imp_i.Name(), day_imp_i.Name()));
      const BoolVar& recluse_i = night_roles[i][RECLUSE];
      const BoolVar& poisoned_i = poisoner_pick_.back()[i];
      BoolVar healthy_recluse_i = model_.NewBoolVar().WithName(
          absl::StrFormat("healthy_recluse_%s_%s", players_[i], night_name));
      model_.AddBoolAnd({recluse_i, Not(poisoned_i)})
          .OnlyEnforceIf(healthy_recluse_i)
          .WithName(absl::StrFormat("%s -> %s ^ !%s", healthy_recluse_i.Name(),
                                    recluse_i.Name(), poisoned_i.Name()));
      model_.AddBoolOr({Not(recluse_i), poisoned_i})
          .OnlyEnforceIf(Not(healthy_recluse_i))
          .WithName(absl::StrFormat("!%s -> !%s V %s", healthy_recluse_i.Name(),
                                    recluse_i.Name(), poisoned_i.Name()));
      vector<BoolVar> eligible_i;
      // eligible_i <-> i is minion or healthy Recluse
      for (Role role : {POISONER, SPY, SCARLET_WOMAN, BARON, RECLUSE}) {
        const BoolVar& night_role_i = night_roles[i][role];
        const BoolVar& day_role_i = day_roles[i][role];
        const BoolVar& e = role == RECLUSE ? healthy_recluse_i : night_role_i;
        eligible_i.push_back(e);
        eligible.push_back(e);
        model_.AddImplication(e, Not(nobody_eligible)).WithName(
            absl::StrFormat("%s -> !%s", e.Name(), nobody_eligible.Name()));
        model_.AddEquality(night_role_i, day_role_i)
              .OnlyEnforceIf(Not(catch_i))
              .WithName(absl::StrFormat(
                  "!%s -> %s = %s", catch_i.Name(), night_role_i.Name(),
                  day_role_i.Name()));
      }
      // catch_i -> exactly 1 of eligible_i.
      model_.AddEquality(LinearExpr::Sum(eligible_i), 1)
            .OnlyEnforceIf(catch_i)
            .WithName(absl::StrFormat("%s -> %s is exactly 1 of eligible",
                                      catch_i.Name(), players_[i]));
    }
  }
  model_.AddEquality(LinearExpr::Sum(eligible), 0)
        .OnlyEnforceIf(nobody_eligible)
        .WithName(absl::StrFormat("%s -> nobody is eligible",
                                  nobody_eligible.Name()));
  // !starpass -> exactly zero catch
  model_.AddEquality(LinearExpr::Sum(player_catch), 0)
        .OnlyEnforceIf(Not(starpass))
        .WithName(absl::StrFormat("!%s -> no catch", starpass.Name()));
  // starpass -> exactly one catches OR nobody eligible
  player_catch.push_back(nobody_eligible);
  model_.AddEquality(LinearExpr::Sum(player_catch), 1)
        .OnlyEnforceIf(starpass)
        .WithName(absl::StrFormat(
            "%s -> 1 catch or noone eligible", starpass.Name()));
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
      if (IsGoodRole(role) && (cur_time_.IsDay || cur_time_.Count > 1)) {
        const auto& pp = players_claiming_[role];
        if (std::find(pp.begin(), pp.end(), player) == pp.end()) {
          continue;
        }
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
  vector<BoolVar> alive_role_players = CollectAliveRoles(roles[count], {role});
  BoolVar alive_role = model_.NewBoolVar().WithName(
      absl::StrFormat("alive_%s_%s", Role_Name(role), time.ToString()));
  model_.AddEquality(LinearExpr::Sum(alive_role_players), alive_role)
        .WithName(absl::StrFormat("%s definition", alive_role.Name()));
  return alive_role;
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
    const auto& recluses = players_claiming_[RECLUSE];
    if (std::find(recluses.begin(), recluses.end(), target) != recluses.end()) {
      const BoolVar& recluse = day_roles_.back()[target][RECLUSE];
      BoolVar poisoned_recluse = CreatePoisonedRoleVar(
          RECLUSE, cur_time_.Count, true);
      BoolVar healthy_recluse = model_.NewBoolVar().WithName(
          absl::StrFormat("%s_healthy_recluse", players_[target]));
      model_.AddBoolAnd({recluse, Not(poisoned_recluse)})
            .OnlyEnforceIf(healthy_recluse)
            .WithName(absl::StrFormat("%s -> %s ^ %s", healthy_recluse.Name(),
                                      recluse.Name(), poisoned_recluse.Name()));
      model_.AddBoolOr({Not(recluse), poisoned_recluse})
            .OnlyEnforceIf(Not(healthy_recluse))
            .WithName(absl::StrFormat("!%s -> !%s V !%s",
                                      healthy_recluse.Name(), recluse.Name(),
                                      poisoned_recluse.Name()));
      BoolVar target_proc = model_.NewBoolVar().WithName(
          absl::StrFormat("%s_healthy_recluse_or_imp", players_[target]));
      model_.AddEquality(LinearExpr::Sum({imp, healthy_recluse}), target_proc)
            .WithName(absl::StrFormat("%s = exactly 1 of %s, %s",
                                      target_proc.Name(), imp.Name(),
                                      healthy_recluse.Name()));
      model_.AddBoolAnd({is_slayer, target_proc, Not(poisoned)})
            .WithName(absl::StrFormat(
                "Slayer shot succeeded %s -> %s ^ %s ^ !%s", time,
                is_slayer.Name(), target_proc.Name(), poisoned.Name()));
    } else {
      model_.AddBoolAnd({is_slayer, imp, Not(poisoned)})
            .WithName(absl::StrFormat(
                "Slayer shot succeeded %s -> %s ^ %s ^ !%s", time,
                is_slayer.Name(), imp.Name(), poisoned.Name()));
    }
  } else {
    // Must be a night death, which is either an Imp pick, or a Mayor bounce.
    night_death_ = death;
    // The Imp cannot be poisoned:
    for (int i = 0; i < num_players_; ++i) {
      if (is_alive_[i]) {
        const BoolVar& imp_i = night_roles_.back()[i][IMP];
        const BoolVar& poisoned_i = poisoner_pick_.back()[i];
        model_.AddImplication(imp_i, Not(poisoned_i))
              .WithName(absl::StrFormat("Imp killed %s %s: %s -> !%s", name,
                                        time, imp_i.Name(), poisoned_i.Name()));
      }
    }
    // The target cannot be Monk protected by a non-poisoned Monk:
    for (int i : players_claiming_[MONK]) {
      if (is_alive_[i]) {
        const BoolVar& poisoned_i = poisoner_pick_.back()[i];
        const BoolVar& monk_i = night_roles_[0][i][MONK];
        const BoolVar& monk_pick = monk_pick_.back()[death];
        model_.AddBoolOr({Not(monk_i), Not(monk_pick)})
              .OnlyEnforceIf(Not(poisoned_i))
              .WithName(absl::StrFormat(
                  "Monk %s protects %s %s: !%s -> !%s V !%s", players_[i], name,
                  time, poisoned_i.Name(), monk_i.Name(), monk_pick.Name()));
      }
    }
    vector<BoolVar> picks;
    picks.push_back(imp_pick_.back()[death]);
    for (int i : players_claiming_[MAYOR]) {
      if (is_alive_[i] && death != i) {  // Possible Mayor bounce
        // Mayor needs to be non-poisoned and Imp picked:
        const BoolVar& poisoned_i = poisoner_pick_.back()[i];
        const BoolVar& mayor_i = night_roles_[0][i][MAYOR];
        const BoolVar& picked = imp_pick_.back()[i];
        picks.push_back(picked);
        model_.AddBoolOr({Not(mayor_i), Not(picked)})
              .OnlyEnforceIf(poisoned_i)
              .WithName(absl::StrFormat("Mayor %s, %s died %s: %s -> !%s V !%s",
                                        players_[i], name, time,
                                        poisoned_i.Name(), mayor_i.Name(),
                                        picked.Name()));
      }
    }
    model_.AddExactlyOne(picks)
          .WithName(absl::StrFormat("%s died %s -> Imp pick V Mayor bounce",
                                    name, time));
  }
  is_alive_[death] = false;
  --num_alive_;
  next_event_maybe_victory_ = true;
}

void GameState::AddClaim(const string& player, Role role) {
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
    model_.AddBoolOr({day_roles_.back()[p_index][role], is_evil_[p_index]});
    return;
  }
  model_.AddBoolOr({shown_token_[p_index][role], is_evil_[p_index]});
}

void GameState::AddClaim(const Claim& claim) {
  const string& player = claim.player();
  if (!claim.has_info()) {
    AddClaim(player, claim.role());
    return;
  }
  switch (claim.info().details_case()) {
    case RoleAction::kWasherwomanInfo:
      AddClaim(player, WASHERWOMAN);
      AddClaimWasherwomanInfo(player, claim.info().washerwoman_info());
      break;
    case RoleAction::kLibrarianInfo:
      AddClaim(player, LIBRARIAN);
      AddClaimLibrarianInfo(player, claim.info().librarian_info());
      break;
    case RoleAction::kInvestigatorInfo:
      AddClaim(player, INVESTIGATOR);
      AddClaimInvestigatorInfo(player, claim.info().investigator_info());
      break;
    case RoleAction::kChefInfo:
      AddClaim(player, CHEF);
      AddClaimChefInfo(player, claim.info().chef_info());
      break;
    case RoleAction::kEmpathInfo:
      AddClaim(player, EMPATH);
      AddClaimEmpathInfo(player, claim.info().empath_info());
      break;
    case RoleAction::kFortunetellerAction:
      AddClaim(player, FORTUNE_TELLER);
      AddClaimFortuneTellerAction(player, claim.info().fortuneteller_action());
      break;
    case RoleAction::kMonkAction:
      AddClaim(player, MONK);
      AddClaimMonkAction(player, claim.info().monk_action());
      break;
    case RoleAction::kButlerAction:
      AddClaim(player, BUTLER);
      AddClaimButlerAction(player, claim.info().butler_action());
      break;
    case RoleAction::kRavenkeeperInfo:
      AddClaim(player, RAVENKEEPER);
      AddClaimRavenkeeperInfo(player, claim.info().ravenkeeper_info());
      break;
    case RoleAction::kUndertakerInfo:
      AddClaim(player, UNDERTAKER);
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
      AddClaim(player, IMP);
      AddClaimImpAction(player, claim.info().imp_action());
      break;
    case RoleAction::kSpyInfo:
      AddClaim(player, SPY);
      AddClaimSpyInfo(player, claim.info().spy_info());
      break;
    default:
      CHECK(false) << "Expected a valid claim info details, got: "
                   << claim.info().details_case();
  }
}

void GameState::AddClaimWasherwomanInfo(
    const string& player, const LearnRoleInfo& washerwoman_info) {
  BeforeEvent(Event::kClaim);
}

void GameState::AddClaimLibrarianInfo(
    const string& player, const LearnRoleInfo& librarian_info) {
  BeforeEvent(Event::kClaim);
}

void GameState::AddClaimInvestigatorInfo(
    const string& player, const LearnRoleInfo& investigator_info) {
  BeforeEvent(Event::kClaim);
}

void GameState::AddClaimChefInfo(const string& player, int chef_info) {
  BeforeEvent(Event::kClaim);
}

void GameState::AddClaimEmpathInfo(const string& player, int empath_info) {
  BeforeEvent(Event::kClaim);
}

void GameState::AddClaimFortuneTellerAction(
    const string& player, const FortuneTellerAction& fortuneteller_action) {
  BeforeEvent(Event::kClaim);
}

void GameState::AddClaimMonkAction(const string& player,
                                   const string& monk_action) {
  BeforeEvent(Event::kClaim);
}

void GameState::AddClaimButlerAction(const string& player,
                                     const string& butler_action) {
  BeforeEvent(Event::kClaim);
}

void GameState::AddClaimRavenkeeperInfo(
    const string& player, const RavenkeeperInfo& ravenkeeper_info) {
  BeforeEvent(Event::kClaim);
}

void GameState::AddClaimUndertakerInfo(const string& player,
                                       Role undertaker_info) {
  BeforeEvent(Event::kClaim);
}

// The open Spy play.
void GameState::AddClaimSpyInfo(const string& player, const SpyInfo& spy_info) {
  BeforeEvent(Event::kClaim);
}

// Useless, but yay theoretically occur after Recluse starpass.
void GameState::AddClaimImpAction(const string& player,
                                  const string& imp_action) {
  BeforeEvent(Event::kClaim);
}

void GameState::AddVictory(Team victory) {
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

void GameState::AddShownToken(const string& player, Role role) {
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
    model_.AddEquality(shown_token_[p_index][role], model_.TrueVar())
          .WithName(absl::StrFormat("%s shown token %s night 1", player,
                                    Role_Name(role)));
  } else {  // role is IMP
    // TODO(olaola): fill this case (starpass occurred).
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

void GameState::AddMinionInfo(const string& player,
                              const MinionInfo& minion_info) {
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
  const int demon = PlayerIndex(minion_info.demon());
  model_.AddEquality(assigned_roles[demon][IMP], model_.TrueVar())
        .WithName(absl::StrFormat("%s learns minion info: %s is the demon",
                                  player, minion_info.demon()));
  for (const string& minion_name : minion_info.minions()) {
    vector<BoolVar> minion_i;
    for (Role role : kMinionRoles) {
      if (role != shown_token) {  // they are a different minion
        minion_i.push_back(assigned_roles[PlayerIndex(minion_name)][role]);
      }
    }
    model_.AddExactlyOne(minion_i)
          .WithName(absl::StrFormat("%s learns minion info: %s is a minion",
                                    player, minion_name));
  }
}

void GameState::AddDemonInfo(const string& player,
                             const DemonInfo& demon_info) {
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
  CHECK_EQ(demon_info.minions_size(), num_minions_)
      << "Demon info should have " << num_minions_ << " minions";
  for (const string& minion_name : demon_info.minions()) {
    vector<BoolVar> minion_i;
    for (Role role : kMinionRoles) {
      minion_i.push_back(assigned_roles[PlayerIndex(minion_name)][role]);
    }
    model_.AddExactlyOne(minion_i)
          .WithName(absl::StrFormat("%s learns demon info: %s is a minion",
                                    player, minion_name));
  }
  CHECK_EQ(demon_info.bluffs_size(), 3) << "Demon info should have 3 bluffs";
  for (int bluff : demon_info.bluffs()) {
    model_.AddEquality(roles_in_play_[bluff], model_.FalseVar())
          .WithName(absl::StrFormat("%s learns demon info: bluff %s", player,
                                    Role_Name(bluff)));
  }
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
    case RoleAction::kRavenkeeperInfo:
      AddRavenkeeperInfo(player, role_action.ravenkeeper_info());
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

void GameState::AddWasherwomanInfo(
    const string& player, const LearnRoleInfo& washerwoman_info) {
  BeforeEvent(Event::kStorytellerInteraction);
}

void GameState::AddLibrarianInfo(
    const string& player, const LearnRoleInfo& librarian_info) {
  BeforeEvent(Event::kStorytellerInteraction);
}

void GameState::AddInvestigatorInfo(
    const string& player, const LearnRoleInfo& investigator_info) {
  BeforeEvent(Event::kStorytellerInteraction);
}

void GameState::AddChefInfo(const string& player, int chef_info) {
  BeforeEvent(Event::kStorytellerInteraction);
}

void GameState::AddEmpathInfo(const string& player, int empath_info) {
  BeforeEvent(Event::kStorytellerInteraction);
}

void GameState::AddFortuneTellerAction(
    const string& player, const FortuneTellerAction& fortuneteller_action) {
  BeforeEvent(Event::kStorytellerInteraction);
}

void GameState::AddMonkAction(const string& player, const string& monk_action) {
  BeforeEvent(Event::kStorytellerInteraction);
  const int monk = PlayerIndex(player);
  const int target = PlayerIndex(monk_action);
  CHECK_NE(player, monk_action) << "Monk cannot pick themselves";
  CHECK_NE(perspective_, OBSERVER) << "Observer cannot see Monk actions";
  if (perspective_ == STORYTELLER) {
    CHECK_EQ(st_player_roles_[monk], MONK)
        << player << " needs to be the MONK, got "
        << Role_Name(st_player_roles_[monk]);
    st_monk_pick_ = target;
  }
  const BoolVar& picked = monk_pick_.back()[target];
  model_.AddEquality(picked, model_.TrueVar())
        .WithName(absl::StrFormat("monk action: %s", picked.Name()));
}

void GameState::AddButlerAction(const string& player,
                                const string& butler_action) {
  BeforeEvent(Event::kStorytellerInteraction);
}

void GameState::AddRavenkeeperInfo(
    const string& player, const RavenkeeperInfo& ravenkeeper_info) {
  BeforeEvent(Event::kStorytellerInteraction);
}

void GameState::AddUndertakerInfo(const string& player, Role undertaker_info) {
  BeforeEvent(Event::kStorytellerInteraction);
}

void GameState::AddSlayerAction(const string& player,
                                const string& slayer_action) {
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

void GameState::AddPoisonerAction(const string& player,
                                  const string& poisoner_action) {
  BeforeEvent(Event::kStorytellerInteraction);
  const int poisoner = PlayerIndex(player);
  const int target = PlayerIndex(poisoner_action);
  CHECK_NE(perspective_, OBSERVER) << "Observer cannot see poisoner actions";
  if (perspective_ == STORYTELLER) {
    CHECK_EQ(st_player_roles_[poisoner], POISONER)
        << player << " needs to be the POISONER, got "
        << Role_Name(st_player_roles_[poisoner]);
    st_poisoner_pick_ = target;
  }
  const BoolVar& picked = poisoner_pick_.back()[target];
  model_.AddEquality(picked, model_.TrueVar())
        .WithName(absl::StrFormat("Poisoner action: %s", picked.Name()));
}

void GameState::AddImpAction(const string& player, const string& imp_action) {
  BeforeEvent(Event::kStorytellerInteraction);
  const int imp = PlayerIndex(player);
  const int target = PlayerIndex(imp_action);
  CHECK_NE(perspective_, OBSERVER) << "Observer cannot see Imp actions";
  if (perspective_ == STORYTELLER) {
    CHECK_EQ(st_player_roles_[imp], IMP)
        << player << " needs to be the IMP, got "
        << Role_Name(st_player_roles_[imp]);
    st_imp_pick_ = target;
  }
  const BoolVar& picked = imp_pick_.back()[target];
  model_.AddEquality(picked, model_.TrueVar())
        .WithName(absl::StrFormat("Imp action: %s", picked.Name()));
}

void GameState::AddSpyInfo(const string& player, const SpyInfo& spy_info) {
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
    model_.AddEquality(LinearExpr::Sum(townsfolk_cases), proc_townsfolk)
          .WithName(absl::StrFormat("%s definition", proc_townsfolk.Name()));
    model_.AddBoolOr({Not(virgin), poisoned, Not(proc_townsfolk)})
          .WithName(absl::StrFormat("%s didn't Virgin proc -> !%s V %s V %s",
                                    players_[nomination.Nominee], virgin.Name(),
                                    poisoned.Name(), proc_townsfolk.Name()));
    return;
  }
  // In case of a proc, we need to add the non-poisoned Spy option to the
  // townsfolk cases:
  const BoolVar& spy = day_roles_.back()[nomination.Nominator][SPY];
  BoolVar poisoned_spy = CreatePoisonedRoleVar(SPY, cur_time_.Count, true);
  BoolVar healthy_spy = model_.NewBoolVar().WithName(
      absl::StrFormat("%s_is_healthy_spy", players_[nomination.Nominator]));
  model_.AddBoolAnd({spy, Not(poisoned_spy)})
        .OnlyEnforceIf(healthy_spy)
        .WithName(absl::StrFormat("%s -> %s ^ !%s", healthy_spy.Name(),
                                  spy.Name(), poisoned_spy.Name()));
  model_.AddBoolOr({Not(spy), poisoned_spy})
        .OnlyEnforceIf(Not(healthy_spy))
        .WithName(absl::StrFormat("!%s -> !%s V %s", healthy_spy.Name(),
                                  spy.Name(), poisoned_spy.Name()));
  townsfolk_cases.push_back(healthy_spy);
  model_.AddEquality(LinearExpr::Sum(townsfolk_cases), proc_townsfolk)
        .WithName(absl::StrFormat("%s definition", proc_townsfolk.Name()));
  model_.AddBoolAnd({virgin, Not(poisoned), proc_townsfolk})
        .WithName(absl::StrFormat("%s Virgin proc -> %s ^ !%s ^ %s",
                                  players_[nomination.Nominee], virgin.Name(),
                                  poisoned.Name(), proc_townsfolk.Name()));
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
  // TODO(olaola): Add Mayor win case!
  const auto& day_roles = day_roles_.back();
  if (execution_death_ == kNoPlayer && slayer_death_ == kNoPlayer) {
    if (night_death_ == kNoPlayer) {
        model_.AddBoolOr({model_.FalseVar()}).WithName(
            "Contradiction: no day or night kill, yet Good wins");
        return;
    }
    // The silly suicide case:
    model_.AddEquality(day_roles[night_death_][IMP], model_.TrueVar())
          .WithName("Imp kills themselves");
    // The reason we use AddEquality and not FixVariable is debugability.
    for (int i = 0; i < num_players_; ++i) {
      if (is_alive_[i]) {
        // Otherwise they would catch the starpass.
        model_.AddEquality(is_evil_[i], model_.FalseVar())
            .WithName(absl::StrFormat("Player %s cannot catch starpass",
                                      players_[i]));
      }
    }
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
        const string name = absl::StrFormat(
            "If %s is the Scarlet Woman on day %d, they are poisoned",
            players_[i], cur_time_.Count);
        model_.AddImplication(day_sw_i, poisoner_pick[i]).WithName(name);
      }
    }
  }
}

void GameState::AddEvilWonConstraints() {
  // Evil wins only if either:
  // * Non-poisoned Saint was executed, OR
  // * 2 players are alive, one of them the Imp.
  const auto& day_roles = day_roles_.back();
  if (execution_death_ != kNoPlayer) {
    const string name = absl::StrFormat(
        "Evil wins on execution of %s -> non-poisoned Saint was executed",
        players_[execution_death_]);
    BoolVar poisoned = poisoner_pick_[cur_time_.Count][execution_death_];
    model_.AddBoolAnd({day_roles[execution_death_][SAINT], Not(poisoned)})
          .WithName(name);
    return;
  }
  if (num_alive_ > 2) {
    model_.AddBoolOr({model_.FalseVar()}).WithName(
        "Contradiction: no execution and >=3 players alive, yet Evil wins");
    return;
  }
  vector<BoolVar> alive_imp = CollectAliveRoles(day_roles, {IMP});
  model_.AddExactlyOne(alive_imp).WithName("Evil wins -> an alive IMP exists");
}

BoolVar GameState::CreatePoisonerPickedRoleVar(
    Role role, int night, bool only_alive) {
  // Returns a new variable for whether a role was Poisoner picked during night.
  // Note this is not quite the same as poisoned, because Poisoner might have
  // since died (see CreatePoisonedAliveRoleVar for that amendment).
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
      const BoolVar& picked_role_i = model_.NewBoolVar().WithName(
          absl::StrFormat("poisoner_picked_%s_%s_night_%d", Role_Name(role),
                          players_[i], night));
      model_.AddBoolAnd({role_i, picked_i})
            .OnlyEnforceIf(picked_role_i)
            .WithName(absl::StrFormat("%s -> %s ^ %s",
                                      picked_role_i.Name(), role_i.Name(),
                                      picked_i.Name()));
      model_.AddBoolOr({Not(role_i), Not(picked_i)})
            .OnlyEnforceIf(Not(picked_role_i))
            .WithName(absl::StrFormat("!%s -> !%s V !%s",
                                      picked_role_i.Name(), role_i.Name(),
                                      picked_i.Name()));
      picked_role_players.push_back(picked_role_i);
    }
  }
  BoolVar role_picked = model_.NewBoolVar().WithName(
      absl::StrFormat("poisoner_picked_%s_night_%d", Role_Name(role), night));
  if (picked_role_players.size() == 0) {
    model_.AddEquality(role_picked, model_.FalseVar())
          .WithName(absl::StrFormat("%s has no options", role_picked.Name()));
  } else {
    model_.AddEquality(LinearExpr::Sum(picked_role_players), role_picked)
          .WithName(absl::StrFormat("%s definition", role_picked.Name()));
  }
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
  BoolVar poisoned = model_.NewBoolVar().WithName(
      absl::StrFormat("poisoned_%s_day_%d", Role_Name(role), day));
  const BoolVar& poisoner_died = day_roles_.back()[night_death_][POISONER];
  model_.AddBoolAnd({picked, Not(poisoner_died)})
        .OnlyEnforceIf(poisoned)
        .WithName(absl::StrFormat("%s -> %s ^ !%s", poisoned.Name(),
                                  picked.Name(), poisoner_died.Name()));
  model_.AddBoolOr({Not(picked), poisoner_died})
        .OnlyEnforceIf(Not(poisoned))
        .WithName(absl::StrFormat("!%s -> !%s V %s", poisoned.Name(),
                                  picked.Name(), poisoner_died.Name()));
  return poisoned;
}

void GameState::AddNoDeathConstraints() {
  const string time = cur_time_.ToString();
  // This happens after new day or Slayer shot when there was no death.
  // In case of Slayer shot, the shot failed:
  if (slayer_shots_.size() > 0) {
    // This can be either:
    // * Slayer is not actually the Slayer
    // * Target is not the Imp
    // * Slayer was poisoned
    const auto& shot = slayer_shots_.back();
    int target = shot.Target, slayer = shot.Slayer;
    const BoolVar& is_slayer = day_roles_.back()[slayer][SLAYER];
    const BoolVar& is_imp = day_roles_.back()[target][IMP];
    const BoolVar poisoned = CreatePoisonedRoleVar(
        SLAYER, cur_time_.Count, true);
    model_.AddBoolOr({Not(is_slayer), Not(is_imp), poisoned})
          .WithName(absl::StrFormat("Slayer shot failed %s -> !%s V !%s V %s",
                                    time, is_slayer.Name(), is_imp.Name(),
                                    poisoned.Name()));
    return;
  }
  // In case of new day, the Imp did not kill at night. This could be:
  // * Imp chose to sink a kill on a dead player (Imp pick is mandatory)
  // * Imp was poisoned
  // * Monk was not poisoned ^ Imp picked alive Monk protected target
  vector<BoolVar> cases;
  const auto& imp_picks = imp_pick_.back();
  for (int i = 0; i < num_players_; ++i) {
    if (!is_alive_[i]) {
      cases.push_back(imp_picks[i]);  // Sink kill.
    }
  }
  BoolVar imp_poisoned = CreatePoisonerPickedRoleVar(
      IMP, cur_time_.Count, true);
  cases.push_back(imp_poisoned);
  if (players_claiming_[MONK].size() > 0) {
    vector<BoolVar> same_picks;
    for (int i = 0; i < num_players_; ++i) {
      if (is_alive_[i]) {
        const BoolVar& imp_pick_i = imp_pick_.back()[i];
        const BoolVar& monk_pick_i = monk_pick_.back()[i];
        BoolVar both_pick_i = model_.NewBoolVar().WithName(
            absl::StrFormat("imp_and_monk_pick_%s_%s", players_[i], time));
        model_.AddBoolAnd({imp_pick_i, monk_pick_i})
              .OnlyEnforceIf(both_pick_i)
              .WithName(absl::StrFormat("%s -> %s ^ %s",
                                        both_pick_i.Name(),
                                        imp_pick_i.Name(), monk_pick_i.Name()));
        model_.AddBoolOr({Not(imp_pick_i), Not(monk_pick_i)})
              .OnlyEnforceIf(Not(both_pick_i))
              .WithName(absl::StrFormat("!%s -> !%s V !%s",
                                        both_pick_i.Name(),
                                        imp_pick_i.Name(), monk_pick_i.Name()));
        same_picks.push_back(both_pick_i);
      }
    }
    BoolVar picks_equal = model_.NewBoolVar().WithName(
        absl::StrFormat("imp_and_monk_pick_same_%s", time));
    model_.AddEquality(LinearExpr::Sum(same_picks), picks_equal)
          .WithName(absl::StrFormat("%s definition", picks_equal.Name()));
    BoolVar monk_poisoned = CreatePoisonerPickedRoleVar(
        MONK, cur_time_.Count, true);
    BoolVar protected_pick = model_.NewBoolVar().WithName(
        absl::StrFormat("imp_pick_healthy_monk_protected_%s", time));
    model_.AddBoolAnd({Not(monk_poisoned), picks_equal})
          .OnlyEnforceIf(protected_pick)
          .WithName(absl::StrFormat("%s -> !%s ^ %s", protected_pick.Name(),
                                    monk_poisoned.Name(), picks_equal.Name()));
    model_.AddBoolOr({monk_poisoned, Not(picks_equal)})
          .OnlyEnforceIf(Not(protected_pick))
          .WithName(absl::StrFormat("!%s -> %s V !%s", protected_pick.Name(),
                                    monk_poisoned.Name(), picks_equal.Name()));
    cases.push_back(protected_pick);
  }
  model_.AddBoolOr(cases).WithName(absl::StrFormat("No night deaths %s", time));
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
    const string name = absl::StrFormat(
        "Contradiction: %d players alive on %s, yet game is not over",
        num_alive_, cur_time_.ToString());
    model_.AddBoolOr({model_.FalseVar()}).WithName(name);
    return;
  }
  const auto& roles = day_roles_.back();
  vector<BoolVar> alive_imp = CollectAliveRoles(roles, {IMP});
  if (num_alive_ >=4) {
    BoolVar alive_sw = CreateAliveRoleVar(SCARLET_WOMAN, cur_time_);
    BoolVar poisoned_sw = CreatePoisonedRoleVar(
        SCARLET_WOMAN, cur_time_.Count, true);
    BoolVar healthy_sw = model_.NewBoolVar().WithName(
        absl::StrFormat("healthy_scarlet_woman_%s", cur_time_.ToString()));
    model_.AddBoolAnd({Not(poisoned_sw), alive_sw})
          .OnlyEnforceIf(healthy_sw)
          .WithName(absl::StrFormat("%s -> !%s ^ %s", healthy_sw.Name(),
                                    poisoned_sw.Name(), alive_sw.Name()));
    model_.AddBoolOr({poisoned_sw, Not(alive_sw)})
          .OnlyEnforceIf(Not(healthy_sw))
          .WithName(absl::StrFormat("!%s -> %s V !%s", healthy_sw.Name(),
                                    poisoned_sw.Name(), alive_sw.Name()));
    alive_imp.push_back(healthy_sw);  // SW can become Imp at night.
  }
  model_.AddBoolOr(alive_imp).WithName(absl::StrFormat(
      "Good did not win -> Imp or healthy SW is alive on %s",
      cur_time_.ToString()));
  if (execution_death_ != kNoPlayer) {
    BoolVar poisoned = CreatePoisonedRoleVar(SAINT, cur_time_.Count, false);
    const BoolVar& executed_saint = roles[execution_death_][SAINT];
    model_.AddBoolOr({Not(executed_saint), poisoned})
          .WithName(absl::StrFormat("Evil did not win %s -> !%s V %s",
                                    cur_time_.ToString(), executed_saint.Name(),
                                    poisoned.Name()));
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
    vector<BoolVar> inverted;
    for (int i = 0; i < num_players_; ++i) {
      for (Role role : kAllRoles) {
        if (SolutionBooleanValue(response, current_roles[i][role])) {
          inverted.push_back(Not(current_roles[i][role]));
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
    model.AddBoolOr(inverted);  // At least one literal is different.
  }
  return result;
}
}  // namespace botc
