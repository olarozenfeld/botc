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

vector<unordered_map<string, Role>> CopyWorlds(const SolverResponse& r) {
  vector<unordered_map<string, Role>> result;
  for (const auto& w : r.worlds()) {
    unordered_map<string, Role> world(w.roles().begin(), w.roles().end());
    result.push_back(world);
  }
  return result;
}

// Nasty hack -- I should write a gMock matcher for this instead, especially
// to compare multiple worlds unordered.
#define EXPECT_WORLDS_EQ(r, w) \
    EXPECT_THAT(CopyWorlds(r), testing::UnorderedElementsAreArray(w))

TEST(WorldEnumeration, MinionPerspectiveBaronFull) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", BARON);
  g.AddMinionInfo("P1", "P2", {});  // P1 Baron, P2 Imp
  g.AddDay(1);
  g.AddClaim("P3", CHEF);
  g.AddClaim("P4", SAINT);
  g.AddClaim("P5", BUTLER);
  g.AddClaim("P6", WASHERWOMAN);
  g.AddClaim("P7", MONK);
  g.AddClaim("P1", LIBRARIAN);  // Evil team lie
  g.AddClaim("P2", SLAYER);
  SolverResponse r = g.SolveGame();
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", BARON}, {"P2", IMP}, {"P3", CHEF}, {"P4", SAINT}, {"P5", BUTLER},
       {"P6", WASHERWOMAN}, {"P7", MONK}}});
  EXPECT_WORLDS_EQ(r, expected_worlds);
}

TEST(WorldEnumeration, MinionPerspectiveBaronDrunk) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", BARON);
  g.AddMinionInfo("P1", "P2", {});  // P1 Baron, P2 Imp
  g.AddDay(1);
  g.AddClaim("P3", CHEF);
  g.AddClaim("P4", SAINT);
  g.AddClaim("P5", MAYOR);
  g.AddClaim("P6", WASHERWOMAN);
  g.AddClaim("P7", MONK);
  g.AddClaim("P1", LIBRARIAN);  // Evil team lie
  g.AddClaim("P2", SLAYER);
  SolverResponse r = g.SolveGame();
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", BARON}, {"P2", IMP}, {"P3", CHEF}, {"P4", SAINT}, {"P5", MAYOR},
       {"P6", WASHERWOMAN}, {"P7", DRUNK}},
      {{"P1", BARON}, {"P2", IMP}, {"P3", CHEF}, {"P4", SAINT}, {"P5", MAYOR},
       {"P6", DRUNK}, {"P7", MONK}},
      {{"P1", BARON}, {"P2", IMP}, {"P3", DRUNK}, {"P4", SAINT}, {"P5", MAYOR},
       {"P6", WASHERWOMAN}, {"P7", MONK}},
      {{"P1", BARON}, {"P2", IMP}, {"P3", CHEF}, {"P4", SAINT}, {"P5", DRUNK},
       {"P6", WASHERWOMAN}, {"P7", MONK}}});
  EXPECT_WORLDS_EQ(r, expected_worlds);
}

TEST(WorldEnumeration, MinionPerspectivePoisonerFull) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", POISONER);
  g.AddMinionInfo("P1", "P2", {});  // P1 Poisoner, P2 Imp
  g.AddDay(1);
  g.AddClaim("P3", CHEF);
  g.AddClaim("P4", VIRGIN);
  g.AddClaim("P5", SOLDIER);
  g.AddClaim("P6", WASHERWOMAN);
  g.AddClaim("P7", MONK);
  g.AddClaim("P1", EMPATH);  // Evil team lie
  g.AddClaim("P2", SLAYER);
  SolverResponse r = g.SolveGame();
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", POISONER}, {"P2", IMP}, {"P3", CHEF}, {"P4", VIRGIN},
       {"P5", SOLDIER}, {"P6", WASHERWOMAN}, {"P7", MONK}}});
  EXPECT_WORLDS_EQ(r, expected_worlds);
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
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", POISONER}, {"P2", SLAYER}, {"P3", CHEF}, {"P4", VIRGIN},
       {"P5", IMP}},
      {{"P1", POISONER}, {"P2", SLAYER}, {"P3", CHEF}, {"P4", IMP},
       {"P5", SOLDIER}},
      {{"P1", POISONER}, {"P2", IMP}, {"P3", CHEF}, {"P4", VIRGIN},
       {"P5", SOLDIER}},
      {{"P1", POISONER}, {"P2", SLAYER}, {"P3", IMP}, {"P4", VIRGIN},
       {"P5", SOLDIER}}});
  EXPECT_WORLDS_EQ(r, expected_worlds);
}

