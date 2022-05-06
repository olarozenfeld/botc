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

#ifndef SRC_MODEL_WRAPPER_H_
#define SRC_MODEL_WRAPPER_H_

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "ortools/sat/cp_model.h"

namespace botc {

using operations_research::sat::CpModelBuilder;
using operations_research::sat::BoolVar;
using std::string;
using std::vector;
using std::map;
using std::unordered_map;
using std::unordered_set;

vector<BoolVar> Not(absl::Span<const BoolVar> literals);

// A convenience wrapper over CpModelBuilder.
class ModelWrapper {
 public:
  const CpModelBuilder& Model() const { return model_; }
  void WriteToFile(const string& filename) const;
  void WriteVariablesToFile(const string& filename) const;
  BoolVar NewVar(const string& name);
  BoolVar FalseVar() { return model_.FalseVar().WithName("0"); }
  BoolVar TrueVar() { return model_.TrueVar().WithName("1"); }
  void FixVariable(const BoolVar& var, bool val);
  void AddAnd(absl::Span<const BoolVar> literals);
  void AddOr(absl::Span<const BoolVar> literals);
  void AddEquality(const BoolVar& var, bool val) {
    AddEquality(var, val ? TrueVar() : FalseVar());
  }
  void AddEquality(const BoolVar& v1, const BoolVar& v2);
  void AddImplication(const BoolVar& v1, const BoolVar& v2);
  void AddImplicationAnd(const BoolVar& var,
                         absl::Span<const BoolVar> literals);
  void AddImplicationOr(const BoolVar& var,
                        absl::Span<const BoolVar> literals);
  void AddImplicationEq(const BoolVar& var,  // var -> left = right
                        const BoolVar& left,
                        const BoolVar& right);
  void AddImplicationSum(const BoolVar& var,
                         absl::Span<const BoolVar> literals, int sum);
  void AddEquivalenceAnd(const BoolVar& var,
                         absl::Span<const BoolVar> literals);
  void AddEquivalenceOr(const BoolVar& var,
                        absl::Span<const BoolVar> literals);
  void AddEquivalenceSum(const BoolVar& var,  // var = Sum(literals)
                         absl::Span<const BoolVar> literals);
  void AddEquivalenceSumEq(const BoolVar& var,
                           absl::Span<const BoolVar> literals,
                           int sum);
  void AddEqualitySum(absl::Span<const BoolVar> literals, int sum);
  void AddAtMostOne(absl::Span<const BoolVar> literals);
  void AddContradiction(const string& reason);
  BoolVar CreateEquivalentVarAnd(absl::Span<const BoolVar> literals,
                                 const string& name);
  BoolVar CreateEquivalentVarOr(absl::Span<const BoolVar> literals,
                                const string& name);
  BoolVar CreateEquivalentVarSum(absl::Span<const BoolVar> literals,
                                 const string& name);
  BoolVar CreateEquivalentVarSumEq(absl::Span<const BoolVar> literals, int sum,
                                   const string& name);

 private:
  CpModelBuilder model_;
  unordered_map<string, BoolVar> var_cache_;  // To prevent duplicate variables
  unordered_set<string> constraint_cache_;    // and constraints.
};

}  // namespace botc

#endif  // SRC_MODEL_WRAPPER_H_
