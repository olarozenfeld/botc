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

#include "ortools/sat/cp_model.pb.h"
#include "ortools/sat/cp_model_solver.h"
#include "ortools/sat/model.h"
#include "ortools/sat/sat_parameters.pb.h"

namespace botc {

namespace {
using operations_research::sat::CpSolverResponse;
using operations_research::sat::CpSolverStatus;
using operations_research::sat::LinearExpr;
using operations_research::sat::Model;
using operations_research::sat::NewFeasibleSolutionObserver;
using operations_research::sat::SatParameters;

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
}  // namespace

namespace internal {
string Time::ToString() const {
  return absl::StrFormat("%s_%d", IsDay ? "day" : "night", Count);
}
}  // namespace internal

// TODO(olaola): replace CHECKs with absl::Status everywhere.
// TODO(olaola): fix the day poisoned bug (alive poisoner should exist).

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

GameState::GameState(Perspective perspective, const Setup& setup)
    : perspective_(perspective), num_players_(setup.players_size()),
      is_alive_(num_players_, true), num_alive_(num_players_),
      num_votes_(0), on_the_block_(kNoPlayer), execution_(kNoPlayer),
      execution_death_(kNoPlayer), slayer_death_(kNoPlayer),
      night_death_(kNoPlayer), game_maybe_over_(false),
      victory_(TEAM_UNSPECIFIED), st_player_roles_(num_players_),
      st_red_herring_(kNoPlayer), st_poisoner_pick_(kNoPlayer),
      st_imp_pick_(kNoPlayer) {
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

  InitRoleVars();
  InitHelperVars();
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
    model_.AddImplication(v, is_evil_[i].Not())
        .WithName(absl::StrFormat("%s implies %s is good", name, players_[i]));
  }
  const BoolVar& ft_in_play = roles_in_play_[FORTUNE_TELLER];
  // If a Fortune Teller is in play, there is exactly one red herring.
  model_.AddEquality(LinearExpr::Sum(red_herring_), 1)
      .OnlyEnforceIf(ft_in_play)
      .WithName("ft_in_play -> 1 red herring");
  // If a Fortune Teller is not in play, there is no red herring.
  model_.AddEquality(LinearExpr::Sum(red_herring_), 0)
      .OnlyEnforceIf(ft_in_play.Not())
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

void GameState::InitRoleVars() {
  // night1_roles[i][role] is true iff player players_[i] has role.
  vector<vector<BoolVar>> night1_roles(num_players_);
  for (int i = 0; i < num_players_; ++i) {
    night1_roles[i].push_back(model_.FalseVar());  // dummy variable
    for (Role role : kAllRoles) {
      // Variable/constraint names are for debugging only.
      string name = absl::StrFormat(
        "role_%s_%s_night_1", players_[i], Role_Name(role));
      night1_roles[i].push_back(model_.NewBoolVar().WithName(name));
    }
  }
  night_roles_.push_back(night1_roles);
  if (perspective_ == STORYTELLER) {
    for (int i = 0; i < num_players_; ++i) {
      Role role = st_player_roles_[i];
      const auto& pr = night1_roles[i];
      // We don't need to fix all of them, but it will be faster.
      for (Role role1 : kAllRoles) {
          model_.FixVariable(pr[role1], role1 == role);
      }
    }
  }
  InitVarRolesInPlay();
  AddRoleUniquenessConstraints(night1_roles);
  // Appropriate numbers of outsiders, townsfolk and minions:
  vector<BoolVar> minions = CollectRoles(night1_roles, kMinionRoles);
  model_.AddEquality(LinearExpr::Sum(minions), num_minions_)
      .WithName(absl::StrFormat("Exactly %d minions", num_minions_));
  AddBaronConstraints();
  InitPoisonerVars();
}

void GameState::AddRoleUniquenessConstraints(
    const vector<vector<BoolVar>>& player_roles) {
  const string time = cur_time_.ToString();
  // Each player assigned exactly one role:
  for (int i = 0; i < num_players_; ++i) {
    string name = absl::StrFormat("player %s has unique role %s", players_[i],
                                  time);
    model_.AddExactlyOne(player_roles[i]).WithName(name);
  }
  // Each role other than IMP assigned to at most one player:
  for (Role role : kAllRoles) {
    if (role == IMP) {
      continue;
    }
    vector<BoolVar> player_is_role(num_players_);
    for (int i = 0; i < num_players_; ++i) {
      player_is_role[i] = player_roles[i][role];
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
      .OnlyEnforceIf(baron_in_play.Not())
      .WithName(
        absl::StrFormat("!baron_in_play -> %d outsiders", num_outsiders_));
  model_.AddEquality(LinearExpr::Sum(outsiders), num_outsiders_ + 2)
      .OnlyEnforceIf(baron_in_play)
      .WithName(
        absl::StrFormat("baron_in_play -> %d outsiders", num_outsiders_ + 2));
  int num_townsfolk = kNumTownsfolk[num_players_ - 5];
  vector<BoolVar> townsfolk = CollectRoles(night_roles_[0], kTownsfolkRoles);;
  model_.AddEquality(LinearExpr::Sum(townsfolk), num_townsfolk)
      .OnlyEnforceIf(baron_in_play.Not())
      .WithName(
        absl::StrFormat("!baron_in_play -> %d townsfolk", num_townsfolk));
  model_.AddEquality(LinearExpr::Sum(townsfolk), num_townsfolk - 2)
      .OnlyEnforceIf(baron_in_play)
      .WithName(
        absl::StrFormat("baron_in_play -> %d townsfolk", num_townsfolk - 2));
}

void GameState::InitHelperVars() {
  // A Player is Evil iff they have an Evil role on Night 1 (in TB).
  for (int i = 0; i < num_players_; ++i) {
    BoolVar is_evil = model_.NewBoolVar()
        .WithName(absl::StrFormat("Evil_%s", players_[i]));
    is_evil_.push_back(is_evil);
    vector<BoolVar> evil_roles;
    for (int role : kEvilRoles) {
      evil_roles.push_back(night_roles_[0][i][role]);
    }
    model_.AddEquality(LinearExpr::Sum(evil_roles), is_evil)
        .WithName(absl::StrFormat("Evil_%s definition", players_[i]));
  }
}

void GameState::AddEvent(const Event& event) {
  CHECK_EQ(victory_, TEAM_UNSPECIFIED) << "No events allowed after victory";
  if (game_maybe_over_ && event.details_case() != Event::kVictory) {
    AddGameNotOverConstraints();
    game_maybe_over_ = false;
  }
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
      game_maybe_over_ = true;
      break;
    case Event::kClaim:
      AddClaim(event.claim());
      break;
    case Event::kVictory:
      AddVictory(event.victory());
      break;
    default:
      CHECK(false) << "Expected a valid event details, got: "
                    << event.details_case();
  }
}

void GameState::AddDay(int count) {
  CHECK(!cur_time_.IsDay)
      << "Trying to begin another day during day " << cur_time_.Count;
  cur_time_.IsDay = true;
  CHECK_EQ(count, cur_time_.Count) << absl::StrFormat(
      "Day %d needs to follow night %d", cur_time_.Count, cur_time_.Count);
  InitNextDayRoleVars();
  InitNextDayHelperVars();
  nominations_.clear();
  slayer_shots_.clear();
  num_votes_ = 0;
  on_the_block_ = execution_ = execution_death_ = slayer_death_ = night_death_ =
      kNoPlayer;
}

void GameState::AddNight(int count) {
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
  InitNextNightRoleVars();
  InitNextNightHelperVars();  // In particular, imp and poisoner picks.
  night_death_ = st_poisoner_pick_ = st_imp_pick_ = kNoPlayer;
}

// Assumption: called only on Night > 1.
void GameState::InitNextNightRoleVars() {
  vector<vector<BoolVar>> night_roles(num_players_);
  for (int i = 0; i < num_players_; ++i) {
    night_roles[i].push_back(model_.FalseVar());  // dummy variable
    for (Role role : kAllRoles) {
      string name = absl::StrFormat(
        "role_%s_%s_night_%d", players_[i], Role_Name(role), cur_time_.Count);
      night_roles[i].push_back(model_.NewBoolVar().WithName(name));
    }
  }
  AddRoleUniquenessConstraints(night_roles);
  night_roles_.push_back(night_roles);
  // The Good roles propagate from night 1:
  PropagateRoles(night_roles_[0], night_roles, kGoodRoles);
  // All Evil roles except Scarlet Woman & Imp propagate from the previous day:
  PropagateRoles(day_roles_.back(), night_roles, {POISONER, SPY, BARON});
  AddScarletWomanConstraints();
}

void GameState::InitNextNightHelperVars() {
  vector<BoolVar> imp_picks;
  for (int i = 0; i < num_players_; ++i) {
    string name = absl::StrFormat("imp_picks_%s_night_%d", players_[i],
                                  cur_time_.Count);
    imp_picks.push_back(model_.NewBoolVar().WithName(name));
  }
  imp_pick_.push_back(imp_picks);
  InitPoisonerVars();
}

void GameState::InitPoisonerVars() {
  vector<BoolVar> poisoner_picks;
  for (int i = 0; i < num_players_; ++i) {
    string name = absl::StrFormat("poisoner_picks_%s_night_%d", players_[i],
                                  cur_time_.Count);
    poisoner_picks.push_back(model_.NewBoolVar().WithName(name));
  }
  poisoner_pick_.push_back(poisoner_picks);
}

void GameState::InitNextDayRoleVars() {
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
  // The Imp, and all Good roles except Recluse always propagate from night 1:
  PropagateRoles(night_roles_[0], day_roles, kTownsfolkRoles);
  PropagateRoles(night_roles_[0], day_roles, {BUTLER, DRUNK, SAINT});
  // The Minions and Recluse can catch a starpass.
  AddImpStarpassConstraints();
}

void GameState::InitNextDayHelperVars() {
}

void GameState::AddScarletWomanConstraints() {
  // The Scarlet Woman becoming Imp triggers if and only if, on previous day:
  // * There are >=5 alive players, AND
  // * SW is alive, AND
  // * SW is not poisoned, AND
  // * The IMP died (executed OR Slayer shot)
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
    if (num_alive_ >= 5 && imp_died && sw_player != kNoPlayer &&
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
  if (num_alive_ < 5 ||
      (execution_death_ == kNoPlayer && slayer_death_ == kNoPlayer)) {
    // Then we know for sure SW can't trigger.
    PropagateRoles(day_roles, night_roles, {SCARLET_WOMAN, IMP});
    return;
  }
  int death = slayer_death_ != kNoPlayer ? slayer_death_ : execution_death_;
  BoolVar imp_died = day_roles[death][IMP];
  const auto& poisoner_pick = poisoner_pick_.back();
  for (int i = 0; i < num_players_; ++i) {
    BoolVar night_imp_i = night_roles[i][IMP];
    BoolVar night_sw_i = night_roles[i][SCARLET_WOMAN];
    BoolVar day_imp_i = day_roles[i][IMP];
    BoolVar day_sw_i = day_roles[i][SCARLET_WOMAN];
    // The Imp remains an Imp, even dead.
    model_.AddImplication(day_imp_i, night_imp_i);
    const string name = absl::StrFormat(
        "sw_trigger_day_%d_%s", cur_time_.Count - 1, players_[i]);
    BoolVar sw_trigger_i = model_.NewBoolVar().WithName(name);
    if (is_alive_[i]) {
      model_.AddBoolAnd({day_sw_i, poisoner_pick[i].Not(), imp_died})
        .OnlyEnforceIf(sw_trigger_i)
        .WithName(absl::StrFormat("%s -> definition", name));
      model_.AddBoolOr({day_sw_i.Not(), poisoner_pick[i], imp_died.Not()})
        .OnlyEnforceIf(sw_trigger_i.Not())
        .WithName(absl::StrFormat("%s <- definition", name));
    } else {
      model_.FixVariable(sw_trigger_i, false);
    }
    model_.AddImplication(sw_trigger_i, night_imp_i)
        .WithName(absl::StrFormat("%s -> %s", name, night_imp_i.Name()));
    model_.AddImplication(sw_trigger_i, night_sw_i.Not())
        .WithName(absl::StrFormat("%s -> !%s", name, night_sw_i.Name()));
    model_.AddBoolOr({sw_trigger_i, day_imp_i})
        .OnlyEnforceIf(night_imp_i)
        .WithName(absl::StrFormat("%s -> %s V %s", night_imp_i.Name(),
                  name, day_imp_i.Name()));
    model_.AddBoolAnd({sw_trigger_i.Not(), day_sw_i})
        .OnlyEnforceIf(night_sw_i)
        .WithName(absl::StrFormat("%s -> !%s ^ %s", night_sw_i.Name(),
                  name, day_sw_i.Name()));
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
      model_.AddBoolOr({imp_i.Not(), poisoned_i, imp_pick_i.Not()})
            .OnlyEnforceIf(starpass_i.Not())
            .WithName(name);
      name = absl::StrFormat(
          "%s -> %s ^ !%s ^ %s", starpass_i.Name(), imp_i.Name(),
          poisoned_i.Name(), imp_pick_i.Name());
      model_.AddBoolAnd({imp_i, poisoned_i.Not(), imp_pick_i})
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
      model_.AddBoolAnd({night_imp_i.Not(), day_imp_i})
          .OnlyEnforceIf(catch_i)
          .WithName(absl::StrFormat("%s -> !%s ^ %s", catch_i.Name(),
                                    night_imp_i.Name(), day_imp_i.Name()));
      model_.AddBoolOr({night_imp_i, day_imp_i.Not()})
          .OnlyEnforceIf(catch_i.Not())
          .WithName(absl::StrFormat("!%s -> %s V !%s", catch_i.Name(),
                                    night_imp_i.Name(), day_imp_i.Name()));
      const BoolVar& recluse_i = night_roles[i][RECLUSE];
      const BoolVar& poisoned_i = poisoner_pick_.back()[i];
      BoolVar healthy_recluse_i = model_.NewBoolVar().WithName(
          absl::StrFormat("healthy_recluse_%s_%s", players_[i], night_name));
      model_.AddBoolAnd({recluse_i, poisoned_i.Not()})
          .OnlyEnforceIf(healthy_recluse_i)
          .WithName(absl::StrFormat("%s -> %s ^ !%s", healthy_recluse_i.Name(),
                                    recluse_i.Name(), poisoned_i.Name()));
      model_.AddBoolOr({recluse_i.Not(), poisoned_i})
          .OnlyEnforceIf(healthy_recluse_i.Not())
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
        model_.AddImplication(e, nobody_eligible.Not()).WithName(
            absl::StrFormat("%s -> !%s", e.Name(), nobody_eligible.Name()));
        model_.AddEquality(night_role_i, day_role_i)
              .OnlyEnforceIf(catch_i.Not())
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
        .OnlyEnforceIf(starpass.Not())
        .WithName(absl::StrFormat("!%s -> no catch", starpass.Name()));
  // starpass -> exactly one catches OR nobody eligible
  player_catch.push_back(nobody_eligible);
  model_.AddEquality(LinearExpr::Sum(player_catch), 1)
        .OnlyEnforceIf(starpass)
        .WithName(absl::StrFormat(
            "%s -> 1 catch or noone eligible", starpass.Name()));
}

vector<BoolVar> GameState::CollectRoles(const vector<vector<BoolVar>>& from,
                                        absl::Span<const Role> roles,
                                        bool only_alive) const {
  vector<BoolVar> result;
  for (int i = 0; i < num_players_; ++i) {
    if (!only_alive || is_alive_[i]) {
      for (int role : roles) {
        result.push_back(night_roles_[0][i][role]);
      }
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
}

void GameState::AddVote(const Vote& vote) {
  // I should be able to use the votes repeated field as absl::Span, but I
  // failed at that, so I'll just copy it to a vector:
  vector<string> votes(vote.votes().begin(), vote.votes().end());
  AddVote(votes, vote.on_the_block());
}

void GameState::AddVote(absl::Span<const string> votes,
                        const string& on_the_block) {
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
  // A Virgin didn't proc, otherwise we'd get an execution without a vote.
  // So we know that nominee is not the Virgin, or is dead, or is poisoned.
  if (is_alive_[nomination.Nominee]) {
    const BoolVar& virgin = night_roles_[0][nomination.Nominee][VIRGIN];
    const BoolVar& poisoned = poisoner_pick_.back()[nomination.Nominee];
    model_.AddBoolOr({virgin.Not(), poisoned}).WithName(
        absl::StrFormat("%s didn't Virgin proc -> !%s V %s",
                        players_[nomination.Nominee], virgin.Name(),
                        poisoned.Name()));
  }
  // TODO(olaola): add Butler constraints here (could only vote with master).
}

void GameState::AddExecution(const string& name) {
  CHECK(cur_time_.IsDay) << "Executions can only occur during the day.";
  const int executee = PlayerIndex(name);
  CHECK_EQ(execution_, kNoPlayer) << "More than one execution attempted.";
  CHECK(!nominations_.empty()) << "Execution must have a preceding nomination.";

  if (executee != on_the_block_) {
    // Virgin proc.
    const auto& nomination = nominations_.back();
    CHECK_EQ(executee, nomination.Nominator)
        << absl::StrFormat("Execution needs to be either of %s (who is on the "
                           "block), or of %s who is last to nominate, got %s",
                           (on_the_block_ == kNoPlayer ? "nobody"
                              : players_[on_the_block_]),
                           players_[nomination.Nominator], name);
    CHECK(is_alive_[nomination.Nominee])
        << "Virgin " << players_[nomination.Nominee]
        << " needs to be alive to proc.";
    const BoolVar& virgin = night_roles_[0][nomination.Nominee][VIRGIN];
    const BoolVar& poisoned = poisoner_pick_.back()[nomination.Nominee];
    model_.AddBoolAnd({virgin, poisoned.Not()}).WithName(
        absl::StrFormat("%s Virgin proc -> %s ^ !%s",
                        players_[nomination.Nominee], virgin.Name(),
                        poisoned.Name()));
  }
  execution_ = executee;
}

void GameState::AddDeath(const string& name) {
  // Deaths are Storyteller announcements of deaths, hence they only occur
  // during the day.
  CHECK(cur_time_.IsDay) << "Death annoucements can only occur during the day.";
  const int death = PlayerIndex(name);
  CHECK(is_alive_[death]) << "What is dead may never die: " << name;
  // Deaths are either an announced night death, Slayer shots, or executions,
  // in that order.
  if (execution_ != kNoPlayer) {
    // This can only be an execution death, due to order.
    CHECK_EQ(death, execution_)
        << absl::StrFormat("Expected death of executee %s, got %s.",
                           players_[execution_], name);
    execution_death_ = death;
  } else if (!slayer_shots_.empty()) {
    // Must be a Slayer kill, due to order.
    const auto& shot = slayer_shots_.back();
    CHECK_EQ(death, shot.Target)
        << absl::StrFormat("Expected death of Slayer shot %s, got %s.",
                           players_[shot.Target], name);
    // Slayer procs, so we know they are alive, not poisoned, and shot the Imp
    // or a non-poisoned Recluse: TODO(olaola): add this!
    CHECK(is_alive_[shot.Slayer])
        << "Slayer " << players_[shot.Slayer] << " needs to be alive to proc.";
    const BoolVar& slayer = night_roles_[0][shot.Slayer][SLAYER];
    const BoolVar& imp = day_roles_.back()[shot.Target][IMP];
    const BoolVar& poisoned = poisoner_pick_.back()[shot.Slayer];
    model_.AddBoolAnd({slayer, poisoned.Not(), imp}).WithName(
        absl::StrFormat("%s Slayer proc -> %s ^ !%s ^ %s",
                        players_[shot.Slayer], slayer.Name(), poisoned.Name(),
                        imp.Name()));
    // TODO(olaola): add spent constraint, here and for Virgin.
  } else {
    // Must be a night death, which is either an Imp pick, or a Mayor bounce.
    night_death_ = death;
    // TODO(olaola): add appropriate constraints here.
  }
  is_alive_[death] = false;
  --num_alive_;
}

void GameState::AddClaim(const string& player, Role role) {
}

void GameState::AddClaim(const string& player,
    Role role, const RoleAction& info) {
}

void GameState::AddClaim(const Claim& claim) {
  if (claim.has_info()) {
    AddClaim(claim.player(), claim.role(), claim.info());
  } else {
    AddClaim(claim.player(), claim.role());
  }
}

void GameState::AddVictory(Team victory) {
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
//  const int pi = PlayerIndex(player);
}

void GameState::AddMinionInfo(const string& player,
                              const MinionInfo& minion_info) {
//  const int pi = PlayerIndex(player);
}

void GameState::AddDemonInfo(const string& player,
                             const DemonInfo& demon_info) {
//  const int pi = PlayerIndex(player);
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
}

void GameState::AddLibrarianInfo(
    const string& player, const LearnRoleInfo& librarian_info) {
}

void GameState::AddInvestigatorInfo(
    const string& player, const LearnRoleInfo& investigator_info) {
}

void GameState::AddChefInfo(const string& player, int chef_info) {
}

void GameState::AddEmpathInfo(const string& player, int empath_info) {
}

void GameState::AddFortuneTellerAction(
    const string& player, const FortuneTellerAction& fortuneteller_action) {
}

void GameState::AddMonkAction(const string& player, const string& monk_action) {
}

void GameState::AddButlerAction(const string& player,
                                const string& butler_action) {
}

void GameState::AddRavenkeeperInfo(
    const string& player, const RavenkeeperInfo& ravenkeeper_info) {
}

void GameState::AddUndertakerInfo(const string& player, Role undertaker_info) {
}

void GameState::AddSlayerAction(const string& player,
                                const string& slayer_action) {
}

void GameState::AddPoisonerAction(const string& player,
                                  const string& poisoner_action) {
}

void GameState::AddImpAction(const string& player, const string& imp_action) {
}

void GameState::AddSpyInfo(const string& player, const SpyInfo& spy_info) {
}

void GameState::AddGoodWonConstraints() {
  // Good can only win if either:
  // * The Imp died today (executed OR Slayer shot) AND
  //    * There are <5 alive players, OR
  //    * SW is not alive, OR
  //    * SW is poisoned
  // OR (silly case, but needs to be addressed):
  // * The Imp killed themselves at night, AND
  // * No-one could catch the starpass (no alive minion - Recluse is optional)
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
  if (num_alive_ >= 5) {
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
    model_.AddBoolAnd({day_roles[execution_death_][SAINT], poisoned.Not()})
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

void GameState::AddGameNotOverConstraints() {
  // This is during the day, after a death. It means !(GOOD won) && !(EVIL won).
  //
  // Good wins iff there is no alive Imp.
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
  const auto& night_roles = night_roles_.back();
  vector<BoolVar> alive_imp = CollectAliveRoles(night_roles, {IMP});
  model_.AddExactlyOne(alive_imp).WithName(absl::StrFormat(
      "Good did not win -> 1 Imp is alive on %s", cur_time_.ToString()));
  if (execution_death_ != kNoPlayer) {
    BoolVar poisoned = poisoner_pick_.back()[execution_death_];
    const string name = absl::StrFormat(
        "Evil did not win day %d -> %s is not the SAINT or %s was poisoned",
        cur_time_.Count - 1, players_[execution_death_],
        players_[execution_death_]);
    model_.AddBoolOr({night_roles[execution_death_][SAINT].Not(), poisoned})
        .WithName(name);
  }
}

SolverRequest FromPlayerRoles(const unordered_map<string, Role>& player_roles) {
  SolverRequest request;
  for (const auto& it : player_roles) {
    auto *pr = request.mutable_assumptions()->add_roles();
    pr->set_player(it.first);
    pr->set_role(it.second);
  }
  request.set_stop_after_first_solution(true);
  return request;
}

SolverResponse GameState::ValidWorld(
    const unordered_map<string, Role>& player_roles) const {
  SolverRequest request = FromPlayerRoles(player_roles);
  request.set_stop_after_first_solution(true);
  return SolveGame(request);
}

SolverResponse GameState::SolveGame() const {
  return SolveGame(SolverRequest());
}

SolverResponse GameState::SolveGame(
    const unordered_map<string, Role>& player_roles) const {
  SolverRequest request;
  for (const auto& it : player_roles) {
    auto *pr = request.mutable_assumptions()->add_roles();
    pr->set_player(it.first);
    pr->set_role(it.second);
  }
  return SolveGame(request);
}

// This function will be very slow in low-info game states. Consider adding
// more assumptions for these cases.
SolverResponse GameState::SolveGame(const SolverRequest& request) const {
  SolverResponse result;
  CpModelBuilder model(model_);  // Making a copy to add assumptions repeatedly.
  const auto& current_roles =
    (cur_time_.IsDay ? day_roles_ : night_roles_).back();
  vector<BoolVar> assumption_literals;
  for (const auto& pr : request.assumptions().roles()) {
    const int player = PlayerIndex(pr.player());
    const auto& v = current_roles[player][pr.role()];
    assumption_literals.push_back(pr.is_not() ? v.Not() : v);
  }
  for (int role : request.assumptions().roles_in_play()) {
    assumption_literals.push_back(roles_in_play_[role]);
  }
  for (int role : request.assumptions().roles_not_in_play()) {
    assumption_literals.push_back(roles_in_play_[role].Not());
  }
  model.AddAssumptions(assumption_literals);
  CpSolverResponse response;
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
        if (SolutionIntegerValue(response, current_roles[i][role])) {
          inverted.push_back(current_roles[i][role].Not());
          const string player = players_[i];
          CHECK(roles->find(player) == roles->end())
              << "Double role assignment for player " << player;
          (*roles)[player] = role;
        }
      }
    }
    CHECK_EQ(roles->size(), num_players_) << "Not all players assigned roles.";
    if (request.stop_after_first_solution()) {
      break;
    }
    // Limit further solutions to different role assignments:
    model.AddBoolOr(inverted);  // At least one literal is different.
  }
  return result;
}
}  // namespace botc
