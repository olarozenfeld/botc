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

#include <filesystem>
#include <map>

#include "src/game_sat_solver.h"
#include "src/game_state.h"

#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace botc {
namespace {

using std::filesystem::path;
using std::map;

vector<string> MakePlayers(int num_players) {
  vector<string> players;
  for (int i = 0; i < num_players; ++i) {
    players.push_back(absl::StrFormat("P%d", i + 1));
  }
  return players;
}

vector<unordered_map<string, Role>> CopyWorlds(const SolverResponse& r) {
  vector<unordered_map<string, Role>> result;
  for (const auto& w : r.worlds()) {
    unordered_map<string, Role> world(w.current_roles().begin(),
                                      w.current_roles().end());
    result.push_back(world);
  }
  return result;
}

// Nasty hack -- I should write a gMock matcher for this instead, especially
// to compare multiple worlds unordered.
#define EXPECT_WORLDS_EQ(r, w) \
    EXPECT_THAT(CopyWorlds(r), testing::UnorderedElementsAreArray(w))

TEST(ValidateSTRoleSetup, Valid5PlayersNoBaron) {
  GameState g(STORYTELLER, TROUBLE_BREWING, MakePlayers(5));
  g.SetRoles({IMP, MONK, SPY, MAYOR, VIRGIN});
  g.AddNight(1);
  g.AddAllShownTokens({IMP, MONK, SPY, MAYOR, VIRGIN});
  g.AddDay(1);
  g.AddAllClaims({SLAYER, MONK, RAVENKEEPER, MAYOR, VIRGIN}, "P1");
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", MONK}, {"P3", SPY}, {"P4", MAYOR},
       {"P5", VIRGIN}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(ValidateSTRoleSetup, Valid5PlayersBaron) {
  GameState g(STORYTELLER, TROUBLE_BREWING, MakePlayers(5));
  g.SetRoles({IMP, SAINT, BARON, RECLUSE, VIRGIN});
  g.AddNight(1);
  g.AddAllShownTokens({IMP, SAINT, BARON, RECLUSE, VIRGIN});
  g.AddDay(1);
  g.AddAllClaims({SLAYER, SAINT, RAVENKEEPER, RECLUSE, VIRGIN}, "P1");
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", SAINT}, {"P3", BARON}, {"P4", RECLUSE},
       {"P5", VIRGIN}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(ValidateSTRoleSetup, Valid6PlayersNoBaron) {
  GameState g(STORYTELLER, TROUBLE_BREWING, MakePlayers(6));
  g.SetRoles({DRUNK, SLAYER, MONK, SCARLET_WOMAN, SOLDIER, IMP});
  g.AddNight(1);
  g.AddAllShownTokens({RAVENKEEPER, SLAYER, MONK, SCARLET_WOMAN, SOLDIER, IMP});
  g.AddDay(1);
  g.AddAllClaims({RAVENKEEPER, SLAYER, MONK, MONK, SOLDIER, VIRGIN}, "P1");
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", DRUNK}, {"P2", SLAYER}, {"P3", MONK}, {"P4", SCARLET_WOMAN},
       {"P5", SOLDIER}, {"P6", IMP}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(ValidateSTRoleSetup, Valid6PlayersBaron) {
  GameState g(STORYTELLER, TROUBLE_BREWING, MakePlayers(6));
  g.SetRoles({DRUNK, RECLUSE, MONK, BARON, SAINT, IMP});
  g.AddNight(1);
  g.AddAllShownTokens({MAYOR, RECLUSE, MONK, BARON, SAINT, IMP});
  g.AddDay(1);
  g.AddAllClaims({MAYOR, RECLUSE, MONK, MONK, SAINT, VIRGIN}, "P1");
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", DRUNK}, {"P2", RECLUSE}, {"P3", MONK}, {"P4", BARON},
       {"P5", SAINT}, {"P6", IMP}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(ValidateSTRoleSetup, Valid9PlayersNoBaron) {
  GameState g(STORYTELLER, TROUBLE_BREWING, MakePlayers(9));
  g.SetRoles({DRUNK, SLAYER, MONK, SCARLET_WOMAN, UNDERTAKER, IMP,
              SAINT, RAVENKEEPER, MAYOR});
  g.AddNight(1);
  g.AddAllShownTokens({VIRGIN, SLAYER, MONK, SCARLET_WOMAN, UNDERTAKER, IMP,
                       SAINT, RAVENKEEPER, MAYOR});
  g.AddDay(1);
  g.AddAllClaims({VIRGIN, SLAYER, MONK, VIRGIN, UNDERTAKER, VIRGIN,
                  SAINT, RAVENKEEPER, MAYOR}, "P1");
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", DRUNK}, {"P2", SLAYER}, {"P3", MONK}, {"P4", SCARLET_WOMAN},
       {"P5", UNDERTAKER}, {"P6", IMP}, {"P7", SAINT}, {"P8", RAVENKEEPER},
       {"P9", MAYOR}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(ValidateSTRoleSetup, Valid9PlayersBaron) {
  GameState g(STORYTELLER, TROUBLE_BREWING, MakePlayers(9));
  g.SetRoles({DRUNK, SLAYER, RECLUSE, BUTLER, UNDERTAKER, IMP, SAINT,
              VIRGIN, BARON});
  g.AddNight(1);
  g.AddAllShownTokens({MAYOR, SLAYER, RECLUSE, BUTLER, UNDERTAKER, IMP, SAINT,
                       VIRGIN, BARON});
  g.AddDay(1);
  g.AddAllClaims({MAYOR, SLAYER, RECLUSE, BUTLER, UNDERTAKER, VIRGIN, SAINT,
                  VIRGIN, VIRGIN}, "P1");
  g.AddClaim("P4", g.NewButlerAction("P3"));
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", DRUNK}, {"P2", SLAYER}, {"P3", RECLUSE}, {"P4", BUTLER},
       {"P5", UNDERTAKER}, {"P6", IMP}, {"P7", SAINT}, {"P8", VIRGIN},
       {"P9", BARON}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(ValidateSTRoleSetup, Invalid6PlayersNoImp) {
  GameState g(STORYTELLER, TROUBLE_BREWING, MakePlayers(6));
  g.SetRoles({DRUNK, SLAYER, MONK, SCARLET_WOMAN, MAYOR, RAVENKEEPER});
  g.AddNight(1);
  g.AddAllShownTokens({VIRGIN, SLAYER, MONK, POISONER, MAYOR, RAVENKEEPER});
  g.AddDay(1);
  g.AddAllClaims({VIRGIN, SLAYER, MONK, VIRGIN, MAYOR, RAVENKEEPER}, "P1");
  EXPECT_FALSE(IsValidWorld(g));
}

TEST(ValidateSTRoleSetup, Invalid6PlayersNoMinion) {
  GameState g(STORYTELLER, TROUBLE_BREWING, MakePlayers(6));
  g.SetRoles({DRUNK, SLAYER, MONK, RAVENKEEPER, MAYOR, IMP});
  g.AddNight(1);
  g.AddAllShownTokens({VIRGIN, SLAYER, MONK, RAVENKEEPER, MAYOR, IMP});
  g.AddDay(1);
  g.AddAllClaims({VIRGIN, SLAYER, MONK, RAVENKEEPER, MAYOR, SAINT}, "P1");
  EXPECT_FALSE(IsValidWorld(g));
}

TEST(ValidateSTRoleSetup, Invalid13PlayersTwoMinions) {
  GameState g(STORYTELLER, TROUBLE_BREWING, MakePlayers(13));
  g.SetRoles({VIRGIN, SLAYER, MONK, RAVENKEEPER, EMPATH, IMP, SPY,
              SCARLET_WOMAN, INVESTIGATOR, WASHERWOMAN, MAYOR, UNDERTAKER,
              SOLDIER});
  g.AddNight(1);
  g.AddAllShownTokens({VIRGIN, SLAYER, MONK, RAVENKEEPER, EMPATH, IMP, SPY,
      SCARLET_WOMAN, INVESTIGATOR, WASHERWOMAN, MAYOR, UNDERTAKER, SOLDIER});
  g.AddDay(1);
  g.AddAllClaims({VIRGIN, SLAYER, MONK, RAVENKEEPER, EMPATH, SAINT, SAINT,
      SAINT, INVESTIGATOR, WASHERWOMAN, MAYOR, UNDERTAKER, SOLDIER}, "P1");
  g.AddClaim("P5", g.NewEmpathInfo(1));
  g.AddClaim("P9", g.NewInvestigatorInfo("P8", "P2", SCARLET_WOMAN));
  g.AddClaim("P10", g.NewWasherwomanInfo("P3", "P8", MONK));
  EXPECT_FALSE(IsValidWorld(g));
}

TEST(ValidateSTRoleSetup, Invalid5PlayersRoleRepeat) {
  GameState g(STORYTELLER, TROUBLE_BREWING, MakePlayers(5));
  g.SetRoles({IMP, VIRGIN, SPY, VIRGIN, SOLDIER});
  g.AddNight(1);
  g.AddAllShownTokens({IMP, VIRGIN, SPY, VIRGIN, SOLDIER});
  g.AddDay(1);
  g.AddAllClaims({SAINT, VIRGIN, SAINT, VIRGIN, SOLDIER}, "P1");
  EXPECT_FALSE(IsValidWorld(g));
}

TEST(ValidateSTRoleSetup, ValidFortuneTellerRedHerring) {
  GameState g(STORYTELLER, TROUBLE_BREWING, MakePlayers(6));
  g.SetRoles({DRUNK, SLAYER, FORTUNE_TELLER, SCARLET_WOMAN, SOLDIER, IMP});
  g.SetRedHerring("P2");
  g.AddNight(1);
  g.AddAllShownTokens({VIRGIN, SLAYER, FORTUNE_TELLER, SCARLET_WOMAN,
                       SOLDIER, IMP});
  g.AddDay(1);
  g.AddAllClaims({VIRGIN, SLAYER, FORTUNE_TELLER, SLAYER, SOLDIER,
                  SLAYER}, "P1");
  g.AddClaim("P3", g.NewFortuneTellerAction("P5", "P6", true));
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", DRUNK}, {"P2", SLAYER}, {"P3", FORTUNE_TELLER},
       {"P4", SCARLET_WOMAN}, {"P5", SOLDIER}, {"P6", IMP}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(ValidateSTRoleSetup, InvalidFortuneTellerRedHerring) {
  GameState g(STORYTELLER, TROUBLE_BREWING, MakePlayers(6));
  g.SetRoles({DRUNK, SLAYER, FORTUNE_TELLER, SCARLET_WOMAN, SOLDIER, IMP});
  g.SetRedHerring("P4");
  g.AddNight(1);
  g.AddAllShownTokens(
      {VIRGIN, SLAYER, FORTUNE_TELLER, SCARLET_WOMAN, SOLDIER, IMP});
  g.AddDay(1);
  g.AddAllClaims({VIRGIN, SLAYER, FORTUNE_TELLER, SAINT, SOLDIER, SAINT}, "P1");
  g.AddClaim("P3", g.NewFortuneTellerAction("P5", "P6", true));
  // The SW can't be a red herring.
  EXPECT_FALSE(IsValidWorld(g));
}

TEST(WorldEnumeration, MinionPerspectiveBaronFull) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", BARON);
  g.AddMinionInfo("P1", "P2", {});  // P1 Baron, P2 Imp
  g.AddDay(1);
  g.AddAllClaims(
      {RAVENKEEPER, SLAYER, UNDERTAKER, SAINT, RECLUSE, MAYOR, MONK}, "P1");
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", BARON}, {"P2", IMP}, {"P3", UNDERTAKER}, {"P4", SAINT},
       {"P5", RECLUSE}, {"P6", MAYOR}, {"P7", MONK}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(WorldEnumeration, MinionPerspectiveBaronDrunk) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", BARON);
  g.AddMinionInfo("P1", "P2", {});  // P1 Baron, P2 Imp
  g.AddDay(1);
  g.AddAllClaims(
      {RAVENKEEPER, SLAYER, UNDERTAKER, SAINT, MAYOR, VIRGIN, MONK}, "P1");
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", BARON}, {"P2", IMP}, {"P3", UNDERTAKER}, {"P4", SAINT},
       {"P5", MAYOR}, {"P6", VIRGIN}, {"P7", DRUNK}},
      {{"P1", BARON}, {"P2", IMP}, {"P3", UNDERTAKER}, {"P4", SAINT},
       {"P5", MAYOR}, {"P6", DRUNK}, {"P7", MONK}},
      {{"P1", BARON}, {"P2", IMP}, {"P3", DRUNK}, {"P4", SAINT}, {"P5", MAYOR},
       {"P6", VIRGIN}, {"P7", MONK}},
      {{"P1", BARON}, {"P2", IMP}, {"P3", UNDERTAKER}, {"P4", SAINT},
       {"P5", DRUNK}, {"P6", VIRGIN}, {"P7", MONK}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(WorldEnumeration, MinionPerspectivePoisonerFull) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", POISONER);
  g.AddMinionInfo("P1", "P2", {});  // P1 Poisoner, P2 Imp
  g.AddDay(1);
  g.AddAllClaims(
      {RAVENKEEPER, SLAYER, UNDERTAKER, VIRGIN, SOLDIER, MAYOR, MONK}, "P1");
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", POISONER}, {"P2", IMP}, {"P3", UNDERTAKER}, {"P4", VIRGIN},
       {"P5", SOLDIER}, {"P6", MAYOR}, {"P7", MONK}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(WorldEnumeration, MinionPerspectivePoisoner5Players) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", POISONER);  // P1 Poisoner, but they don't know the Imp
  g.AddDay(1);
  g.AddAllClaims({MAYOR, SLAYER, MONK, VIRGIN, SOLDIER}, "P1");
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", POISONER}, {"P2", SLAYER}, {"P3", MONK}, {"P4", VIRGIN},
       {"P5", IMP}},
      {{"P1", POISONER}, {"P2", SLAYER}, {"P3", MONK}, {"P4", IMP},
       {"P5", SOLDIER}},
      {{"P1", POISONER}, {"P2", IMP}, {"P3", MONK}, {"P4", VIRGIN},
       {"P5", SOLDIER}},
      {{"P1", POISONER}, {"P2", SLAYER}, {"P3", IMP}, {"P4", VIRGIN},
       {"P5", SOLDIER}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(WorldEnumeration, MinionPerspectivePoisoner5PlayersFull) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", POISONER);  // P1 Poisoner, but they don't know the Imp
  g.AddDay(1);
  g.AddAllClaims({SLAYER, SAINT, MONK, VIRGIN, SOLDIER}, "P1");
  // P2 claimed outsider, which P1 knows is a lie. Therefore, P1 deduces P2 is
  // the Imp.
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", POISONER}, {"P2", IMP}, {"P3", MONK}, {"P4", VIRGIN},
       {"P5", SOLDIER}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(WorldEnumeration, DemonPerspective7Players) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2"}, {EMPATH, CHEF, SOLDIER});
  g.AddDay(1);
  g.AddAllClaims(
      {SOLDIER, SAINT, UNDERTAKER, VIRGIN, MAYOR, SLAYER, RAVENKEEPER}, "P1");
  // No true outsider claims, so no Baron
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", SPY}, {"P3", UNDERTAKER}, {"P4", VIRGIN},
       {"P5", MAYOR}, {"P6", SLAYER}, {"P7", RAVENKEEPER}},
      {{"P1", IMP}, {"P2", POISONER}, {"P3", UNDERTAKER}, {"P4", VIRGIN},
       {"P5", MAYOR}, {"P6", SLAYER}, {"P7", RAVENKEEPER}},
      {{"P1", IMP}, {"P2", SCARLET_WOMAN}, {"P3", UNDERTAKER}, {"P4", VIRGIN},
       {"P5", MAYOR}, {"P6", SLAYER}, {"P7", RAVENKEEPER}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(WorldEnumeration, InvalidDemonPerspective7Players) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2"}, {UNDERTAKER, MONK, SOLDIER});
  g.AddDay(1);
  g.AddAllClaims(
      {UNDERTAKER, SAINT, MONK, VIRGIN, MAYOR, SLAYER, RAVENKEEPER}, "P1");
  // This is impossible, since Monk is a demon bluff.
  EXPECT_FALSE(IsValidWorld(g));
}

TEST(Chef, LearnsNumber_0) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", CHEF);
  g.AddRoleAction("P1", g.NewChefInfo(0));
  g.AddDay(1);
  g.AddAllClaims({CHEF, MAYOR, VIRGIN, SLAYER, RECLUSE}, "P1");
  g.AddClaim("P1", g.NewChefInfo(0));
  GameSatSolver s(g);
  unordered_map<string, Role> roles({
      {"P1", CHEF}, {"P2", IMP}, {"P3", DRUNK}, {"P4", BARON},
      {"P5", RECLUSE}});
  EXPECT_WORLDS_EQ(
      s.Solve(SolverRequestBuilder::FromCurrentRoles(roles)), {roles});
  roles = {{"P1", CHEF}, {"P2", IMP}, {"P3", VIRGIN}, {"P4", SLAYER},
           {"P5", SCARLET_WOMAN}};
  EXPECT_WORLDS_EQ(
      s.Solve(SolverRequestBuilder::FromCurrentRoles(roles)), {roles});
  roles = {{"P1", DRUNK}, {"P2", IMP}, {"P3", BARON}, {"P4", SLAYER},
           {"P5", RECLUSE}};  // Drunk Chef
  EXPECT_WORLDS_EQ(
      s.Solve(SolverRequestBuilder::FromCurrentRoles(roles)), {roles});
  roles = {{"P1", CHEF}, {"P2", MAYOR}, {"P3", VIRGIN}, {"P4", POISONER},
           {"P5", IMP}};  // Poisoned Chef.
  EXPECT_WORLDS_EQ(
      s.Solve(SolverRequestBuilder::FromCurrentRoles(roles)), {roles});
  roles = {{"P1", CHEF}, {"P2", MAYOR}, {"P3", VIRGIN}, {"P4", SPY},
           {"P5", IMP}};  // Spy reads as Good.
  EXPECT_WORLDS_EQ(
      s.Solve(SolverRequestBuilder::FromCurrentRoles(roles)), {roles});
  roles = {{"P1", CHEF}, {"P2", MAYOR}, {"P3", VIRGIN}, {"P4", SCARLET_WOMAN},
           {"P5", IMP}};
  EXPECT_FALSE(s.IsValidWorld(SolverRequestBuilder::FromCurrentRoles(roles)));
}

TEST(Chef, LearnsNumber_1) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P3", CHEF);
  g.AddRoleAction("P3", g.NewChefInfo(1));
  g.AddDay(1);
  g.AddAllClaims({RAVENKEEPER, MAYOR, CHEF, SLAYER, RECLUSE}, "P1");
  g.AddClaim("P3", g.NewChefInfo(1));
  unordered_map<string, Role> roles({
      {"P1", DRUNK}, {"P2", IMP}, {"P3", CHEF}, {"P4", BARON},
      {"P5", RECLUSE}});
  EXPECT_WORLDS_EQ(
      Solve(g, SolverRequestBuilder::FromCurrentRoles(roles)), {roles});
}

TEST(Investigator, DemonLearnsMinionRole) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2"}, {MONK, CHEF, SOLDIER});
  g.AddDay(1);
  g.AddAllClaims(
      {MONK, SAINT, INVESTIGATOR, VIRGIN, MAYOR, SLAYER, RAVENKEEPER}, "P1");
  // Minion claims only outsider, so no Baron.
  g.AddClaim("P3", g.NewInvestigatorInfo("P2", "P5", POISONER));
  // Minion can only be a Poisoner:
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", POISONER}, {"P3", INVESTIGATOR}, {"P4", VIRGIN},
       {"P5", MAYOR}, {"P6", SLAYER}, {"P7", RAVENKEEPER}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(Washerwoman, VirginConfirmsWasherwoman) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", BARON);
  g.AddMinionInfo("P1", "P2", {});
  g.AddDay(1);
  g.AddAllClaims(
      {MONK, SAINT, WASHERWOMAN, VIRGIN, MAYOR, SLAYER, RECLUSE}, "P1");
  // Baron learns that P6 is the Drunk, not the Slayer.
  g.AddClaim("P3", g.NewWasherwomanInfo("P4", "P5", MAYOR));
  g.AddNomination("P3", "P4");
  g.AddExecution("P3");
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", BARON}, {"P2", IMP}, {"P3", WASHERWOMAN}, {"P4", VIRGIN},
       {"P5", MAYOR}, {"P6", DRUNK}, {"P7", RECLUSE}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(Librarian, VirginConfirmsLibrarian) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", BARON);
  g.AddMinionInfo("P1", "P2", {});
  g.AddDay(1);
  g.AddAllClaims(
      {MONK, SAINT, LIBRARIAN, VIRGIN, MAYOR, SLAYER, RECLUSE}, "P1");
  // Baron learns that P6 is the Drunk.
  g.AddClaim("P3", g.NewLibrarianInfo("P1", "P6", DRUNK));
  g.AddNomination("P3", "P4");
  g.AddExecution("P3");
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", BARON}, {"P2", IMP}, {"P3", LIBRARIAN}, {"P4", VIRGIN},
       {"P5", MAYOR}, {"P6", DRUNK}, {"P7", RECLUSE}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(Virgin, HealthyVirginProc) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", BARON);
  g.AddMinionInfo("P1", "P2", {});  // P1 Baron, P2 Imp
  g.AddDay(1);
  g.AddAllClaims(
      {MONK, MAYOR, UNDERTAKER, VIRGIN, SAINT, SOLDIER, RAVENKEEPER}, "P1");
  g.AddNomination("P3", "P4");
  g.AddExecution("P3");
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", BARON}, {"P2", IMP}, {"P3", UNDERTAKER}, {"P4", VIRGIN},
       {"P5", SAINT}, {"P6", DRUNK}, {"P7", RAVENKEEPER}},
      {{"P1", BARON}, {"P2", IMP}, {"P3", UNDERTAKER}, {"P4", VIRGIN},
       {"P5", SAINT}, {"P6", SOLDIER}, {"P7", DRUNK}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(Virgin, DrunkVirginNonProc) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", BARON);
  g.AddMinionInfo("P1", "P2", {});  // P1 Baron, P2 Imp
  g.AddDay(1);
  g.AddAllClaims(
      {MONK, MAYOR, UNDERTAKER, VIRGIN, SAINT, SOLDIER, RAVENKEEPER}, "P1");
  g.AddNomination("P3", "P4");
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", BARON}, {"P2", IMP}, {"P3", UNDERTAKER}, {"P4", DRUNK},
       {"P5", SAINT}, {"P6", SOLDIER}, {"P7", RAVENKEEPER}},
      {{"P1", BARON}, {"P2", IMP}, {"P3", DRUNK}, {"P4", VIRGIN},
       {"P5", SAINT}, {"P6", SOLDIER}, {"P7", RAVENKEEPER}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(Virgin, PoisonedVirginNonProc) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", POISONER);
  g.AddMinionInfo("P1", "P2", {});
  g.AddRoleAction("P1", g.NewPoisonerAction("P4"));
  g.AddDay(1);
  g.AddAllClaims(
      {MONK, SAINT, UNDERTAKER, VIRGIN, MAYOR, SOLDIER, RAVENKEEPER}, "P1");
  g.AddNomination("P5", "P4");
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", POISONER}, {"P2", IMP}, {"P3", UNDERTAKER}, {"P4", VIRGIN},
       {"P5", MAYOR}, {"P6", SOLDIER}, {"P7", RAVENKEEPER}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(Undertaker, HealthyVirginProcDrunkUndertaker) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", BARON);
  g.AddMinionInfo("P1", "P2", {});  // P1 Baron, P2 Imp
  g.AddDay(1);
  g.AddAllClaims(
      {MAYOR, MAYOR, RAVENKEEPER, VIRGIN, SAINT, SOLDIER, UNDERTAKER}, "P1");
  g.AddNomination("P3", "P4");
  g.AddExecution("P3");
  g.AddDeath("P3");
  g.AddNight(2);
  g.AddDay(2);
  g.AddClaim("P7", g.NewUndertakerInfo(SPY));
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", BARON}, {"P2", IMP}, {"P3", RAVENKEEPER}, {"P4", VIRGIN},
       {"P5", SAINT}, {"P6", SOLDIER}, {"P7", DRUNK}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(Undertaker, SpyFalseRegisters) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", UNDERTAKER);
  g.AddDay(1);
  g.AddAllClaims({UNDERTAKER, MAYOR, VIRGIN, SLAYER, INVESTIGATOR}, "P1");
  g.AddClaim("P5", g.NewInvestigatorInfo("P1", "P2", POISONER));
  g.AddNomination("P5", "P3");
  g.AddExecution("P5");
  g.AddDeath("P5");
  g.AddNight(2);
  g.AddRoleAction("P1", g.NewUndertakerInfo(INVESTIGATOR));
  g.AddDay(2);
  g.AddClaim("P1", g.NewUndertakerInfo(INVESTIGATOR));
  SolverRequest r = SolverRequestBuilder::FromCurrentRoles("P5", SPY);
  EXPECT_TRUE(IsValidWorld(g, r));
  r = SolverRequestBuilder::FromCurrentRoles("P5", INVESTIGATOR);
  EXPECT_TRUE(IsValidWorld(g, r));
  r = SolverRequestBuilder::FromCurrentRolesNot(
      {{"P5", SPY}, {"P5", INVESTIGATOR}});
  EXPECT_FALSE(IsValidWorld(g, r));
}

TEST(Undertaker, RecluseFalseRegisters) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", UNDERTAKER);
  g.AddDay(1);
  g.AddAllClaims({UNDERTAKER, MAYOR, VIRGIN, SLAYER, RECLUSE}, "P1");
  g.AddNominationVoteExecution("P2", "P5");
  g.AddDeath("P5");
  g.AddNight(2);
  g.AddRoleAction("P1", g.NewUndertakerInfo(IMP));
  g.AddDay(2);
  g.AddClaim("P1", g.NewUndertakerInfo(IMP));
  SolverRequest r = SolverRequestBuilder::FromCurrentRoles("P5", RECLUSE);
  EXPECT_TRUE(IsValidWorld(g, r));
  r = SolverRequestBuilder::FromCurrentRoles("P5", IMP);
  EXPECT_TRUE(IsValidWorld(g, r));
  r = SolverRequestBuilder::FromCurrentRolesNot({{"P5", IMP}, {"P5", RECLUSE}});
  EXPECT_FALSE(IsValidWorld(g, r));
}

TEST(Undertaker, HealthyUndertakerUseless) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", BARON);
  g.AddMinionInfo("P1", "P2", {});  // P1 Baron, P2 Imp
  g.AddDay(1);
  g.AddAllClaims(
      {SLAYER, MAYOR, RAVENKEEPER, VIRGIN, SAINT, SOLDIER, UNDERTAKER}, "P1");
  g.AddNomination("P3", "P4");
  g.AddExecution("P3");
  g.AddDeath("P3");
  g.AddNight(2);
  g.AddDay(2);
  g.AddClaim("P7", g.NewUndertakerInfo(RAVENKEEPER));
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", BARON}, {"P2", IMP}, {"P3", RAVENKEEPER}, {"P4", VIRGIN},
       {"P5", SAINT}, {"P6", DRUNK}, {"P7", UNDERTAKER}},
      {{"P1", BARON}, {"P2", IMP}, {"P3", RAVENKEEPER}, {"P4", VIRGIN},
       {"P5", SAINT}, {"P6", SOLDIER}, {"P7", DRUNK}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
  g.AddNominationVoteExecution("P1", "P6");
  g.AddDeath("P6");
  g.AddNight(3);
  g.AddDay(3);
  g.AddClaim("P7", g.NewUndertakerInfo(DRUNK));
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}


TEST(NightDeaths, ImpDeducesPoisoner) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2"}, {EMPATH, RECLUSE, MONK});
  g.AddDay(1);
  g.AddAllClaims({MAYOR, SAINT, UNDERTAKER, RAVENKEEPER, SOLDIER, SLAYER,
                  VIRGIN}, "P1");
  // Minion is not a Baron, but other options are all in.
  g.AddNight(2);
  g.AddRoleAction("P1", g.NewImpAction("P5"));
  g.AddDay(2);
  g.AddNightDeath("P5");  // Imp successfully kills Soldier, so P2 is Poisoner.
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", POISONER}, {"P3", UNDERTAKER}, {"P4", RAVENKEEPER},
       {"P5", SOLDIER}, {"P6", SLAYER}, {"P7", VIRGIN}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(NightDeaths, ImpDeducesSoberMonk) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2"}, {EMPATH, RECLUSE, FORTUNE_TELLER});
  g.AddDay(1);
  g.AddAllClaims(
      {MAYOR, RAVENKEEPER, VIRGIN, SAINT, SOLDIER, SLAYER, MONK}, "P1");
  g.AddNight(2);
  g.AddRoleAction("P1", g.NewImpAction("P6"));
  g.AddDay(2);
  g.AddClaim("P7", g.NewMonkAction("P6"));
  SolverRequest r = SolverRequestBuilder::FromCurrentRoles("P7", MONK);
  EXPECT_EQ(Solve(g, r).worlds_size(), Solve(g).worlds_size());
}

TEST(NightDeaths, ImpDeducesDrunkSoldier) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2"}, {EMPATH, RECLUSE, FORTUNE_TELLER});
  g.AddDay(1);
  g.AddAllClaims(
      {MAYOR, RAVENKEEPER, VIRGIN, SAINT, SOLDIER, SLAYER, MONK}, "P1");
  g.AddNight(2);
  g.AddRoleAction("P1", g.NewImpAction("P5"));
  g.AddDay(2);
  g.AddNightDeath("P5");
  g.AddClaim("P7", g.NewMonkAction("P6"));
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", BARON}, {"P3", VIRGIN}, {"P4", SAINT},
       {"P5", DRUNK}, {"P6", SLAYER}, {"P7", MONK}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(NightDeaths, MayorBounceToSoldier) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2"}, {SLAYER, RECLUSE, FORTUNE_TELLER});
  g.AddDay(1);
  g.AddAllClaims(
      {SLAYER, RAVENKEEPER, VIRGIN, SAINT, SOLDIER, MAYOR, UNDERTAKER}, "P1");
  g.AddNight(2);
  g.AddRoleAction("P1", g.NewImpAction("P6"));
  g.AddDay(2);
  // That means both the Mayor and the Soldier are sober:
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", BARON}, {"P3", VIRGIN}, {"P4", SAINT},
       {"P5", SOLDIER}, {"P6", MAYOR}, {"P7", DRUNK}},
      {{"P1", IMP}, {"P2", BARON}, {"P3", DRUNK}, {"P4", SAINT},
       {"P5", SOLDIER}, {"P6", MAYOR}, {"P7", UNDERTAKER}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(NightDeaths, MayorBounceToMonkProtectedTarget) {
  GameState g(STORYTELLER, TROUBLE_BREWING, MakePlayers(7));
  g.SetRoles({IMP, BARON, MONK, MAYOR, SAINT, RECLUSE, VIRGIN});
  g.AddNight(1);
  g.AddAllShownTokens({IMP, BARON, MONK, MAYOR, SAINT, RECLUSE, VIRGIN});
  g.AddDemonInfo("P1", {"P2"}, {RAVENKEEPER, FORTUNE_TELLER, SOLDIER});
  g.AddMinionInfo("P2", "P1", {});
  g.AddDay(1);
  g.AddAllClaims(
      {RAVENKEEPER, SOLDIER, MONK, MAYOR, SAINT, RECLUSE, VIRGIN}, "P1");
  g.AddNight(2);
  // Monk is protecting P7, not the Mayor.
  g.AddRoleAction("P3", g.NewMonkAction("P7"));
  // Imp tries to kill the Mayor.
  g.AddRoleAction("P1", g.NewImpAction("P4"));
  g.AddDay(2);
  g.AddClaim("P3", g.NewMonkAction("P7"));  // Kill bounced to P7.
  EXPECT_TRUE(IsValidWorld(g));
}

TEST(NightDeaths, MayorBounce) {
  GameState g(STORYTELLER, TROUBLE_BREWING, MakePlayers(5));
  g.SetRoles({IMP, BARON, MAYOR, SAINT, RECLUSE});
  g.AddNight(1);
  g.AddAllShownTokens({IMP, BARON, MAYOR, SAINT, RECLUSE});
  g.AddDay(1);
  g.AddAllClaims({RAVENKEEPER, SOLDIER, MAYOR, SAINT, RECLUSE}, "P1");
  g.AddNight(2);
  g.AddRoleAction("P1", g.NewImpAction("P3"));  // Imp tries to kill the Mayor.
  g.AddDay(2);
  g.AddNightDeath("P5");
  EXPECT_TRUE(IsValidWorld(g));
}

TEST(Starpass, ImpPerspectiveSuccessToBaron) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2"}, {EMPATH, MAYOR, FORTUNE_TELLER});
  g.AddDay(1);
  g.AddAllClaims(
      {MAYOR, RAVENKEEPER, RECLUSE, SAINT, SOLDIER, SLAYER, VIRGIN}, "P1");
  g.AddNight(2);
  g.AddRoleAction("P1", g.NewImpAction("P1"));
  g.AddDay(2);
  g.AddNightDeath("P1");
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", IMP}, {"P3", RECLUSE}, {"P4", SAINT},
       {"P5", SOLDIER}, {"P6", SLAYER}, {"P7", VIRGIN}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(Starpass, ImpPerspectiveSuccessToRecluse) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2"}, {EMPATH, MAYOR, FORTUNE_TELLER});
  g.AddDay(1);
  g.AddAllClaims(
      {MAYOR, RAVENKEEPER, RECLUSE, SAINT, SOLDIER, SLAYER, VIRGIN}, "P1");
  g.AddNight(2);
  g.AddRoleAction("P1", g.NewImpAction("P1"));
  g.AddDay(2);
  g.AddNightDeath("P1");
  g.AddClaim("P3", IMP);  // Recluse comes out and claims Good Imp.
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", BARON}, {"P3", IMP}, {"P4", SAINT},
       {"P5", SOLDIER}, {"P6", SLAYER}, {"P7", VIRGIN}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(Starpass, ImpPerspectiveFailMonkProtected) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2"}, {EMPATH, MAYOR, FORTUNE_TELLER});
  g.AddDay(1);
  g.AddAllClaims(
      {MAYOR, RAVENKEEPER, RECLUSE, SAINT, SOLDIER, SLAYER, MONK}, "P1");
  g.AddNight(2);
  g.AddRoleAction("P1", g.NewImpAction("P1"));
  g.AddDay(2);
  g.AddClaim("P7", g.NewMonkAction("P1"));
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", BARON}, {"P3", RECLUSE}, {"P4", SAINT},
       {"P5", SOLDIER}, {"P6", SLAYER}, {"P7", MONK}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(Starpass, ImpPerspectiveInvalidFailMonkProtectedOther) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2"}, {EMPATH, MAYOR, FORTUNE_TELLER});
  g.AddDay(1);
  g.AddAllClaims(
      {MAYOR, RAVENKEEPER, RECLUSE, SAINT, SOLDIER, SLAYER, MONK}, "P1");
  g.AddNight(2);
  g.AddRoleAction("P1", g.NewImpAction("P1"));
  g.AddDay(2);
  g.AddClaim("P7", g.NewMonkAction("P2"));
  EXPECT_FALSE(IsValidWorld(g));
}