TEST(WorldEnumeration, MinionPerspectivePoisoner5PlayersFull) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", POISONER);  // P1 Poisoner, but they don't know the Imp
  g.AddDay(1);
  g.AddClaim("P1", EMPATH);  // Poisoner lies
  g.AddClaim("P2", SAINT);  // P2 claimed outsider, which P1 knows is a lie.
  g.AddClaim("P3", CHEF);  // Therefore, P1 deduces P2 is the Imp.
  g.AddClaim("P4", VIRGIN);
  g.AddClaim("P5", SOLDIER);
  SolverResponse r = g.SolveGame();
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", POISONER}, {"P2", IMP}, {"P3", CHEF}, {"P4", VIRGIN},
       {"P5", SOLDIER}}});
  EXPECT_WORLDS_EQ(r, expected_worlds);
}

TEST(WorldEnumeration, DemonPerspective7Players) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2"}, {EMPATH, CHEF, SOLDIER});
  g.AddDay(1);
  g.AddClaim("P1", EMPATH);  // Imp lies
  g.AddClaim("P2", SAINT);  // Minion lies
  g.AddClaim("P3", UNDERTAKER);
  g.AddClaim("P4", VIRGIN);
  g.AddClaim("P5", MAYOR);
  g.AddClaim("P6", SLAYER);
  g.AddClaim("P7", RAVENKEEPER);  // No true outsider claims, so no Baron
  SolverResponse r = g.SolveGame();
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", SPY}, {"P3", UNDERTAKER}, {"P4", VIRGIN},
       {"P5", MAYOR}, {"P6", SLAYER}, {"P7", RAVENKEEPER}},
      {{"P1", IMP}, {"P2", POISONER}, {"P3", UNDERTAKER}, {"P4", VIRGIN},
       {"P5", MAYOR}, {"P6", SLAYER}, {"P7", RAVENKEEPER}},
      {{"P1", IMP}, {"P2", SCARLET_WOMAN}, {"P3", UNDERTAKER}, {"P4", VIRGIN},
       {"P5", MAYOR}, {"P6", SLAYER}, {"P7", RAVENKEEPER}}});
  EXPECT_WORLDS_EQ(r, expected_worlds);
}

TEST(WorldEnumeration, InvalidDemonPerspective7Players) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2"}, {EMPATH, CHEF, SOLDIER});
  g.AddDay(1);
  g.AddClaim("P1", EMPATH);  // Imp lies
  g.AddClaim("P2", SAINT);  // Minion lies
  g.AddClaim("P3", CHEF);  // This is impossible, since Chef is a bluff.
  g.AddClaim("P4", VIRGIN);
  g.AddClaim("P5", MAYOR);
  g.AddClaim("P6", SLAYER);
  g.AddClaim("P7", RAVENKEEPER);
  SolverResponse r = g.SolveGame();
  EXPECT_EQ(r.worlds_size(), 0);
}

TEST(Investigator, DemonLearnsMinionRole) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2"}, {EMPATH, CHEF, SOLDIER});
  g.AddDay(1);
  g.AddClaim("P1", EMPATH);  // Imp lies
  g.AddClaim("P2", SAINT);  // Minion claims only outsider, so no Baron.
  g.AddClaim("P3", INVESTIGATOR);
  g.AddClaimInvestigatorInfo("P3", "P2", "P5", POISONER);
  g.AddClaim("P4", VIRGIN);
  g.AddClaim("P5", MAYOR);
  g.AddClaim("P6", SLAYER);
  g.AddClaim("P7", RAVENKEEPER);
  SolverResponse r = g.SolveGame();
  // Minion can only be a Poisoner:
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", POISONER}, {"P3", INVESTIGATOR}, {"P4", VIRGIN},
       {"P5", MAYOR}, {"P6", SLAYER}, {"P7", RAVENKEEPER}}});
  EXPECT_WORLDS_EQ(r, expected_worlds);
}

TEST(Washerwoman, VirginConfirmsWasherwoman) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", BARON);
  g.AddMinionInfo("P1", "P2", {});
  g.AddDay(1);
  g.AddClaim("P1", EMPATH);
  g.AddClaim("P2", SAINT);
  g.AddClaim("P3", WASHERWOMAN);
  g.AddClaimWasherwomanInfo("P3", "P4", "P5", MAYOR);
  g.AddClaim("P4", VIRGIN);
  g.AddClaim("P5", MAYOR);
  g.AddClaim("P6", SLAYER);
  g.AddClaim("P7", RECLUSE);  // Baron learns that P6 is the Drunk.
  g.AddNomination("P3", "P4");
  g.AddExecution("P3");
  SolverResponse r = g.SolveGame();
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", BARON}, {"P2", IMP}, {"P3", WASHERWOMAN}, {"P4", VIRGIN},
       {"P5", MAYOR}, {"P6", DRUNK}, {"P7", RECLUSE}}});
  EXPECT_WORLDS_EQ(r, expected_worlds);
}

