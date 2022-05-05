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

#include "src/model_wrapper.h"

#include <algorithm>
#include <fstream>
#include <iostream>

#include "ortools/sat/cp_model.h"
#include "src/util.h"

namespace botc {
using operations_research::sat::LinearExpr;
using std::ofstream;

vector<BoolVar> Not(absl::Span<const BoolVar> literals) {
  vector<BoolVar> result;
  for (const auto& v : literals) {
    result.push_back(Not(v));
  }
  return result;
}

namespace {
string ConstraintName(const string& separator,
                      absl::Span<const BoolVar> literals) {
  if (literals.size() == 0) {
    return "0";
  }
  vector<string> names;
  for (const BoolVar& v : literals) {
    names.push_back(v.Name());
  }
  sort(names.begin(), names.end());
  string name = names[0];
  for (int i = 1; i < names.size(); ++i) {
    absl::StrAppend(&name, absl::StrFormat(" %s %s", separator, names[i]));
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

void ModelWrapper::WriteToFile(const string& filename) const {
  WriteProtoToFile(filename, model_.Build());
}

void ModelWrapper::WriteVariablesToFile(const string& filename) const {
  const auto& model_pb = model_.Build();
  ofstream f;
  f.open(filename);
    for (int i = 0; i < model_pb.variables_size(); ++i) {
      f << i << ": " << VarDebugString(model_pb, i) << "\n";
    }
  f.close();
}

BoolVar ModelWrapper::NewVar(const string& name) {
  const auto it = var_cache_.find(name);
  if (it != var_cache_.end()) {
    return it->second;
  }
  BoolVar v = model_.NewBoolVar().WithName(name);
  var_cache_[name] = v;
  return v;
}

void ModelWrapper::FixVariable(const BoolVar& var, bool val) {
  model_.FixVariable(var, val);
}

void ModelWrapper::AddAnd(absl::Span<const BoolVar> literals) {
  const string name = AndConstraintName(literals);
  if (!constraint_cache_.contains(name)) {
    model_.AddBoolAnd(literals).WithName(name);
    constraint_cache_.insert(name);
  }
}

void ModelWrapper::AddOr(absl::Span<const BoolVar> literals) {
  const string name = OrConstraintName(literals);
  if (!constraint_cache_.contains(name)) {
    model_.AddBoolOr(literals).WithName(name);
    constraint_cache_.insert(name);
  }
}

void ModelWrapper::AddEquality(const BoolVar& v1, const BoolVar& v2) {
  const bool less = v1.Name() < v2.Name();
  const BoolVar& left = less ? v1 : v2;
  const BoolVar& right = less ? v2 : v1;
  const string name = absl::StrFormat("%s = %s", left.Name(), right.Name());
  if (!constraint_cache_.contains(name)) {
    model_.AddEquality(left, right).WithName(name);
    constraint_cache_.insert(name);
  }
}

void ModelWrapper::AddImplication(const BoolVar& v1, const BoolVar& v2) {
  const string name = absl::StrFormat("%s -> %s", v1.Name(), v2.Name());
  if (!constraint_cache_.contains(name)) {
    model_.AddImplication(v1, v2).WithName(name);
    constraint_cache_.insert(name);
  }
}

void ModelWrapper::AddImplicationAnd(const BoolVar& var,
                                     absl::Span<const BoolVar> literals) {
  if (literals.size() == 0) {
    model_.FixVariable(var, false);
    return;
  }
  const string name = AndConstraintName(var, literals);
  if (!constraint_cache_.contains(name)) {
    model_.AddBoolAnd(literals).OnlyEnforceIf(var).WithName(name);
    constraint_cache_.insert(name);
  }
}

void ModelWrapper::AddImplicationOr(const BoolVar& var,
                                    absl::Span<const BoolVar> literals) {
  if (literals.size() == 0) {
    model_.FixVariable(var, false);
    return;
  }
  const string name = OrConstraintName(var, literals);
  if (!constraint_cache_.contains(name)) {
    model_.AddBoolOr(literals).OnlyEnforceIf(var).WithName(name);
    constraint_cache_.insert(name);
  }
}

void ModelWrapper::AddImplicationSum(
  const BoolVar& var, absl::Span<const BoolVar> literals, int sum) {
  const string name = absl::StrFormat(
      "%s -> %d = %s", var.Name(), sum, ConstraintName("+", literals));
  if (!constraint_cache_.contains(name)) {
    model_.AddEquality(LinearExpr::Sum(literals), sum)
          .OnlyEnforceIf(var)
          .WithName(name);
    constraint_cache_.insert(name);
  }
}

void ModelWrapper::AddImplicationEq(const BoolVar& var,
                                    const BoolVar& left,
                                    const BoolVar& right) {
  const string name = absl::StrFormat(
      "%s -> %s = %s", var.Name(), left.Name(), right.Name());
  if (!constraint_cache_.contains(name)) {
    model_.AddEquality(left, right)
          .OnlyEnforceIf(var)
          .WithName(name);
    constraint_cache_.insert(name);
  }
}

void ModelWrapper::AddEquivalenceAnd(
    const BoolVar& var, absl::Span<const BoolVar> literals) {
  AddImplicationAnd(var, literals);
  AddImplicationOr(Not(var), Not(literals));
}

void ModelWrapper::AddEquivalenceOr(
    const BoolVar& var, absl::Span<const BoolVar> literals) {
  AddImplicationOr(var, literals);
  AddImplicationAnd(Not(var), Not(literals));
}

void ModelWrapper::AddEquivalenceSum(const BoolVar& var,
                                     absl::Span<const BoolVar> literals) {
  const string name = absl::StrFormat(
      "%s = %s", var.Name(), ConstraintName("+", literals));
  if (!constraint_cache_.contains(name)) {
    model_.AddEquality(LinearExpr::Sum(literals), var).WithName(name);
    constraint_cache_.insert(name);
  }
}

void ModelWrapper::AddEquivalenceSumEq(const BoolVar& var,
                                       absl::Span<const BoolVar> literals,
                                       int sum) {
  const string sum_name = ConstraintName("+", literals);
  const string name1 = absl::StrFormat(
      "%s -> %d = %s", var.Name(), sum, sum_name);
  const string name2 = absl::StrFormat(
      "%s -> %d != %s", Not(var).Name(), sum, sum_name);
  if (!constraint_cache_.contains(name1)) {
    model_.AddEquality(LinearExpr::Sum(literals), sum)
          .OnlyEnforceIf(var)
          .WithName(name1);
    constraint_cache_.insert(name1);
  }
  if (!constraint_cache_.contains(name2)) {
    model_.AddNotEqual(LinearExpr::Sum(literals), sum)
          .OnlyEnforceIf(Not(var))
          .WithName(name2);
    constraint_cache_.insert(name2);
  }
}

void ModelWrapper::AddEqualitySum(absl::Span<const BoolVar> literals, int sum) {
  const string name = absl::StrFormat(
      "%d = %s", sum, ConstraintName("+", literals));
  if (!constraint_cache_.contains(name)) {
    model_.AddEquality(LinearExpr::Sum(literals), sum).WithName(name);
    constraint_cache_.insert(name);
  }
}

void ModelWrapper::AddAtMostOne(absl::Span<const BoolVar> literals) {
  const string name = absl::StrFormat("1 >= %s", ConstraintName("+", literals));
  if (!constraint_cache_.contains(name)) {
    model_.AddAtMostOne(literals).WithName(name);
    constraint_cache_.insert(name);
  }
}

void ModelWrapper::AddContradiction(const string& reason) {
  model_.AddBoolOr({model_.FalseVar()})
        .WithName(absl::StrCat("Contradiction: ", reason));
}

BoolVar ModelWrapper::CreateEquivalentVarAnd(
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

BoolVar ModelWrapper::CreateEquivalentVarOr(
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

BoolVar ModelWrapper::CreateEquivalentVarSum(
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

BoolVar ModelWrapper::CreateEquivalentVarSumEq(
    absl::Span<const BoolVar> literals, int sum, const string& name) {
  if (literals.size() == 0) {
    return model_.FalseVar();
  }
  const string key = absl::StrFormat(
      "%d=%s", sum, ConstraintName("+", literals));
  const auto it = var_cache_.find(key);
  if (it != var_cache_.end()) {
    return it->second;
  }
  BoolVar var = model_.NewBoolVar().WithName(name);
  AddEquivalenceSumEq(var, literals, sum);
  var_cache_[key] = var;
  return var;
}

}  // namespace botc
