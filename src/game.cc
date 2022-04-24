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

namespace botc {

const int kNoPlayer =  - 1;  // Used in place of player index.

using operations_research::sat::CpSolverResponse;
using operations_research::sat::CpSolverStatus;
using operations_research::sat::LinearExpr;


namespace {
  const int kNumTownsfolk[] = {3, 3, 5, 5, 5, 7, 7, 7, 9, 9, 9};
  const int kNumOutsiders[] = {0, 1, 0, 1, 2, 0, 1, 2, 0, 1, 2};
  const int kNumMinions[] = {1, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3};
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
  const Role kDemonRoles[] = {IMP};
}  // namespace

// TODO(olaola): replace CHECKs with absl::Status everywhere.

GameState::GameState(Perspective perspective, const Setup& setup)
    : perspective_(perspective), num_players_(setup.players_size()),
      is_alive_(num_players_, true), num_alive_(num_players_),
      num_votes_(0), on_the_block_(kNoPlayer), execution_(kNoPlayer),
      execution_death_(kNoPlayer), slayer_death_(kNoPlayer),
      night_death_(kNoPlayer), victory_(TEAM_UNSPECIFIED),
      st_player_roles_(num_players_), st_red_herring_(kNoPlayer),
      st_poisoner_pick_(kNoPlayer), st_imp_pick_(kNoPlayer) {
  CHECK_GE(num_players_, 5);
  CHECK_LE(num_players_, 15);
  num_outsiders_ = kNumOutsiders[num_players_ - 5];
  num_minions_ = kNumMinions[num_players_ - 5];
  int player_index = 0;
  for (const auto& name : setup.players()) {
      players_.push_back(name);
      CHECK(player_index_.find(name) == player_index_.end())
          << "Player " << name << " is not unique";
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
  for (const auto& player_role : setup.player_roles()) {
    const string& name = player_role.player();
    CHECK_NE(player_role.role(), ROLE_UNSPECIFIED)
        << "Got unassigned role for player " << name;
    const auto it = player_index_.find(name);
    CHECK(it != player_index_.end()) << "Invalid player name: " << name;
    st_player_roles_[it->second] = player_role.role();
  }

  InitRoleVars();
  night_roles_[0].InitIsEvil(kEvilRoles, player);
  InitRedHerring(setup.red_herring());
}

GameState::GameState(const GameLog& game_log):
  GameState(game_log.perspective(), game_log.setup()) {
  for (const auto& event : game_log.events()) {
    AddEvent(event);
  }
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
    const auto it = player_index_.find(name);
    CHECK(it != player_index_.end()) << "Invalid red herring player: " << name;
    st_red_herring_ = it->second;
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
  BoolVar ft_in_play = NewVarRoleInPlay(FORTUNE_TELLER);
  // If a Fortune Teller is in play, there is exactly one red herring.
  model_.AddEquality(LinearExpr::Sum(red_herring_), 1)
      .OnlyEnforceIf(ft_in_play)
      .WithName("ft_in_play -> 1 red herring");
  // If a Fortune Teller is not in play, there is no red herring.
  model_.AddEquality(LinearExpr::Sum(red_herring_), 0)
      .OnlyEnforceIf(ft_in_play.Not())
      .WithName("!ft_in_play -> no red herring");
}

BoolVar GameState::NewVarRoleInPlay(Role role) {
  return night_roles_[0].RoleInPlay(role);
}

void GameState::InitRoleVars() {
  // night1_roles[i][role] is true iff player players_[i] has role.
  RoleMatrix night1_roles(model_, players_, Role_MIN, Role_MAX, 1);
  night_roles_.push_back(night1_roles);

  if (perspective_ == STORYTELLER) {
    for (int i = 0; i < num_players_; ++i) {
      Role role = st_player_roles_[i];
      night1_roles.FixRole(i, role);
    }
  }
  
  // There is exactly one IMP:
  night1_roles.enforceRoleCount(kDemonRoles, 1).WithName("Exactly 1 IMP");
  
  // Appropriate numbers of outsiders, townsfolk and minions
  night1_roles.enforceRoleCount(kMinionRoles, num_minions_)
      .WithName(absl::StrFormat("Exactly %d minions", num_minions_));
  
  night1_roles.enforceRoleCount(kOutsiderRoles, num_outsiders_)
      .OnlyEnforceIf(baron_in_play.Not())
      .WithName(
        absl::StrFormat("!baron_in_play -> %d outsiders", num_outsiders_));
  AddBaronConstraints();
}

void GameState::AddBaronConstraints() {
  BoolVar baron_in_play = night1_roles.RoleInPlay(BARON);
  night1_roles_.enforceRoleCount(kOutsiderRoles, num_outsiders_)
      .OnlyEnforceIf(baron_in_play.Not())
      .WithName(
        absl::StrFormat("!baron_in_play -> %d outsiders", num_outsiders_));
  night1_roles.enforceRoleCount(kOutsiderRoles, num_outsiders_ + 2)
      .OnlyEnforceIf(baron_in_play)
      .WithName(
        absl::StrFormat("baron_in_play -> %d outsiders", num_outsiders_ + 2));

  night1_roles.enfoceRoleCount(kTownsfolkRoles, num_townsfolk)
      .OnlyEnforceIf(baron_in_play.Not())
      .WithName(
        absl::StrFormat("!baron_in_play -> %d townsfolk", num_townsfolk));
  night1_roles.enfoceRoleCount(kTownsfolkRoles, num_townsfolk - 2)
      .OnlyEnforceIf(baron_in_play)
      .WithName(
        absl::StrFormat("baron_in_play -> %d townsfolk", num_townsfolk - 2));
}

void GameState::AddEvent(const Event& event) {
  CHECK_EQ(victory_, TEAM_UNSPECIFIED) << "No events allowed after victory";
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
    default:
      CHECK(false) << "Expected a valid event details, got: "
                    << event.details_case();
  }
}

void GameState::AddDay(int count) {
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
  InitNextNightHelperVars();
  AddGameNotOverConstraints();  // Everyone go to sleep -> game is continuing.
  night_death_ = st_poisoner_pick_ = st_imp_pick_ = kNoPlayer;
}

// Assumption: called only on Night > 1.
void GameState::InitNextNightRoleVars() {
  RoleMatrix night_roles(num_players_, players_, Role_MIN, Role_MAX, cur_time_.Count));
  night_roles_.push_back(night_roles);

