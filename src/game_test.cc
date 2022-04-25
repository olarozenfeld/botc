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
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace botc {
namespace {

vector<string> MakePlayers(int num_players) {
  vector<string> players;
  for (int i = 0; i < num_players; ++i) {
    players.push_back(absl::StrFormat("P%d", i + 1));
  }
  return players;
}

GameState FromRolesWithRedHerring(absl::Span<const Role> roles,
                                  const string& red_herring) {
  const vector<string> players = MakePlayers(roles.size());
  unordered_map<string, Role> role_map;
  for (int i = 0; i < roles.size(); ++i) {
    role_map[players[i]] = roles[i];
  }
  return GameState::FromStorytellerPerspective(players, role_map, red_herring);
}

GameState FromRoles(absl::Span<const Role> roles) {
  return FromRolesWithRedHerring(roles, "");
}

MinionInfo NewMinionInfo(const string& demon) {
  MinionInfo mi;
  mi.set_demon(demon);
  return mi;
}

TEST(ValidateSTRoleSetup, Valid5PlayersNoBaron) {
  GameState g = FromRoles({IMP, MONK, SPY, EMPATH, VIRGIN});
  EXPECT_EQ(g.SolveGame().worlds_size(), 1);
}

TEST(ValidateSTRoleSetup, Valid5PlayersBaron) {
  GameState g = FromRoles({IMP, SAINT, BARON, BUTLER, LIBRARIAN});
  EXPECT_EQ(g.SolveGame().worlds_size(), 1);
}

TEST(ValidateSTRoleSetup, Valid6PlayersNoBaron) {
  GameState g = FromRoles({DRUNK, SLAYER, MONK, SCARLET_WOMAN, EMPATH, IMP});
  EXPECT_EQ(g.SolveGame().worlds_size(), 1);
}

TEST(ValidateSTRoleSetup, Valid6PlayersBaron) {
  GameState g = FromRoles({DRUNK, RECLUSE, MONK, BARON, SAINT, IMP});
  EXPECT_EQ(g.SolveGame().worlds_size(), 1);
}

TEST(ValidateSTRoleSetup, Valid9PlayersNoBaron) {
  GameState g = FromRoles({DRUNK, SLAYER, MONK, SCARLET_WOMAN, EMPATH, IMP,
                           SAINT, WASHERWOMAN, CHEF});
  EXPECT_EQ(g.SolveGame().worlds_size(), 1);
}

TEST(ValidateSTRoleSetup, Valid9PlayersBaron) {
  GameState g = FromRoles({DRUNK, SLAYER, RECLUSE, BUTLER, EMPATH, IMP, SAINT,
                           WASHERWOMAN, BARON});
  EXPECT_EQ(g.SolveGame().worlds_size(), 1);
}

TEST(ValidateSTRoleSetup, Invalid6PlayersNoImp) {
  GameState g = FromRoles({DRUNK, SLAYER, MONK, SCARLET_WOMAN, EMPATH, CHEF});
  EXPECT_EQ(g.SolveGame().worlds_size(), 0);
}

TEST(ValidateSTRoleSetup, Invalid6PlayersNoMinion) {
  GameState g = FromRoles({DRUNK, SLAYER, MONK, CHEF, EMPATH, IMP});
  EXPECT_EQ(g.SolveGame().worlds_size(), 0);
}

TEST(ValidateSTRoleSetup, Invalid13PlayersTwoMinions) {
  GameState g = FromRoles(
    {VIRGIN, SLAYER, MONK, CHEF, EMPATH, IMP, SPY, SCARLET_WOMAN,
     INVESTIGATOR, WASHERWOMAN, MAYOR, UNDERTAKER, SOLDIER});
  EXPECT_EQ(g.SolveGame().worlds_size(), 0);
}

TEST(ValidateSTRoleSetup, Invalid5PlayersRoleRepeat) {
  GameState g = FromRoles({IMP, EMPATH, SPY, EMPATH, VIRGIN});
  EXPECT_EQ(g.SolveGame().worlds_size(), 0);
}

TEST(ValidateSTRoleSetup, ValidFortuneTellerRedHerring) {
  GameState g = FromRolesWithRedHerring(
      {DRUNK, SLAYER, FORTUNE_TELLER, SCARLET_WOMAN, EMPATH, IMP}, "P2");
  EXPECT_EQ(g.SolveGame().worlds_size(), 1);
}

TEST(ValidateSTRoleSetup, InvalidFortuneTellerRedHerring) {
  GameState g = FromRolesWithRedHerring(
      {DRUNK, SLAYER, FORTUNE_TELLER, SCARLET_WOMAN, EMPATH, IMP}, "P4");
  // The SW can't be a red herring.
  EXPECT_EQ(g.SolveGame().worlds_size(), 0);
}

unordered_map<string, Role> CopyWorld(const SolverResponse r, int index) {
  unordered_map<string, Role> world(
      r.worlds(index).roles().begin(), r.worlds(index).roles().end());
  return world;
}

// Nasty hack -- I should write a gMock matcher for this instead, especially
// to compare multiple worlds unordered.
#define EXPECT_WORLD_EQ(r, i, w) \
    EXPECT_THAT(CopyWorld(r, i), testing::Eq(unordered_map<string, Role>(w)))

TEST(WorldEnumeration, MinionPerspectiveBaronFull) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", BARON);
  g.AddMinionInfo("P1", NewMinionInfo("P2"));  // P1 Baron, P2 Imp
  g.AddDay(1);
  g.AddClaim("P3", CHEF);
  g.AddClaim("P4", SAINT);
  g.AddClaim("P5", BUTLER);
  g.AddClaim("P6", WASHERWOMAN);
  g.AddClaim("P7", MONK);
  g.AddClaim("P1", LIBRARIAN);  // Evil team lie
  g.AddClaim("P2", SLAYER);
  SolverResponse r = g.SolveGame();
  EXPECT_EQ(r.worlds_size(), 1);
  unordered_map<string, Role> expected_world({
      {"P1", BARON}, {"P2", IMP}, {"P3", CHEF}, {"P4", SAINT}, {"P5", BUTLER},
      {"P6", WASHERWOMAN}, {"P7", MONK}});
  EXPECT_WORLD_EQ(r, 0, expected_world);
}

TEST(WorldEnumeration, MinionPerspectiveBaronDrunk) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", BARON);
  g.AddMinionInfo("P1", NewMinionInfo("P2"));  // P1 Baron, P2 Imp
  g.AddDay(1);
  g.AddClaim("P3", CHEF);
  g.AddClaim("P4", SAINT);
  g.AddClaim("P5", MAYOR);
  g.AddClaim("P6", WASHERWOMAN);
  g.AddClaim("P7", MONK);
  g.AddClaim("P1", LIBRARIAN);  // Evil team lie
  g.AddClaim("P2", SLAYER);
  SolverResponse r = g.SolveGame();
  EXPECT_EQ(r.worlds_size(), 4);
  unordered_map<string, Role> expected_world({
      {"P1", BARON}, {"P2", IMP}, {"P3", CHEF}, {"P4", SAINT}, {"P5", MAYOR},
      {"P6", WASHERWOMAN}, {"P7", DRUNK}});
  EXPECT_WORLD_EQ(r, 0, expected_world);
  expected_world = {
      {"P1", BARON}, {"P2", IMP}, {"P3", CHEF}, {"P4", SAINT}, {"P5", MAYOR},
      {"P6", DRUNK}, {"P7", MONK}};
  EXPECT_WORLD_EQ(r, 1, expected_world);
  expected_world = {
      {"P1", BARON}, {"P2", IMP}, {"P3", DRUNK}, {"P4", SAINT}, {"P5", MAYOR},
      {"P6", WASHERWOMAN}, {"P7", MONK}};
  EXPECT_WORLD_EQ(r, 2, expected_world);
  expected_world = {
      {"P1", BARON}, {"P2", IMP}, {"P3", CHEF}, {"P4", SAINT}, {"P5", DRUNK},
      {"P6", WASHERWOMAN}, {"P7", MONK}};
  EXPECT_WORLD_EQ(r, 3, expected_world);
}