TEST(Librarian, VirginConfirmsLibrarian) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", BARON);
  g.AddMinionInfo("P1", "P2", {});
  g.AddDay(1);
  g.AddClaim("P1", EMPATH);
  g.AddClaim("P2", SAINT);
  g.AddClaim("P3", LIBRARIAN);
  g.AddClaimLibrarianInfo("P3", "P1", "P6", DRUNK);
  g.AddClaim("P4", VIRGIN);
  g.AddClaim("P5", MAYOR);
  g.AddClaim("P6", SLAYER);
  g.AddClaim("P7", RECLUSE);  // Baron learns that P6 is the Drunk.
  g.AddNomination("P3", "P4");
  g.AddExecution("P3");
  SolverResponse r = g.SolveGame();
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", BARON}, {"P2", IMP}, {"P3", LIBRARIAN}, {"P4", VIRGIN},
       {"P5", MAYOR}, {"P6", DRUNK}, {"P7", RECLUSE}}});
  EXPECT_WORLDS_EQ(r, expected_worlds);
}

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

TEST(Virgin, HealthyVirginProc) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", BARON);
  g.AddMinionInfo("P1", "P2", {});  // P1 Baron, P2 Imp
  g.AddDay(1);
  g.AddClaim("P1", EMPATH);
  g.AddClaim("P2", MAYOR);
  g.AddClaim("P3", CHEF);
  g.AddClaim("P4", VIRGIN);
  g.AddClaim("P5", SAINT);
  g.AddClaim("P6", SOLDIER);
  g.AddClaim("P7", INVESTIGATOR);
  g.AddNomination("P3", "P4");
  g.AddExecution("P3");
  SolverResponse r = g.SolveGame();
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", BARON}, {"P2", IMP}, {"P3", CHEF}, {"P4", VIRGIN},
       {"P5", SAINT}, {"P6", DRUNK}, {"P7", INVESTIGATOR}},
      {{"P1", BARON}, {"P2", IMP}, {"P3", CHEF}, {"P4", VIRGIN},
       {"P5", SAINT}, {"P6", SOLDIER}, {"P7", DRUNK}}});
  EXPECT_WORLDS_EQ(r, expected_worlds);
}

TEST(Virgin, DrunkVirginNonProc) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", BARON);
  g.AddMinionInfo("P1", "P2", {});  // P1 Baron, P2 Imp
  g.AddDay(1);
  g.AddClaim("P1", EMPATH);
  g.AddClaim("P2", MAYOR);
  g.AddClaim("P3", CHEF);
  g.AddClaim("P4", VIRGIN);
  g.AddClaim("P5", SAINT);
  g.AddClaim("P6", SOLDIER);
  g.AddClaim("P7", INVESTIGATOR);
  g.AddNomination("P3", "P4");
  g.AddNoStorytellerAnnouncement();  // No proc.
  SolverResponse r = g.SolveGame();
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", BARON}, {"P2", IMP}, {"P3", CHEF}, {"P4", DRUNK},
       {"P5", SAINT}, {"P6", SOLDIER}, {"P7", INVESTIGATOR}},
      {{"P1", BARON}, {"P2", IMP}, {"P3", DRUNK}, {"P4", VIRGIN},
       {"P5", SAINT}, {"P6", SOLDIER}, {"P7", INVESTIGATOR}}});
  EXPECT_WORLDS_EQ(r, expected_worlds);
}

TEST(Virgin, PoisonedVirginNonProc) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", POISONER);
  g.AddMinionInfo("P1", "P2", {});
  g.AddPoisonerAction("P1", "P4");
  g.AddDay(1);
  g.AddClaim("P1", EMPATH);
  g.AddClaim("P2", SAINT);
  g.AddClaim("P3", CHEF);
  g.AddClaim("P4", VIRGIN);
  g.AddClaim("P5", EMPATH);
  g.AddClaim("P6", SOLDIER);
  g.AddClaim("P7", INVESTIGATOR);
  g.AddNomination("P3", "P4");
  g.AddNoStorytellerAnnouncement();  // No proc.
  SolverResponse r = g.SolveGame();
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", POISONER}, {"P2", IMP}, {"P3", CHEF}, {"P4", VIRGIN},
       {"P5", EMPATH}, {"P6", SOLDIER}, {"P7", INVESTIGATOR}}});
  EXPECT_WORLDS_EQ(r, expected_worlds);
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

