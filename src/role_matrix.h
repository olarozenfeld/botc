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

#ifndef SRC_ROLE_MATRIX_H_
#define SRC_ROLE_MATRIX_H_

#include <optional>
#include <string>
#include <vector>

#include "absl/strings/str_format.h"
#include "src/game_log.pb.h"
#include "ortools/sat/cp_model.h"

namespace botc {

using operations_research::sat::CpModelBuilder;
using operations_research::sat::BoolVar;
using std::string;
using std::vector;
using operations_research::sat::Constraint;
using operations_research::sat::LinearExpr;

class RoleMatrix {
private:
  CpModelBuilder &model_;
  int role_min_, role_max_;

  // role_matrix_[player][role] is a BoolVar that depicts that Player "player" is role "role"
  // for invalid roles, contains FalseVar;
  vector<vector<BoolVar>> role_matrix_;

  // role_in_play[role] contains a BoolVar that is equivalent to at least one
  //   player having role "role".
  // variables are created lazily, the ones that were not requested are NULL
  vector<std::optional<BoolVar>> role_in_play_;

  // is_evil[player] is a BoolVar that is equivalent to the player having one
  // of the evil roles
  vector<BoolVar> is_evil_;

  // INTERNAL HELPER METHODS
  int num_players() {
    return role_matrix_.size();
  }

  // add to a vector all role_matrix_[X][role] for all players X
  void addRoleVars(int role, vector<BoolVar> &v) {
    for(int p = 0; p < num_players(); p++){
      v.push_back(role_matrix_[p][role]);
    }
  }

  // get a vector of all role_matrix_[X][role] for all players X
  vector<BoolVar> roleVars(int role) {
    vector<BoolVar> result;
    addRoleVars(role, result);
    return result;
  }


public:

  RoleMatrix(CpModelBuilder &model, vector<string> &players, int role_min, int role_max, int night_count) 
      : model_(model), role_min_(role_min), role_max_(role_max),
        role_matrix_(players.size(), vector<BoolVar>(role_max + 1, model_.FalseVar())) ,
        role_in_play_(role_max+1) {
    
    // Create BoolVars for "player is role"
    for(int i = 0; i < players.size(); i++) {
      role_matrix_[i].resize(role_max+1);
      for(int role = role_min; role <= role_max; role++) {
        string name = absl::StrFormat(
          "role_%s_%s_night_%d", players[i], Role_Name(role), night_count);
        role_matrix_[i][role] = model_.NewBoolVar().WithName(name);
      }
    }

    // Each player assigned exactly one role:
    for (int i = 0; i < players.size(); ++i) {
      string name = absl::StrFormat("player %s has unique role", players[i]);
      model_.AddExactlyOne(role_matrix_[i]).WithName(name);
    }

    // Each role is assigned at most once
    for(int role = role_min_; role <= role_max; role++){
      vector<BoolVar> role_vars = roleVars(role);
      
      string name = absl::StrFormat(
        "Role %s has at most one player", Role_Name(role));
      model_.AddAtMostOne(role_vars).WithName(name);
    }
  }

  template <size_t N>
  void InitIsEvil(const Role (&evil_roles)[], const vector<string> &players) {      
    // Initialize is_evil_
    for(int p = 0; p < players.size(); p++) {
      BoolVar is_evil = model_.NewBoolVar()
        .WithName(absl::StrFormat("Evil_%s", players[p]));
      is_evil_.push_back(is_evil);

      vector<BoolVar> evil_role_vars;
      for(int role : evil_roles) {
        evil_role_vars.push_back(role_matrix_[p][role]);
      }
      model_.AddEquality(LinearExpr::Sum(evil_role_vars), is_evil)
        .WithName(absl::StrFormat("Evil_%s definition", players[p]));
    }
  }

  // Returns a BoolVar that corresponds to "at least one player has the given role"
  BoolVar RoleInPlay(int role) {
    if (!role_in_play_[role].has_value()) {
      string name = absl::StrFormat("in_play_%s", Role_Name(role));
      BoolVar v = model_.NewBoolVar().WithName(name);

      model_.AddEquality(LinearExpr::Sum(roleVars(role)), v)
        .WithName(absl::StrFormat("%s definition", name));

      role_in_play_[role].emplace(v);
    }
    return role_in_play_[role].value();
  }

  // Ensure that there's exactly "count" of roles listed in the array roles.
  // Returns the resulting constraint that can be used for further customization,
  //   such as adding a name or an EnforceIfClause
  template <size_t N>
  Constraint enforceRoleCount(const Role (&roles)[N], int count) {
    vector<BoolVar> vars;
    for(int role : roles) {
      addRoleVars(role, vars);
    }
    return model_.AddEquality(LinearExpr::Sum(vars), count);
  }
 
  void FixRole(int player, int role) {
    model_.FixVariable(role_matrix_[player][role], 1);
    // Can set others to false here but they should be set by propagation
  }

  void PropagateRolesFrom(const RoleMatrix &previous, absl::Span<const Role> roles,
                                vector<string> players,
                                const string& from_name,
                                const string& to_name) {
    for(int i = 0; i < num_players(); i++) {
      for( int role : roles) {
        string name = absl::StrFormat(
          "Role %s for player %s propagates from %s to %s", Role_Name(role),
          players[i], from_name, to_name);
        model_.AddEquality(previous.role_matrix_[i][role], role_matrix_[i][role])
            .WithName(name);
      }
    }
  }
};

} // namespace botc

#endif  // SRC_ROLE_MATRIX_H_