TEST(Starpass, ImpPerspectiveFailPoisoned) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2"}, {EMPATH, MAYOR, FORTUNE_TELLER});
  g.AddDay(1);
  g.AddAllClaims(
      {MAYOR, RAVENKEEPER, MONK, UNDERTAKER, SOLDIER, SLAYER, VIRGIN}, "P1");
  g.AddNight(2);
  g.AddRoleAction("P1", g.NewImpAction("P1"));
  g.AddDay(2);
  g.AddClaim("P3", g.NewMonkAction("P4"));
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", POISONER}, {"P3", MONK}, {"P4", UNDERTAKER},
       {"P5", SOLDIER}, {"P6", SLAYER}, {"P7", VIRGIN}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(Starpass, PoisonerPerspectiveCatch) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", POISONER);
  g.AddRoleAction("P1", g.NewPoisonerAction("P2"));
  g.AddDay(1);
  g.AddAllClaims({MAYOR, RAVENKEEPER, VIRGIN, UNDERTAKER, SOLDIER}, "P1");
  g.AddNight(2);
  g.AddRoleAction("P1", g.NewPoisonerAction("P2"));
  g.AddShownToken("P1", IMP);
  g.AddDay(2);
  g.AddNightDeath("P4");
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", RAVENKEEPER}, {"P3", VIRGIN}, {"P4", IMP},
       {"P5", SOLDIER}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(Starpass, InvalidPoisonerPerspectiveCatch) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", POISONER);
  g.AddRoleAction("P1", g.NewPoisonerAction("P2"));
  g.AddDay(1);
  g.AddAllClaims({MAYOR, SLAYER, VIRGIN, UNDERTAKER, SOLDIER}, "P1");
  g.AddNight(2);
  g.AddRoleAction("P1", g.NewPoisonerAction("P2"));
  g.AddShownToken("P1", IMP);
  g.AddDay(2);
  g.AddNightDeath("P2");
  EXPECT_FALSE(IsValidWorld(g));
}

