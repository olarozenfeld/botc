#include "src/game.h"

#include "ortools/constraint_solver/constraint_solver.h"

namespace botc {

using operations_research::DecisionBuilder;
using operations_research::IntVar;
using operations_research::Solver;

GameState::GameState(Perspective perspective, const Setup& setup):perspective_(perspective){
    // TODO
}

GameState::GameState(const GameLog& game_log) {
    // TODO
}

void GameState::AddEvent(const Event& event) {
    // TODO
}

void GameState::RemoveLastEvent() {
    // TODO
}

GameLog ToProto() const {
    // TODO
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
