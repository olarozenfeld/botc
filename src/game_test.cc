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

#include "absl/types/span.h"
#include "gtest/gtest.h"

namespace botc {
namespace {

GameLog FromRoles(absl::Span<const Role> roles) {
  GameLog log;
  log.set_perspective(STORYTELLER);
  auto* setup = log.mutable_setup();
  auto *player_roles = setup->mutable_player_roles();
  for (int i = 0; i < roles.size(); ++i) {
    const string player = absl::StrFormat("P%d", i);
    setup->add_players(player);
    (*player_roles)[player] = roles[i];
  }
  return log;
}

TEST(ValidateSTRoleSetup, Valid5PlayersNoBaron) {
  GameState g(FromRoles({IMP, MONK, SPY, EMPATH, VIRGIN}));
  EXPECT_EQ(g.CountWorlds({}), 1);
}

TEST(ValidateSTRoleSetup, Valid5PlayersBaron) {
  GameState g(FromRoles({IMP, SAINT, BARON, BUTLER, LIBRARIAN}));
  EXPECT_EQ(g.CountWorlds({}), 1);
}

TEST(ValidateSTRoleSetup, Valid6PlayersNoBaron) {
  GameState g(FromRoles(
    {DRUNK, SLAYER, MONK, SCARLET_WOMAN, EMPATH, IMP}));
  EXPECT_EQ(g.CountWorlds({}), 1);
}

TEST(ValidateSTRoleSetup, Valid6PlayersBaron) {
  GameState g(FromRoles(
    {DRUNK, RECLUSE, MONK, BARON, SAINT, IMP}));
  EXPECT_EQ(g.CountWorlds({}), 1);
}

TEST(ValidateSTRoleSetup, Valid9PlayersNoBaron) {
  GameState g(FromRoles({DRUNK, SLAYER, MONK, SCARLET_WOMAN, EMPATH, IMP, SAINT,
                         WASHERWOMAN, CHEF}));
  EXPECT_EQ(g.CountWorlds({}), 1);
}

TEST(ValidateSTRoleSetup, Valid9PlayersBaron) {
  GameState g(FromRoles({DRUNK, SLAYER, RECLUSE, BUTLER, EMPATH, IMP, SAINT,
                         WASHERWOMAN, BARON}));
  EXPECT_EQ(g.CountWorlds({}), 1);
}

TEST(ValidateSTRoleSetup, Invalid6PlayersNoImp) {
  GameState g(FromRoles(
    {DRUNK, SLAYER, MONK, SCARLET_WOMAN, EMPATH, CHEF}));
  EXPECT_EQ(g.CountWorlds({}), 0);
}

TEST(ValidateSTRoleSetup, Invalid6PlayersNoMinion) {
  GameState g(FromRoles({DRUNK, SLAYER, MONK, CHEF, EMPATH, IMP}));
  EXPECT_EQ(g.CountWorlds({}), 0);
}

TEST(ValidateSTRoleSetup, Invalid13PlayersTwoMinions) {
  GameState g(FromRoles(
    {VIRGIN, SLAYER, MONK, CHEF, EMPATH, IMP, SPY, SCARLET_WOMAN,
     INVESTIGATOR, WASHERWOMAN, MAYOR, UNDERTAKER, SOLDIER}));
  EXPECT_EQ(g.CountWorlds({}), 0);
}

TEST(ValidateSTRoleSetup, Invalid5PlayersRoleRepeat) {
  GameState g(FromRoles({IMP, EMPATH, SPY, EMPATH, VIRGIN}));
  EXPECT_EQ(g.CountWorlds({}), 0);
}

TEST(ValidateSTRoleSetup, ValidFortuneTellerRedHerring) {
  GameLog log = FromRoles(
      {DRUNK, SLAYER, FORTUNE_TELLER, SCARLET_WOMAN, EMPATH, IMP});
  log.mutable_setup()->set_red_herring("P1");
  GameState g(log);
  EXPECT_EQ(g.CountWorlds({}), 1);
}

TEST(ValidateSTRoleSetup, InvalidFortuneTellerRedHerring) {
  GameLog log = FromRoles(
      {DRUNK, SLAYER, FORTUNE_TELLER, SCARLET_WOMAN, EMPATH, IMP});
  log.mutable_setup()->set_red_herring("P3");  // The SW can't be a red herring.
  GameState g(log);
  EXPECT_EQ(g.CountWorlds({}), 0);
}

TEST(ObserverPerspective, SimpleTest) {
  GameLog log;
  auto* setup = log.mutable_setup();
  setup->add_players("A");
  setup->add_players("B");
  setup->add_players("C");
  setup->add_players("D");
  setup->add_players("E");

  GameState g(log);
  EXPECT_TRUE(g.IsValid());
}

}  // namespace
}  // namespace botc

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