TEST(Starpass, BaronPerspectiveThreeMinions) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(13));
  g.AddNight(1);
  g.AddShownToken("P4", BARON);
  g.AddMinionInfo("P4", "P2", {"P1", "P13"});
  g.AddDay(1);
  g.AddAllClaims(
      {WASHERWOMAN, CHEF, LIBRARIAN, MONK, SOLDIER, SLAYER, UNDERTAKER,
       SAINT, VIRGIN, RAVENKEEPER, MAYOR, RECLUSE, EMPATH}, "P1");
  g.AddNight(2);
  g.AddDay(2);
  g.AddNightDeath("P2");  // P13 caught a starpass.
  g.AddNight(3);
  g.AddShownToken("P4", IMP);  // We caught a starpass.
  g.AddDay(3);
  g.AddNightDeath("P13");
  g.AddNight(4);
  g.AddRoleAction("P4", g.NewImpAction("P4"));  // We starpass to P1.
  g.AddDay(4);
  g.AddNightDeath("P4");
  g.AddClaim("P1", g.NewWasherwomanInfo("P2", "P3", MAYOR));
  g.AddClaim("P3", g.NewLibrarianInfo("P1", "P8", SAINT));
  g.AddClaim("P2", g.NewChefInfo(0));
  g.AddClaim("P4", g.NewMonkAction("P5"), Time::Night(2));
  g.AddClaim("P4", g.NewMonkAction("P5"), Time::Night(3));
  g.AddClaim("P4", g.NewMonkAction("P5"), Time::Night(4));
  g.AddClaim("P13", g.NewEmpathInfo(0), Time::Night(1));
  g.AddClaim("P13", g.NewEmpathInfo(0), Time::Night(2));
  SolverRequest r = SolverRequestBuilder()
      .AddRolesInPlay({SCARLET_WOMAN, SPY}).Build();
  // This also verifies that P13 (and not P1) must be the Scarlet Woman.
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", IMP}, {"P3", LIBRARIAN}, {"P4", IMP},
       {"P5", SOLDIER}, {"P6", SLAYER}, {"P7", UNDERTAKER},
       {"P8", SAINT}, {"P9", VIRGIN}, {"P10", RAVENKEEPER},
       {"P11", MAYOR}, {"P12", RECLUSE}, {"P13", IMP}}});
  EXPECT_WORLDS_EQ(Solve(g, r), expected_worlds);
}

