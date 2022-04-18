#include "src/game.h"

namespace botc {

using operations_research::DecisionBuilder;

namespace {
  const int kNumTownsfolk[] = {3, 3, 5, 5, 5, 7, 7, 7, 9, 9, 9};
  const int kNumOutsiders[] = {0, 1, 0, 1, 2, 0, 1, 2, 0, 1, 2};
  const int kNumMinions[] = {1, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3};
}

GameState::GameState(Perspective perspective, const Setup& setup):
    perspective_(perspective),solver_("Solver"),player_role_vars_(setup.players().size()) {
    for (const auto& name : setup.players()) {
        players_.push_back(name);
    }
    num_players_ = players_.size();
    CHECK_GE(num_players_, 5) << "Need at least 5 players.";
    CHECK_LE(num_players_, 15) << "Need at most 15 players.";
    // Verify expected number of minions, townsfolk and outsiders.
    int expected_townsfolk = kNumTownsfolk[num_players_-5];
    int expected_outsiders = kNumOutsiders[num_players_-5];
    int expected_minions = kNumMinions[num_players_-5];
    num_outsiders_ = setup.num_outsiders();
    num_minions_ = setup.num_minions();
    num_townsfolk_ = num_players_ - num_outsiders_ - num_minions_ - 1;
    string err_msg = absl::StrFormat("Expected values for %d players "
        " <townsfolk, outsiders, minions, demons>: <%d,%d,%d,1>, got: <%d,%d,%d,1>.",
        num_players_, expected_townsfolk, expected_outsiders,
        expected_minions, num_townsfolk_, num_outsiders_, num_minions_);
    CHECK(expected_townsfolk == num_townsfolk_ &&
        expected_outsiders == num_outsiders_ &&
        expected_minions == num_minions_) << err_msg;
    if (perspective_ == STORYTELLER) {
        CHECK_EQ(setup.player_roles_size(), num_players_) <<
            "Expected fully assigned player roles in storyteller perspective";
    } else {
        // Check we don't have unexpected info. If we do, it is not necessarily a mistake,
        // but most likely is one.
        if (setup.player_roles_size() > 0) {
            LOG(WARNING) << "Player roles present in a non-storyteller perspective.";
        }
        if (!setup.red_herring().empty()) {
            LOG(WARNING) << "Red-herring info present in a non-storyteller perspective.";
        }
    }
}

void GameState::InitRoleVarsConstraints() {
    // player_role_vars_[i][role] is true iff player players_[i] has role+1.
    for (int i = 0; i < num_players_; ++i) {
        for (int role = WASHERWOMAN; role <= IMP; ++role) {
            player_role_vars_[i].push_back(solver_.MakeBoolVar());
        }
    }
    // Each player assigned exactly one role:
    for (int i = 0; i < num_players_; ++i) {
        solver_.AddConstraint(solver_.MakeSumEquality(player_role_vars_[i], 1));
    }
    // There is exactly one IMP:
    vector<IntVar*> player_is_imp;
    for (int i = 0; i < num_players_; ++i) {
        player_is_imp[i] = player_role_vars_[i][IMP-1];
    }
    solver_.AddConstraint(solver_.MakeSumEquality(player_is_imp, 1));
    // Each role other than IMP assigned to at most one player:
    for (int role = WASHERWOMAN; role < IMP; ++role) {
        vector<IntVar*> player_is_role;
        for (int i = 0; i < num_players_; ++i) {
            player_is_role[i] = player_role_vars_[i][role-1];
        }
        solver_.AddConstraint(solver_.MakeSumLessOrEqual(player_is_role, 1));
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
    // TODO
}

GameLog GameState::ToProto() const {
    // TODO
    GameLog g;
    return g;
}

void SimpleCpProgram() {
  // Instantiate the solver.
  // [START solver]
  Solver solver("CpSimple");
  // [END solver]

  // Create the variables.
  // [START variables]
  const int64_t num_vals = 3;
  IntVar* const x = solver.MakeIntVar(0, num_vals - 1, "x");
  IntVar* const y = solver.MakeIntVar(0, num_vals - 1, "y");
  IntVar* const z = solver.MakeIntVar(0, num_vals - 1, "z");
  // [END variables]

  // Constraint 0: x != y..
  // [START constraints]
  solver.AddConstraint(solver.MakeAllDifferent({x, y}));
  LOG(INFO) << "Number of constraints: "
            << std::to_string(solver.constraints());
  // [END constraints]

  // Solve the problem.
  // [START solve]
  DecisionBuilder* const db = solver.MakePhase(
      {x, y, z}, Solver::CHOOSE_FIRST_UNBOUND, Solver::ASSIGN_MIN_VALUE);
  // [END solve]

  // Print solution on console.
  // [START print_solution]
  int count = 0;
  solver.NewSearch(db);
  while (solver.NextSolution()) {
    ++count;
    LOG(INFO) << "Solution " << count << ":" << std::endl
              << " x=" << x->Value() << " y=" << y->Value()
              << " z=" << z->Value();
  }
  solver.EndSearch();
  LOG(INFO) << "Number of solutions found: " << solver.solutions();
  // [END print_solution]

  // [START advanced]
  LOG(INFO) << "Advanced usage:" << std::endl
            << "Problem solved in " << std::to_string(solver.wall_time())
            << "ms" << std::endl
            << "Memory usage: " << std::to_string(Solver::MemoryUsage())
            << "bytes";
  // [END advanced]
}

}  // namespace botc