  // The Good roles propagate from night 1:
  const string prev_day_name = absl::StrFormat("day_%d", cur_time_.Count - 1);
  const string night_name = absl::StrFormat("night_%d", cur_time_.Count);

  night_roles.PropagateRolesFrom(night_roles_[0], kGoodRoles, players_, "night_1", night_name);

  // All Evil roles except Scarlet Woman & Imp propagate from the previous day:
  night_roles.PropagateRolesFrom(day_roles_.back(), {POISONER, SPY, BARON}, players_, prev_day_name, night_name);
  
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
  vector<BoolVar> poisoner_picks;
  for (int i = 0; i < num_players_; ++i) {
    string name = absl::StrFormat("poisoner_picks_%s_night_%d", players_[i],
                                  cur_time_.Count);
    poisoner_picks.push_back(model_.NewBoolVar().WithName(name));
  }
  poisoner_pick_.push_back(poisoner_picks);
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
  const string prev_day_name = absl::StrFormat("day_%d", cur_time_.Count - 1);
  const string night_name = absl::StrFormat("night_%d", cur_time_.Count);
  if (num_alive_ < 5 ||
      (execution_death_ == kNoPlayer && slayer_death_ == kNoPlayer)) {
    // Then we know for sure SW can't trigger.
    PropagateRoles(day_roles, night_roles, {SCARLET_WOMAN, IMP}, prev_day_name,
                   night_name);
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
    const string name = absl::StrFormat("sw_trigger_%s_%d", prev_day_name, i);
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

void GameState::AddStorytellerInteraction(
    const StorytellerInteraction& interaction) {
  auto it = player_index_.find(interaction.player());
  CHECK(it != player_index_.end())
      << "Invalid player " << interaction.player();
  const int player = it->second;
  switch (interaction.details_case()) {
    case StorytellerInteraction::kShownToken:
      AddShownToken(player, interaction.shown_token());
      break;
    case StorytellerInteraction::kMinionInfo:
      AddMinionInfo(player, interaction.minion_info());
      break;
    case StorytellerInteraction::kDemonInfo:
      AddDemonInfo(player, interaction.demon_info());
      break;
    case StorytellerInteraction::kRoleAction:
      AddRoleAction(player, interaction.role_action());
      break;
    default:
      CHECK(false) << "Expected a valid interaction details, got: "
                   << interaction.details_case();
  }
}

void GameState::AddNomination(const Nomination& nomination) {
  CHECK(cur_time_.IsDay) << "Nominations can only occur during the day.";
  auto it = player_index_.find(nomination.nominator());
  CHECK(it != player_index_.end())
      << "Invalid nominator " << nomination.nominator();
  const int nominator = it->second;
  it = player_index_.find(nomination.nominee());
  CHECK(it != player_index_.end())
      << "Invalid nominee " << nomination.nominee();
  const int nominee = it->second;
  CHECK(is_alive_[nominator])
      << players_[nominator] << " is dead and cannot nominate.";
  for (const auto& nom : nominations_) {
    CHECK_NE(nom.Nominator, nominator)
      << players_[nominator] << " has already nominated today.";
    CHECK_NE(nom.Nominee, nominee)
        << players_[nominee] << " has already been nominated today.";
  }
  nominations_.push_back({.Nominator = nominator, .Nominee = nominee});
}

void GameState::AddVote(const Vote& vote) {
  CHECK(!nominations_.empty()) << "A vote must have a preceding nomination.";
  const auto& nomination = nominations_.back();
  // TODO(olaola): validate vote correctness better!
  for (const string& name : vote.votes()) {
    const auto it = player_index_.find(name);
    CHECK(it != player_index_.end()) << "Invalid voter " << name;
  }
  int cur_block = kNoPlayer;
  if (!vote.on_the_block().empty()) {
    const auto it = player_index_.find(vote.on_the_block());
    CHECK(it != player_index_.end()) << "Invalid execution candidate "
                                     << vote.on_the_block();
    cur_block = it->second;
  }
  int cur_votes = vote.votes_size();
  if (on_the_block_ == kNoPlayer) {
    int votes_required = num_votes_ == 0 ? num_alive_ / 2 : num_votes_ + 1;
    if (cur_votes < votes_required) {
      // Vote fails, nothing changed.
      CHECK_EQ(cur_block, on_the_block_)
          << absl::StrFormat("Needed %d votes to put %s on the block, got %d",
                             votes_required, players_[cur_block], cur_votes);
    } else {
      // Vote succeeds.
      num_votes_ = cur_votes;
      CHECK_EQ(cur_block, nomination.Nominee)
          << absl::StrFormat("%s expected to go on the block, got: %s",
                             players_[nomination.Nominee], vote.on_the_block());
    }
  } else {
    if (cur_votes < num_votes_) {
      // Vote fails, nothing changed.
      CHECK_EQ(cur_block, on_the_block_)
          << absl::StrFormat("Needed %d votes to put %s on the block, got %d",
                             num_votes_ + 1, players_[cur_block], cur_votes);
    } else if (cur_votes == num_votes_) {
      // Tied vote, no one on the block.
      CHECK_EQ(cur_block, kNoPlayer)
          << absl::StrFormat("Tied vote, no one goes on the block, got: %s",
                             vote.on_the_block());
    } else {
      // Vote succeeds.
      num_votes_ = cur_votes;
      CHECK_EQ(cur_block, nomination.Nominee)
          << absl::StrFormat("%s expected to go on the block, got: %s",
                             players_[nomination.Nominee], vote.on_the_block());
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
  const auto it = player_index_.find(name);
  CHECK(it != player_index_.end()) << "Invalid player " << name;
  const int executee = it->second;
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
  const auto it = player_index_.find(name);
  CHECK(it != player_index_.end()) << "Invalid player " << name;
  const int death = it->second;
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
    // Slayer procs, so we know they are alive, not poisoned, and shot the Imp:
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

void GameState::AddClaim(const Claim& claim) {
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

void GameState::AddShownToken(int player, Role role) {
}

void GameState::AddMinionInfo(int player, const MinionInfo& minion_info) {
}

void GameState::AddDemonInfo(int player, const DemonInfo& demon_info) {
}

void GameState::AddRoleAction(int player, const RoleAction& role_action) {
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
      AddFortunetellerAction(player, role_action.fortuneteller_action());
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
    int player, const LearnRoleInfo& washerwoman_info) {
}

void GameState::AddLibrarianInfo(
    int player, const LearnRoleInfo& librarian_info) {
}

void GameState::AddInvestigatorInfo(
    int player, const LearnRoleInfo& investigator_info) {
}

void GameState::AddChefInfo(int player, int chef_info) {
}

void GameState::AddEmpathInfo(int player, int empath_info) {
}

void GameState::AddFortunetellerAction(
    int player, const FortuneTellerAction& fortuneteller_action) {
}

void GameState::AddMonkAction(int player, string monk_action) {
}

void GameState::AddButlerAction(int player, string butler_action) {
}

void GameState::AddRavenkeeperInfo(
    int player, const RavenkeeperInfo& ravenkeeper_info) {
}

void GameState::AddUndertakerInfo(int player, Role undertaker_info) {
}

void GameState::AddSlayerAction(int player, string slayer_action) {
}

void GameState::AddPoisonerAction(int player, string poisoner_action) {
}

void GameState::AddImpAction(int player, string imp_action) {
}

void GameState::AddSpyInfo(int player, const SpyInfo& spy_info) {
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
  // This is at the start of a night. It means !(GOOD won) && !(EVIL won).
  //
  // Good wins iff there is no alive Imp.
  //
  // Evil wins iff either:
  // * A non-poisoned Saint was executed, OR
  // * 2 players are alive, one of them the Imp.
  if (num_alive_ <= 2) {
    const string name = absl::StrFormat(
        "Contradiction: %d players alive on night %d, yet game is not over",
        num_alive_, cur_time_.Count);
    model_.AddBoolOr({model_.FalseVar()}).WithName(name);
    return;
  }
  const auto& night_roles = night_roles_.back();
  vector<BoolVar> alive_imp = CollectAliveRoles(night_roles, {IMP});
  model_.AddExactlyOne(alive_imp).WithName(absl::StrFormat(
      "Good did not win -> an Imp is alive on night %d", cur_time_.Count));
  if (execution_death_ != kNoPlayer) {
    BoolVar poisoned = poisoner_pick_[cur_time_.Count - 2][execution_death_];
    const string name = absl::StrFormat(
        "Evil did not win day %d -> %s is not the SAINT or %s was poisoned",
        cur_time_.Count - 1, players_[execution_death_],
        players_[execution_death_]);
    model_.AddBoolOr({night_roles[execution_death_][SAINT].Not(), poisoned})
        .WithName(name);
  }
}

bool GameState::IsValid() const {
  const CpSolverResponse response = Solve(model_.Build());
  LOG(INFO) << "response: " << response.DebugString();
  CHECK(response.status() != CpSolverStatus::MODEL_INVALID);
  CHECK(response.status() != CpSolverStatus::UNKNOWN);
  return (response.status() == CpSolverStatus::OPTIMAL || response.status() == CpSolverStatus::FEASIBLE);
}
}  // namespace botc