TEST(Ravenkeeper, SpyFalseRegisters) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", RAVENKEEPER);
  g.AddDay(1);
  g.AddAllClaims({RAVENKEEPER, MAYOR, VIRGIN, SLAYER, INVESTIGATOR}, "P1");
  g.AddClaim("P5", g.NewInvestigatorInfo("P1", "P2", POISONER));
  g.AddNomination("P5", "P3");
  g.AddExecution("P5");
  g.AddDeath("P5");
  g.AddNight(2);
  g.AddRoleAction("P1", g.NewRavenkeeperAction("P5", INVESTIGATOR));
  g.AddDay(2);
  g.AddNightDeath("P1");
  g.AddClaim("P1", g.NewRavenkeeperAction("P5", INVESTIGATOR));
  SolverRequest r = SolverRequestBuilder::FromCurrentRoles("P5", SPY);
  EXPECT_TRUE(IsValidWorld(g, r));
  r = SolverRequestBuilder::FromCurrentRoles("P5", INVESTIGATOR);
  EXPECT_TRUE(IsValidWorld(g, r));
  r = SolverRequestBuilder::FromCurrentRolesNot(
      {{"P5", SPY}, {"P5", INVESTIGATOR}});
  EXPECT_FALSE(IsValidWorld(g, r));
}