TEST(WorldEnumeration, MinionPerspectivePoisonerFull) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", POISONER);
  g.AddMinionInfo("P1", NewMinionInfo("P2"));  // P1 Poisoner, P2 Imp
  g.AddDay(1);
  g.AddClaim("P3", CHEF);
  g.AddClaim("P4", VIRGIN);
  g.AddClaim("P5", SOLDIER);
  g.AddClaim("P6", WASHERWOMAN);
  g.AddClaim("P7", MONK);
  g.AddClaim("P1", EMPATH);  // Evil team lie
  g.AddClaim("P2", SLAYER);
  SolverResponse r = g.SolveGame();
  EXPECT_EQ(r.worlds_size(), 1);
  unordered_map<string, Role> expected_world({
      {"P1", POISONER}, {"P2", IMP}, {"P3", CHEF}, {"P4", VIRGIN},
      {"P5", SOLDIER}, {"P6", WASHERWOMAN}, {"P7", MONK}});
  EXPECT_WORLD_EQ(r, 0, expected_world);
}

TEST(WorldEnumeration, MinionPerspectivePoisoner5Players) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", POISONER);  // P1 Poisoner, but they don't know the Imp
  g.AddDay(1);
  g.AddClaim("P1", EMPATH);  // Poisoner lies
  g.AddClaim("P2", SLAYER);
  g.AddClaim("P3", CHEF);
  g.AddClaim("P4", VIRGIN);
  g.AddClaim("P5", SOLDIER);
  SolverResponse r = g.SolveGame();
  EXPECT_EQ(r.worlds_size(), 4);
  unordered_map<string, Role> expected_world({
      {"P1", POISONER}, {"P2", SLAYER}, {"P3", CHEF}, {"P4", VIRGIN},
      {"P5", IMP}});
  EXPECT_WORLD_EQ(r, 0, expected_world);
  expected_world = {
      {"P1", POISONER}, {"P2", SLAYER}, {"P3", CHEF}, {"P4", IMP},
      {"P5", SOLDIER}};
  EXPECT_WORLD_EQ(r, 1, expected_world);
  expected_world = {
      {"P1", POISONER}, {"P2", IMP}, {"P3", CHEF}, {"P4", VIRGIN},
      {"P5", SOLDIER}};
  EXPECT_WORLD_EQ(r, 2, expected_world);
  expected_world = {
      {"P1", POISONER}, {"P2", SLAYER}, {"P3", IMP}, {"P4", VIRGIN},
      {"P5", SOLDIER}};
  EXPECT_WORLD_EQ(r, 3, expected_world);
}

