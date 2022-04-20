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

const int kNoPlayer = -1;  // Used in place of player index.

using operations_research::sat::CpSolverResponse;
using operations_research::sat::CpSolverStatus;
using operations_research::sat::LinearExpr;

namespace {
  // Will be useful for travelers.
  // const int kNumTownsfolk[] = {3, 3, 5, 5, 5, 7, 7, 7, 9, 9, 9};
  const int kNumOutsiders[] = {0, 1, 0, 1, 2, 0, 1, 2, 0, 1, 2};
  const int kNumMinions[] = {1, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3};
}

GameState::GameState(Perspective perspective, const Setup& setup)
    : perspective_(perspective), num_players_(setup.players_size()),
      is_alive_(num_players_, true), player_roles_(num_players_) {
  CHECK_GE(num_players_, 5);
  CHECK_LE(num_players_, 15);
  num_outsiders_ = kNumOutsiders[num_players_-5];
  num_minions_ = kNumMinions[num_players_-5];
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
    // Check we don't have unexpected info. If we do, it is not necessarily a
    // mistake, but most likely is one.
    if (setup.player_roles_size() > 0) {
        LOG(WARNING) << "Player roles assigned in non-storyteller perspective.";
    }
    if (!setup.red_herring().empty()) {
        LOG(WARNING) << "Red-herring info in non-storyteller perspective.";
    }
  }
  for (const auto& player_role : setup.player_roles()) {
    const string& name = player_role.player();
    CHECK_NE(player_role.role(), ROLE_UNSPECIFIED)
        << "Got unassigned role for player " << name;
    const auto it = player_index_.find(name);
    CHECK(it != player_index_.end()) << "Invalid player name: " << name;
    player_roles_[it->second] = player_role.role();
  }

  red_herring_ = kNoPlayer;
  if (!setup.red_herring().empty()) {
    const string& name = setup.red_herring();
    const auto it = player_index_.find(name);
    CHECK(it != player_index_.end()) << "Invalid red herring player: " << name;
    red_herring_ = it->second;
  }

  SetupRoleVars();
  SetupHelperVars();
}

void GameState::SetupRoleVars() {
    // night1_roles[i][role] is true iff player players_[i] has role+1.
    vector<vector<BoolVar>> night1_roles(num_players_);
    for (int i = 0; i < num_players_; ++i) {
        for (int role = WASHERWOMAN; role <= IMP; ++role) {
            // Variable/constraint names are for debugging only.
            string name = absl::StrFormat(
              "N1_role_%s_%s", players_[i], Role_Name(role));
            night1_roles[i].push_back(model_.NewBoolVar().WithName(name));
        }
    }
    // Fix all specified roles:
    for (int i = 0; i < num_players_; ++i) {
        Role role = player_roles_[i];
        if (role != ROLE_UNSPECIFIED) {
            const auto& pr = night1_roles[i];
            // We don't need to fix all of them, but it will be faster.
            for (int role1 = WASHERWOMAN; role1 <= IMP; ++role1) {
                model_.FixVariable(pr[role1-1], role1 == role);
            }
        }
    }
    // Each player assigned exactly one role:
    for (int i = 0; i < num_players_; ++i) {
        string name = absl::StrFormat("player %s has unique role", players_[i]);
        model_.AddExactlyOne(night1_roles[i]).WithName(name);
    }
    // There is exactly one IMP:
    vector<BoolVar> player_is_imp(num_players_);
    for (int i = 0; i < num_players_; ++i) {
        player_is_imp[i] = night1_roles[i][IMP-1];
    }
    model_.AddExactlyOne(player_is_imp).WithName("Exactly 1 IMP");
    // Each role other than IMP assigned to at most one player:
    for (int role = WASHERWOMAN; role < IMP; ++role) {
        vector<BoolVar> player_is_role(num_players_);
        for (int i = 0; i < num_players_; ++i) {
            player_is_role[i] = night1_roles[i][role-1];
        }
        string name = absl::StrFormat(
          "Role %s has at most one player", Role_Name(role));
        model_.AddAtMostOne(player_is_role).WithName(name);
    }
    // Appropriate numbers of outsiders, townsfolk and minions:
    BoolVar baron_in_play = model_.NewBoolVar().WithName("baron_in_play");
    vector<BoolVar> player_is_baron(num_players_);
    for (int i = 0; i < num_players_; ++i) {
        player_is_baron[i] = night1_roles[i][BARON-1];
    }
    // If roles are known, the baron_in_play variable can be fixed:
    if (player_roles_.size() == num_players_) {
        bool have_baron = false;
        for (Role role : player_roles_) {
            if (role == BARON) {
                have_baron = true;
            }
        }
        model_.FixVariable(baron_in_play, have_baron);
    }
    model_.AddEquality(LinearExpr::Sum(player_is_baron), baron_in_play)
        .WithName("baron_in_play definition");
    vector<BoolVar> outsiders;
    for (int i = 0; i < num_players_; ++i) {
        for (int role = BUTLER; role <= SAINT; ++role) {
            outsiders.push_back(night1_roles[i][role-1]);
        }
    }
    model_.AddEquality(LinearExpr::Sum(outsiders), num_outsiders_)
        .OnlyEnforceIf(baron_in_play.Not())
        .WithName(
          absl::StrFormat("!baron_in_play -> %d outsiders", num_outsiders_));
    model_.AddEquality(LinearExpr::Sum(outsiders), num_outsiders_+2)
        .OnlyEnforceIf(baron_in_play)
        .WithName(
          absl::StrFormat("baron_in_play -> %d outsiders", num_outsiders_+2));
    vector<BoolVar> minions;
    for (int i = 0; i < num_players_; ++i) {
        for (int role = POISONER; role <= BARON; ++role) {
            minions.push_back(night1_roles[i][role-1]);
        }
    }
    model_.AddEquality(LinearExpr::Sum(minions), num_minions_)
        .WithName(absl::StrFormat("Exactly %d minions", num_minions_));
    // Since we don't support travelers yet, no need to compute townsfolk.
    player_roles_night_.push_back(night1_roles);
}

void GameState::SetupHelperVars() {
    // A Player is Evil iff they have an Evil role on Night 1 (in TB).
    const vector<vector<BoolVar>>& night1_roles = player_roles_night_[0];
    for (int i = 0; i < num_players_; ++i) {
        BoolVar is_evil = model_.NewBoolVar()
            .WithName(absl::StrFormat("Evil_%s", players_[i]));
        player_is_evil_.push_back(is_evil);
        vector<BoolVar> evil_roles;
        for (int role = POISONER; role <= IMP; ++role) {
            evil_roles.push_back(night1_roles[i][role-1]);
        }
        model_.AddEquality(LinearExpr::Sum(evil_roles), is_evil)
            .WithName(absl::StrFormat("Evil_%s definition", players_[i]));
    }
}

GameState::GameState(const GameLog& game_log):
    GameState(game_log.perspective(), game_log.setup()) {
    for (const auto& event : game_log.events()) {
        AddEvent(event);
    }
}

void GameState::AddEvent(const Event& event) {
    events_.push_back(event);
    // TBD
}

GameLog GameState::ToProto() const {
    // TBD
    GameLog g;
    return g;
}

bool GameState::IsValid() const {
  const CpSolverResponse response = Solve(model_.Build());
  LOG(INFO) << "response: " << response.DebugString();
  return (response.status() == CpSolverStatus::OPTIMAL);
}
}  // namespace botc