TEST(Ravenkeeper, RecluseFalseRegisters) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", RAVENKEEPER);
  g.AddDay(1);
  g.AddAllClaims({RAVENKEEPER, MAYOR, VIRGIN, SLAYER, RECLUSE}, "P1");
  g.AddNominationVoteExecution("P5", "P5");
  g.AddDeath("P5");
  g.AddNight(2);
  g.AddRoleAction("P1", g.NewRavenkeeperAction("P5", IMP));
  g.AddDay(2);
  g.AddNightDeath("P1");
  g.AddClaim("P1", g.NewRavenkeeperAction("P5", IMP));
  SolverRequest r = SolverRequestBuilder::FromCurrentRoles("P5", RECLUSE);
  EXPECT_TRUE(IsValidWorld(g, r));
  r = SolverRequestBuilder::FromCurrentRoles("P5", IMP);
  EXPECT_TRUE(IsValidWorld(g, r));
  r = SolverRequestBuilder::FromCurrentRolesNot({{"P5", IMP}, {"P5", RECLUSE}});
  EXPECT_FALSE(IsValidWorld(g, r));
}

TEST(Ravenkeeper, LearnsTrueRole) {
  GameState g(STORYTELLER, TROUBLE_BREWING, MakePlayers(5));
  g.SetRoles({IMP, BARON, RAVENKEEPER, RECLUSE, SAINT});
  g.AddNight(1);
  g.AddAllShownTokens({IMP, BARON, RAVENKEEPER, RECLUSE, SAINT});
  g.AddDay(1);
  g.AddAllClaims({MAYOR, SLAYER, RAVENKEEPER, RECLUSE, SAINT}, "P1");
  g.AddNight(2);
  g.AddRoleAction("P1", g.NewImpAction("P3"));
  g.AddRoleAction("P3", g.NewRavenkeeperAction("P2", BARON));
  g.AddDay(2);
  g.AddNightDeath("P3");
  g.AddClaim("P3", g.NewRavenkeeperAction("P2", BARON));
  EXPECT_TRUE(IsValidWorld(g));
}