TEST(ScarletWomanProc, ExecuteImp) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P5", SCARLET_WOMAN);
  g.AddMinionInfo("P5", "P1", {});  // P5 SW, P1 Imp
  g.AddDay(1);
  g.AddClaim("P1", SOLDIER);  // Imp lies
  g.AddClaim("P2", MAYOR);
  g.AddClaim("P3", CHEF);
  g.AddClaim("P4", VIRGIN);
  g.AddClaim("P5", FORTUNE_TELLER);
  g.AddClaim("P6", SLAYER);
  g.AddClaim("P7", RAVENKEEPER);
  g.AddNomination("P2", "P1");
  g.AddVote({"P2", "P3", "P4", "P6"}, "P1");
  g.AddExecution("P1");
  g.AddDeath("P1");
  g.AddNight(2);
  SolverResponse r = g.SolveGame();
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", MAYOR}, {"P3", CHEF}, {"P4", VIRGIN},
       {"P5", IMP}, {"P6", SLAYER}, {"P7", RAVENKEEPER}}});
  EXPECT_WORLDS_EQ(r, expected_worlds);
}

TEST(ScarletWomanProc, SlayerKillsImp) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P5", SCARLET_WOMAN);
  g.AddMinionInfo("P5", "P1", {});  // P5 SW, P1 Imp
  g.AddDay(1);
  g.AddClaim("P1", SOLDIER);  // Imp lies
  g.AddClaim("P2", MAYOR);
  g.AddClaim("P3", CHEF);
  g.AddClaim("P4", VIRGIN);
  g.AddClaim("P5", FORTUNE_TELLER);
  g.AddClaim("P6", SLAYER);
  g.AddClaim("P7", RAVENKEEPER);
  g.AddSlayerAction("P6", "P1");
  g.AddDeath("P1");
  g.AddNight(2);
  SolverResponse r = g.SolveGame();
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", MAYOR}, {"P3", CHEF}, {"P4", VIRGIN},
       {"P5", IMP}, {"P6", SLAYER}, {"P7", RAVENKEEPER}}});
  EXPECT_WORLDS_EQ(r, expected_worlds);
}

TEST(GameEndConditions, ExecuteImpGameOver) {
  GameState g = GameState::FromObserverPerspective(MakePlayers(5));
  g.AddNight(1);
  g.AddDay(1);
  g.AddNomination("P1", "P1");
  g.AddVote({"P2", "P3", "P4"}, "P1");
  g.AddExecution("P1");
  g.AddDeath("P1");
  g.AddVictory(GOOD);
  SolverResponse r = g.ValidWorld();
  EXPECT_EQ(r.worlds_size(), 1);
  SolverRequest request;
  auto* pr = request.mutable_current_assumptions()->add_roles();
  pr->set_player("P1");
  pr->set_role(IMP);
  pr->set_is_not(true);
  r = g.ValidWorld(request);
  EXPECT_EQ(r.worlds_size(), 0);
  pr->set_is_not(false);
  request.mutable_starting_assumptions()->add_roles_in_play(SCARLET_WOMAN);
  r = g.ValidWorld(request);
  EXPECT_EQ(r.worlds_size(), 0);  // Because the SW would have proc-ed.
}

TEST(GameEndConditions, InvalidExecuteImpOn4GameNotOver) {
  GameState g = GameState::FromObserverPerspective(MakePlayers(5));
  g.AddNight(1);
  g.AddDay(1);
  g.AddNomination("P1", "P1");
  g.AddVote({"P2", "P3", "P4"}, "P1");
  g.AddExecution("P1");
  g.AddDeath("P1");
  g.AddNight(2);
  g.AddDay(2);
  g.AddNomination("P2", "P3");
  g.AddVote({"P4", "P5"}, "P3");
  g.AddExecution("P3");
  g.AddDeath("P3");
  g.AddNight(3);  // The game continues, so P3 could not have been the Imp.
  SolverResponse r = g.ValidWorld(FromCurrentRoles({{"P3", IMP}}));
  EXPECT_EQ(r.worlds_size(), 0);
}

TEST(GameEndConditions, InvalidExecuteImpNoScarletWomanGameNotOver) {
  GameState g = GameState::FromObserverPerspective(MakePlayers(5));
  g.AddNight(1);
  g.AddDay(1);
  g.AddNomination("P1", "P1");
  g.AddVote({"P2", "P3", "P4"}, "P1");
  g.AddExecution("P1");
  g.AddDeath("P1");
  g.AddNight(2);  // The game continues, so P1 Imp -> SW in play.
  SolverRequest request = FromCurrentRoles({{"P1", IMP}});
  SolverResponse r = g.ValidWorld(request);
  EXPECT_EQ(r.worlds_size(), 1);
  request.mutable_starting_assumptions()->add_roles_not_in_play(SCARLET_WOMAN);
  r = g.ValidWorld(request);
  EXPECT_EQ(r.worlds_size(), 0);
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