// To test: failing votes, succeeding votes, block replacements,
// executions, deaths (inferences e.g. Saint), Virgin procs.

TEST(VotingProcess, ProgressiveVotes) {
  GameState g = GameState::FromObserverPerspective(MakePlayers(5));
  g.AddNight(1);
  g.AddDay(1);
  g.AddNomination("P1", "P2");
  g.AddVote({"P3", "P1"}, "");  // Vote fails
  EXPECT_EQ(g.OnTheBlock(), "");
  g.AddNomination("P2", "P1");
  g.AddVote({"P2", "P3", "P4"}, "P1");  // Vote succeeds
  EXPECT_EQ(g.OnTheBlock(), "P1");
  g.AddNomination("P3", "P3");
  g.AddVote({"P4", "P5", "P1"}, "");  // Vote ties
  EXPECT_EQ(g.OnTheBlock(), "");
  g.AddNomination("P5", "P5");
  g.AddVote({"P5", "P1", "P2"}, "");  // Vote fails
  EXPECT_EQ(g.OnTheBlock(), "");
  g.AddNomination("P4", "P4");
  g.AddVote({"P5", "P1", "P2", "P4"}, "P4");  // Vote succeeds
  EXPECT_EQ(g.OnTheBlock(), "P4");
}

TEST(Executions, CorrectGameState) {
  GameState g = GameState::FromObserverPerspective(MakePlayers(5));
  g.AddNight(1);
  g.AddDay(1);
  g.AddNomination("P1", "P1");
  g.AddVote({"P2", "P3", "P4"}, "P1");
  g.AddExecution("P1");
  g.AddDeath("P1");
  EXPECT_EQ(g.NumAlive(), 4);
  EXPECT_FALSE(g.IsAlive("P1"));
}

TEST(ObserverPerspective, SimpleTest) {
  GameState g = GameState::FromObserverPerspective(MakePlayers(5));
  EXPECT_EQ(g.ValidWorld().worlds_size(), 1);
}

}  // namespace
}  // namespace botc

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