TEST(Ravenkeeper, DrunkRavenkeeperLearnsFalseRole) {
  GameState g(STORYTELLER, TROUBLE_BREWING, MakePlayers(5));
  g.SetRoles({IMP, BARON, DRUNK, SLAYER, SAINT});
  g.AddNight(1);
  g.AddAllShownTokens({IMP, BARON, RAVENKEEPER, SLAYER, SAINT});
  g.AddDay(1);
  g.AddAllClaims({MAYOR, UNDERTAKER, RAVENKEEPER, SLAYER, SAINT}, "P1");
  g.AddNight(2);
  g.AddRoleAction("P1", g.NewImpAction("P3"));
  g.AddRoleAction("P3", g.NewRavenkeeperAction("P2", UNDERTAKER));
  g.AddDay(2);
  g.AddNightDeath("P3");
  g.AddClaim("P3", g.NewRavenkeeperAction("P2", UNDERTAKER));
  EXPECT_TRUE(IsValidWorld(g));
}

TEST(Ravenkeeper, PoisonedRavenkeeperLearnsFalseRole) {
  GameState g(STORYTELLER, TROUBLE_BREWING, MakePlayers(5));
  g.SetRoles({IMP, POISONER, RAVENKEEPER, SLAYER, UNDERTAKER});
  g.AddNight(1);
  g.AddAllShownTokens({IMP, POISONER, RAVENKEEPER, SLAYER, UNDERTAKER});
  g.AddDay(1);
  g.AddAllClaims({MAYOR, SAINT, RAVENKEEPER, SLAYER, UNDERTAKER}, "P1");
  g.AddNight(2);
  g.AddRoleAction("P2", g.NewPoisonerAction("P3"));
  g.AddRoleAction("P1", g.NewImpAction("P3"));
  g.AddRoleAction("P3", g.NewRavenkeeperAction("P2", SAINT));
  g.AddDay(2);
  g.AddNightDeath("P3");
  g.AddClaim("P3", g.NewRavenkeeperAction("P2", SAINT));
  EXPECT_TRUE(IsValidWorld(g));
}

TEST(Ravenkeeper, InvalidRavenkeeperLearnsFalseRole) {
  GameState g(STORYTELLER, TROUBLE_BREWING, MakePlayers(5));
  g.SetRoles({IMP, POISONER, RAVENKEEPER, SLAYER, UNDERTAKER});
  g.AddNight(1);
  g.AddAllShownTokens({IMP, POISONER, RAVENKEEPER, SLAYER, UNDERTAKER});
  g.AddDay(1);
  g.AddAllClaims({MAYOR, SAINT, RAVENKEEPER, SLAYER, UNDERTAKER}, "P1");
  g.AddNight(2);
  // Ravenkeeper is healthy.
  g.AddRoleAction("P2", g.NewPoisonerAction("P4"));
  g.AddRoleAction("P1", g.NewImpAction("P3"));
  // Should learn POISONER.
  g.AddRoleAction("P3", g.NewRavenkeeperAction("P2", SAINT));
  g.AddDay(2);
  g.AddNightDeath("P3");
  g.AddClaim("P3", g.NewRavenkeeperAction("P2", SAINT));
  EXPECT_FALSE(IsValidWorld(g));
}

TEST(FortuneTeller, LearnsTrueInfo) {
  GameState g(STORYTELLER, TROUBLE_BREWING, MakePlayers(5));
  g.SetRoles({IMP, BARON, FORTUNE_TELLER, RECLUSE, SAINT});
  g.SetRedHerring("P5");
  g.AddNight(1);
  g.AddAllShownTokens({IMP, BARON, FORTUNE_TELLER, RECLUSE, SAINT});
  g.AddRoleAction("P3", g.NewFortuneTellerAction("P1", "P2", true));
  g.AddDay(1);
  g.AddAllClaims({MAYOR, SLAYER, FORTUNE_TELLER, RECLUSE, SAINT}, "P1");
  g.AddClaim("P3", g.NewFortuneTellerAction("P1", "P2", true));
  g.AddNight(2);
  g.AddRoleAction("P1", g.NewImpAction("P5"));
  g.AddRoleAction("P3", g.NewFortuneTellerAction("P3", "P4", true));
  g.AddDay(2);
  g.AddNightDeath("P5");
  g.AddClaim("P3", g.NewFortuneTellerAction("P3", "P4", true));
  g.AddNight(3);
  g.AddRoleAction("P1", g.NewImpAction("P5"));
  g.AddRoleAction("P3", g.NewFortuneTellerAction("P3", "P5", true));
  g.AddDay(3);
  g.AddClaim("P3", g.NewFortuneTellerAction("P3", "P5", true));
  g.AddNight(4);
  g.AddRoleAction("P1", g.NewImpAction("P1"));  // Imp Starpass to P2.
  g.AddShownToken("P2", IMP);
  g.AddRoleAction("P3", g.NewFortuneTellerAction("P3", "P2", true));
  g.AddDay(4);
  g.AddNightDeath("P1");
  g.AddClaim("P3", g.NewFortuneTellerAction("P3", "P2", true));
  EXPECT_TRUE(IsValidWorld(g));
}

