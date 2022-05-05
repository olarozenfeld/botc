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

#include "ortools/sat/cp_model.h"
#include "src/model_wrapper.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace botc {
using operations_research::sat::LinearExpr;

TEST(ModelWrapper, EquivalentToCpModel) {
  CpModelBuilder original;
  ModelWrapper wrapper;

  BoolVar x_o = original.NewBoolVar().WithName("x");
  BoolVar y_o = original.NewBoolVar().WithName("y");
  BoolVar z_o = original.NewBoolVar().WithName("z");
  BoolVar x_w = wrapper.NewVar("x");
  BoolVar y_w = wrapper.NewVar("y");
  BoolVar z_w = wrapper.NewVar("z");

  original.FixVariable(x_o, true);
  original.FixVariable(y_o, false);
  wrapper.FixVariable(x_w, true);
  wrapper.FixVariable(y_w, false);

  original.AddBoolAnd({x_o, Not(y_o)}).WithName("Not(y) ^ x");
  wrapper.AddAnd({x_w, Not(y_w)});

  original.AddBoolOr({x_o, z_o}).WithName("x V z");
  wrapper.AddOr({x_w, z_w});

  original.AddEquality(Not(x_o), z_o).WithName("Not(x) = z");
  wrapper.AddEquality(Not(x_w), z_w);

  original.AddImplication(x_o, y_o).WithName("x -> y");
  wrapper.AddImplication(x_w, y_w);

  original.AddBoolAnd({y_o, z_o}).OnlyEnforceIf(x_o).WithName("x -> y ^ z");
  wrapper.AddImplicationAnd(x_w, {y_w, z_w});

  original.AddBoolOr({y_o, z_o}).OnlyEnforceIf(x_o).WithName("x -> y V z");
  wrapper.AddImplicationOr(x_w, {y_w, z_w});

  original.AddEquality(LinearExpr::Sum({y_o, z_o}), 2)
          .OnlyEnforceIf(x_o)
          .WithName("x -> 2 = y + z");
  wrapper.AddImplicationSum(x_w, {y_w, z_w}, 2);

  original.AddEquality(y_o, z_o).OnlyEnforceIf(x_o).WithName("x -> y = z");
  wrapper.AddImplicationEq(x_w, y_w, z_w);

  original.AddEquality(LinearExpr::Sum({y_o, z_o}), x_w).WithName("x = y + z");
  wrapper.AddEquivalenceSum(x_w, {y_w, z_w});

  // Unfortunately, the testing::EqualsProto matcher is not OSS yet
  // (see https://github.com/google/googletest/issues/1761)
  EXPECT_EQ(wrapper.Model().Build().DebugString(),
            original.Build().DebugString());
}

TEST(ModelWrapper, CachingWorks) {
  ModelWrapper wrapper;

  BoolVar x = wrapper.NewVar("x");
  BoolVar y = wrapper.NewVar("y");
  EXPECT_EQ(x, wrapper.NewVar("x"));  // Variable caching.
  EXPECT_EQ(wrapper.Model().Build().variables_size(), 2);
  // Equivalent constraint caching.
  wrapper.AddAnd({x, Not(y)});
  wrapper.AddAnd({Not(y), x});
  wrapper.AddEquality(x, y);
  wrapper.AddEquality(y, x);
  EXPECT_EQ(wrapper.Model().Build().constraints_size(), 2);
}
}  // namespace botc

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
