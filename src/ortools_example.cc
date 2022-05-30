// Copyright 2010-2021 Google LLC
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

#include <stdlib.h>

#include <filesystem>
#include <iostream>
#include <memory>

#include "absl/flags/flag.h"
#include "absl/types/span.h"
#include "ortools/base/integral_types.h"
#include "ortools/base/logging.h"
#include "ortools/sat/cp_model.h"
#include "ortools/sat/cp_model.pb.h"
#include "ortools/sat/cp_model_solver.h"
#include "ortools/sat/model.h"
#include "ortools/sat/sat_parameters.pb.h"
#include "ortools/util/sorted_interval_list.h"
#include "src/util.h"

ABSL_FLAG(std::string, model, "", "Model proto file path.");

namespace operations_research {
namespace sat {

using std::filesystem::path;
using std::string;

void RabbitsAndPheasantsSat() {
  CpModelBuilder cp_model;

  const Domain all_animals(0, 20);
  const IntVar rabbits = cp_model.NewIntVar(all_animals).WithName("rabbits");
  const IntVar pheasants =
      cp_model.NewIntVar(all_animals).WithName("pheasants");

  cp_model.AddEquality(rabbits + pheasants, 20);
  cp_model.AddEquality(4 * rabbits + 2 * pheasants, 56);

  const CpSolverResponse response = Solve(cp_model.Build());

  if (response.status() == CpSolverStatus::OPTIMAL) {
    // Get the value of x in the solution.
    LOG(INFO) << SolutionIntegerValue(response, rabbits) << " rabbits, and "
              << SolutionIntegerValue(response, pheasants) << " pheasants";
  }
}

void SimpleSat() {
  CpModelBuilder cp_model;

  BoolVar x = cp_model.NewBoolVar();
  BoolVar y = cp_model.NewBoolVar();
  cp_model.AddEquality(x, y);

  const CpSolverResponse response = Solve(cp_model.Build());

  if (response.status() == CpSolverStatus::OPTIMAL) {
    // Get the value of x in the solution.
    LOG(INFO) << "x=" << SolutionIntegerValue(response, x) << ", y="
              << SolutionIntegerValue(response, y);
  }
}

void SolveFromFilename(path filename) {
  CpModelBuilder cp_model;
  CpModelProto pb;
  botc::ReadProtoFromFile(filename, &pb);
  cp_model.CopyFrom(pb);

  Model model;
  SatParameters parameters;
  parameters.set_enumerate_all_solutions(true);
  model.Add(NewSatParameters(parameters));
  int solutions = 0;
  model.Add(NewFeasibleSolutionObserver([&](const CpSolverResponse& r) {
    LOG(INFO) << "Found solution " << ++solutions;
    for (int i = 0; i < r.solution_size(); ++i) {
      IntVar v = cp_model.GetIntVarFromProtoIndex(i);
      if (v.Name() != "1" && SolutionIntegerValue(r, v) == 1) {
        LOG(INFO) << v.Name();
      }
    }
  }));
  SolveCpModel(cp_model.Build(), &model);
  LOG(INFO) << "Solutions: " << solutions;
}

void SimpleSat2() {
  CpModelBuilder cp_model;

  BoolVar x = cp_model.NewBoolVar();
  BoolVar y = cp_model.NewBoolVar();
  cp_model.AddEquality(x, y);
  BoolVar z = cp_model.NewBoolVar();
  BoolVar t = cp_model.NewBoolVar();
  cp_model.AddEquality(z, t);

  // Search for x values in increasing order.
//  cp_model.AddDecisionStrategy({x}, DecisionStrategyProto::CHOOSE_FIRST,
//                               DecisionStrategyProto::SELECT_MIN_VALUE);

  // Create a solver and solve with a fixed search.
  Model model;
  SatParameters parameters;
//  parameters.set_search_branching(SatParameters::FIXED_SEARCH);
  parameters.set_enumerate_all_solutions(true);
//  parameters.set_instantiate_all_variables(false);
  model.Add(NewSatParameters(parameters));
  model.Add(NewFeasibleSolutionObserver([&](const CpSolverResponse& r) {
    LOG(INFO) << "x=" << SolutionIntegerValue(r, x) << " y="
              << SolutionIntegerValue(r, y) << " response: "
              << r.DebugString();
  }));
  SolveCpModel(cp_model.Build(), &model);
}

void StepFunctionSampleSat() {
  // Create the CP-SAT model.
  CpModelBuilder cp_model;

  // Declare our primary variable.
  const IntVar x = cp_model.NewIntVar({0, 20});

  // Create the expression variable and implement the step function
  // Note it is not defined for var == 2.
  //
  //        -               3
  // -- --      ---------   2
  //                        1
  //      -- ---            0
  // 0 ================ 20
  //
  IntVar expr = cp_model.NewIntVar({0, 3});

  // expr == 0 on [5, 6] U [8, 10]
  BoolVar b0 = cp_model.NewBoolVar();
  cp_model.AddLinearConstraint(x, Domain::FromValues({5, 6, 8, 9, 10}))
      .OnlyEnforceIf(b0);
  cp_model.AddEquality(expr, 0).OnlyEnforceIf(b0);

  // expr == 2 on [0, 1] U [3, 4] U [11, 20]
  BoolVar b2 = cp_model.NewBoolVar();
  cp_model
      .AddLinearConstraint(x, Domain::FromIntervals({{0, 1}, {3, 4}, {11, 20}}))
      .OnlyEnforceIf(b2);
  cp_model.AddEquality(expr, 2).OnlyEnforceIf(b2);

  // expr == 3 when x = 7
  BoolVar b3 = cp_model.NewBoolVar();
  cp_model.AddEquality(x, 7).OnlyEnforceIf(b3);
  cp_model.AddEquality(expr, 3).OnlyEnforceIf(b3);

  // At least one bi is true. (we could use an exactly one constraint).
  cp_model.AddBoolOr({b0, b2, b3});

  // Search for x values in increasing order.
  cp_model.AddDecisionStrategy({x}, DecisionStrategyProto::CHOOSE_FIRST,
                               DecisionStrategyProto::SELECT_MIN_VALUE);

  // Create a solver and solve with a fixed search.
  Model model;
  SatParameters parameters;
  parameters.set_search_branching(SatParameters::FIXED_SEARCH);
  parameters.set_enumerate_all_solutions(true);
  model.Add(NewSatParameters(parameters));
  model.Add(NewFeasibleSolutionObserver([&](const CpSolverResponse& r) {
    LOG(INFO) << "x=" << SolutionIntegerValue(r, x) << " expr"
              << SolutionIntegerValue(r, expr) << " response: "
              << r.DebugString();
  }));
  SolveCpModel(cp_model.Build(), &model);
}

}  // namespace sat
}  // namespace operations_research

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  operations_research::sat::SolveFromFilename(absl::GetFlag(FLAGS_model));
  return EXIT_SUCCESS;
}