TEST(Empath, LearnsTrueInfo) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(10));
  g.AddNight(1);
  g.AddShownToken("P1", EMPATH);
  g.AddRoleAction("P1", g.NewEmpathInfo(2));  // Both Spy and Recluse proc evil
  g.AddDay(1);
  // Virgin (P2) is actually the Spy, Saint (P7): Imp, Mayor (P8): Baron.
  g.AddAllClaims({EMPATH, VIRGIN, SOLDIER, MAYOR, SLAYER, RAVENKEEPER,
                  SAINT, MAYOR, INVESTIGATOR, RECLUSE}, "P1");
  g.AddClaim("P1", g.NewEmpathInfo(2));
  g.AddClaim("P9", g.NewInvestigatorInfo("P1", "P3", POISONER));
  g.AddNominationVoteExecution("P10", "P4");
  g.AddDeath("P4");
  g.AddNight(2);
  g.AddRoleAction("P1", g.NewEmpathInfo(0));
  g.AddDay(2);
  g.AddNightDeath("P5");
  g.AddClaim("P1", g.NewEmpathInfo(0));  // Both Spy and Recluse proc good
  g.AddNominationVoteExecution("P10", "P10");
  g.AddDeath("P10");
  g.AddNight(3);
  g.AddRoleAction("P1", g.NewEmpathInfo(1));  // Spy Evil again
  g.AddDay(3);
  g.AddClaim("P1", g.NewEmpathInfo(1));
  g.AddNominationVoteExecution("P1", "P9");
  g.AddDeath("P9");
  g.AddNight(4);
  g.AddRoleAction("P1", g.NewEmpathInfo(1));  // Spy Good again, pings Baron.
  g.AddDay(4);
  g.AddClaim("P1", g.NewEmpathInfo(1));
  g.AddNominationVoteExecution("P1", "P8");
  g.AddDeath("P8");
  g.AddNight(5);
  g.AddRoleAction("P1", g.NewEmpathInfo(1));  // Spy Good, ping Imp
  g.AddDay(5);
  g.AddClaim("P1", g.NewEmpathInfo(1));
  unordered_map<string, Role> roles({
      {"P1", EMPATH}, {"P2", SPY}, {"P3", SOLDIER}, {"P4", MAYOR},
      {"P5", SLAYER}, {"P6", RAVENKEEPER}, {"P7", IMP}, {"P8", BARON},
      {"P9", DRUNK}, {"P10", RECLUSE}});
  const SolverRequest& r = SolverRequestBuilder::FromCurrentRoles(roles);
  EXPECT_WORLDS_EQ(Solve(g, r), {roles});
}

TEST(Slayer, ImpDeducesDrunkSlayer) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2"}, {EMPATH, RECLUSE, FORTUNE_TELLER});
  g.AddDay(1);
  g.AddAllClaims(
      {MAYOR, RAVENKEEPER, VIRGIN, SAINT, SOLDIER, SLAYER, MONK}, "P1");
  g.AddRoleAction("P6", g.NewSlayerAction("P1"));
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", BARON}, {"P3", VIRGIN}, {"P4", SAINT},
       {"P5", SOLDIER}, {"P6", DRUNK}, {"P7", MONK}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(Soldier, InvalidImpKillsHealthySoldier) {
  GameState g(OBSERVER, TROUBLE_BREWING, MakePlayers(5));
  g.AddNight(1);
  g.AddDay(1);
  g.AddAllClaims({WASHERWOMAN, RECLUSE, SAINT, BUTLER, SOLDIER}, "P1");
  g.AddClaim("P1", g.NewWasherwomanInfo("P4", "P5", SOLDIER));
  g.AddClaim("P4", g.NewButlerAction("P3"));
  const SolverRequest& r =
      SolverRequestBuilder::FromCurrentRoles("P5", SOLDIER);
  // P5 is SOLDIER in all worlds:
  EXPECT_EQ(Solve(g, r).worlds_size(), Solve(g).worlds_size());
  g.AddNight(2);
  g.AddDay(2);
  g.AddNightDeath("P5");
  g.AddClaim("P4", g.NewButlerAction("P3"));
  EXPECT_FALSE(IsValidWorld(g));
}

TEST(Monk, InvalidImpKillsHealthyMonkProtectedTarget) {
  GameState g(OBSERVER, TROUBLE_BREWING, MakePlayers(5));
  g.AddNight(1);
  g.AddDay(1);
  g.AddAllClaims({WASHERWOMAN, RECLUSE, SAINT, BUTLER, MONK}, "P1");
  g.AddClaim("P1", g.NewWasherwomanInfo("P4", "P5", MONK));
  g.AddClaim("P4", g.NewButlerAction("P3"));
  const SolverRequest& r =
      SolverRequestBuilder::FromCurrentRoles("P5", MONK);
  // P5 is MONK in all worlds:
  EXPECT_EQ(Solve(g, r).worlds_size(), Solve(g).worlds_size());
  g.AddNight(2);
  g.AddDay(2);
  g.AddNightDeath("P1");
  g.AddClaim("P4", g.NewButlerAction("P3"));
  g.AddClaim("P5", g.NewMonkAction("P1"));
  EXPECT_FALSE(IsValidWorld(g));
}

TEST(ScarletWomanProc, ExecuteImp) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P2", SCARLET_WOMAN);
  g.AddMinionInfo("P2", "P1", {});  // P5 SW, P1 Imp
  g.AddDay(1);
  g.AddAllClaims(
      {SOLDIER, MAYOR, RAVENKEEPER, VIRGIN, UNDERTAKER, SLAYER, MONK}, "P1");
  g.AddNominationVoteExecution("P5", "P1");
  g.AddDeath("P1");
  g.AddNight(2);
  g.AddShownToken("P2", IMP);
  g.AddRoleAction("P2", g.NewImpAction("P5"));
  g.AddDay(2);
  g.AddNightDeath("P5");
  g.AddClaim("P7", g.NewMonkAction("P6"));
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", IMP}, {"P3", RAVENKEEPER}, {"P4", VIRGIN},
       {"P5", UNDERTAKER}, {"P6", SLAYER}, {"P7", MONK}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(ScarletWomanProc, SlayerKillsImp) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P2", SCARLET_WOMAN);
  g.AddMinionInfo("P2", "P1", {});  // P5 SW, P1 Imp
  g.AddDay(1);
  g.AddAllClaims(
      {SOLDIER, MAYOR, RAVENKEEPER, VIRGIN, UNDERTAKER, SLAYER, MONK}, "P1");
  g.AddRoleAction("P6", g.NewSlayerAction("P1"));
  g.AddDeath("P1");
  g.AddNight(2);
  g.AddShownToken("P2", IMP);
  g.AddRoleAction("P2", g.NewImpAction("P5"));
  g.AddDay(2);
  g.AddNightDeath("P5");
  g.AddClaim("P7", g.NewMonkAction("P4"));
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", IMP}, {"P3", RAVENKEEPER}, {"P4", VIRGIN},
       {"P5", UNDERTAKER}, {"P6", SLAYER}, {"P7", MONK}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(GameEndConditions, ExecuteImpGameOver) {
  GameState g(OBSERVER, TROUBLE_BREWING, MakePlayers(5));
  g.AddNight(1);
  g.AddDay(1);
  g.AddAllClaims({SOLDIER, MAYOR, RAVENKEEPER, VIRGIN, UNDERTAKER}, "P1");
  g.AddNominationVoteExecution("P2", "P1");
  g.AddDeath("P1");
  g.AddVictory(GOOD);
  EXPECT_TRUE(IsValidWorld(g));
  SolverRequest r = SolverRequestBuilder::FromCurrentRolesNot("P1", IMP);
  EXPECT_FALSE(IsValidWorld(g, r));
  r = SolverRequestBuilder()
      .AddCurrentRoles({{"P1", IMP}})
      .AddRolesInPlay({SCARLET_WOMAN})
      .Build();
  // Because the SW would have proc-ed.
  EXPECT_FALSE(IsValidWorld(g, r));
}

TEST(GameEndConditions, InvalidExecuteImpOn4GameNotOver) {
  GameState g(OBSERVER, TROUBLE_BREWING, MakePlayers(5));
  g.AddNight(1);
  g.AddDay(1);
  g.AddNight(2);
  g.AddDay(2);
  g.AddAllClaims({SOLDIER, MAYOR, RAVENKEEPER, VIRGIN, UNDERTAKER}, "P1");
  g.AddNightDeath("P1");
  g.AddNominationVoteExecution("P2", "P3");
  g.AddDeath("P3");
  // The game continues, so P3 could not have been the Imp.
  SolverRequest r = SolverRequestBuilder::FromCurrentRoles("P3", IMP);
  EXPECT_FALSE(IsValidWorld(g, r));
}

TEST(GameEndConditions, InvalidExecuteImpNoScarletWomanGameNotOver) {
  GameState g(OBSERVER, TROUBLE_BREWING, MakePlayers(5));
  g.AddNight(1);
  g.AddDay(1);
  g.AddAllClaims({SOLDIER, MAYOR, RAVENKEEPER, VIRGIN, UNDERTAKER}, "P1");
  g.AddNominationVoteExecution("P1", "P1");
  g.AddDeath("P1");
  // The game continues, so P1 Imp -> SW in play.
  SolverRequestBuilder r =
      SolverRequestBuilder().AddCurrentRoles({{"P1", IMP}});
  EXPECT_TRUE(IsValidWorld(g, r.Build()));
  r.AddRolesNotInPlay({SCARLET_WOMAN});
  EXPECT_FALSE(IsValidWorld(g, r.Build()));
}

TEST(GameEndConditions, ExecuteSaintGameOver) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", SAINT);
  g.AddDay(1);
  g.AddAllClaims({SAINT, MAYOR, SOLDIER, SLAYER, RECLUSE}, "P1");
  g.AddNominationVoteExecution("P2", "P1");
  g.AddDeath("P1");
  g.AddVictory(EVIL);
  EXPECT_TRUE(IsValidWorld(g));
}

TEST(GameEndConditions, ExecuteSaintGameNotOverPoisoner) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(6));
  g.AddNight(1);
  g.AddShownToken("P1", SAINT);
  g.AddDay(1);
  g.AddAllClaims({SAINT, MAYOR, SOLDIER, SLAYER, MONK, VIRGIN}, "P1");
  g.AddNominationVoteExecution("P2", "P1");
  g.AddDeath("P1");
  EXPECT_TRUE(IsValidWorld(g));
  // Poisoner must have got us:
  SolverRequest r =
      SolverRequestBuilder().AddRolesNotInPlay({POISONER}).Build();
  EXPECT_FALSE(IsValidWorld(g, r));
}

TEST(GameEndConditions, MayorWin) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", MAYOR);
  g.AddDay(1);
  g.AddAllClaims({MAYOR, SAINT, SOLDIER, SLAYER, RECLUSE}, "P1");
  g.AddNominationVoteExecution("P2", "P5");
  g.AddDeath("P5");
  g.AddNight(2);
  g.AddDay(2);
  g.AddNightDeath("P4");  // Final 3.
  g.AddVictory(GOOD);
  EXPECT_TRUE(IsValidWorld(g));
}

TEST(GameEndConditions, PoisonedMayorNoWin) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", MAYOR);
  g.AddDay(1);
  g.AddAllClaims({MAYOR, VIRGIN, SOLDIER, SLAYER, RAVENKEEPER}, "P1");
  g.AddNominationVoteExecution("P2", "P5");
  g.AddDeath("P5");
  g.AddNight(2);
  g.AddDay(2);
  g.AddNightDeath("P4");  // Final 3.
  g.AddNight(3);
  g.AddDay(3);
  g.AddNightDeath("P2");
  g.AddVictory(EVIL);
  EXPECT_TRUE(IsValidWorld(g));
  // Poisoner must have got us:
  SolverRequest r =
      SolverRequestBuilder().AddRolesNotInPlay({POISONER}).Build();
  EXPECT_FALSE(IsValidWorld(g, r));
}

TEST(Spy, SpyPerspective) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(13));
  g.AddNight(1);
  g.AddShownToken("P1", SPY);
  g.AddMinionInfo("P1", "P2", {"P4", "P13"});
  GrimoireInfo spy_info = g.GrimoireInfoFromRoles(
      {SPY, IMP, DRUNK, BARON, SOLDIER, SLAYER, UNDERTAKER,
       SAINT, VIRGIN, RAVENKEEPER, CHEF, MONK, SCARLET_WOMAN}, LIBRARIAN);
  g.AddRoleAction("P1", g.NewSpyInfo(spy_info));
  g.AddDay(1);
  g.AddAllClaims(
      {WASHERWOMAN, MAYOR, LIBRARIAN, RECLUSE, SOLDIER, SLAYER, UNDERTAKER,
       SAINT, VIRGIN, RAVENKEEPER, CHEF, MONK, EMPATH}, "P1");
  g.AddClaim("P1", g.NewWasherwomanInfo("P2", "P3", MAYOR));
  g.AddClaim("P3", g.NewLibrarianInfo("P4", "P8", SAINT));  // Actually true.
  g.AddClaim("P11", g.NewChefInfo(0));
  g.AddClaim("P13", g.NewEmpathInfo(0));

  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", SPY}, {"P2", IMP}, {"P3", DRUNK}, {"P4", BARON},
       {"P5", SOLDIER}, {"P6", SLAYER}, {"P7", UNDERTAKER},
       {"P8", SAINT}, {"P9", VIRGIN}, {"P10", RAVENKEEPER},
       {"P11", CHEF}, {"P12", MONK}, {"P13", SCARLET_WOMAN}}});
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);  // Spy knows the world.

  g.AddNominationVoteExecution("P3", "P9");
  g.AddDeath("P9");
  g.AddNight(2);
  spy_info.mutable_player_info(8)->set_shroud(true);  // P9 executed.
  spy_info.mutable_player_info(1)->set_shroud(true);  // P2 starpassed
  spy_info.mutable_player_info(12)->set_role(IMP);  // P13 caught starpass
  g.AddRoleAction("P1", g.NewSpyInfo(spy_info));
  g.AddDay(2);
  g.AddNightDeath("P2");
  g.AddClaim("P7", g.NewUndertakerInfo(VIRGIN));
  g.AddClaim("P12", g.NewMonkAction("P13"));
  g.AddClaim("P13", g.NewEmpathInfo(0));
  expected_worlds = {
      {{"P1", SPY}, {"P2", IMP}, {"P3", DRUNK}, {"P4", BARON},
       {"P5", SOLDIER}, {"P6", SLAYER}, {"P7", UNDERTAKER},
       {"P8", SAINT}, {"P9", VIRGIN}, {"P10", RAVENKEEPER},
       {"P11", CHEF}, {"P12", MONK}, {"P13", IMP}}};
  EXPECT_WORLDS_EQ(Solve(g), expected_worlds);
}

TEST(Examples, ExamplesWork) {
  map<path, int> world_counts_by_game({
    {"tb/monk.pbtxt", 3},
    {"tb/teensy_observer.pbtxt", 3},
    {"tb/virgin.pbtxt", 8},
  });
  for (const auto& it : world_counts_by_game) {
    GameState g = GameState::ReadFromFile("src/examples" / it.first);
    SolverResponse r = Solve(g);
    EXPECT_EQ(r.worlds_size(), it.second);
  }
}
}  // namespace
}  // namespace botc

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
