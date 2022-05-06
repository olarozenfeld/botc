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

TEST(Proto, ToAndFromProto) {
  SpyInfo spy_info;
  GameState g = FromRolesWithRedHerring(
      {IMP, SPY, SCARLET_WOMAN, POISONER, BUTLER, DRUNK, WASHERWOMAN, LIBRARIAN,
       INVESTIGATOR, CHEF, EMPATH, FORTUNE_TELLER, UNDERTAKER, MONK,
       RAVENKEEPER}, "P11");
  g.AddNight(1);
  g.AddAllShownTokens({IMP, SPY, SCARLET_WOMAN, POISONER, BUTLER, SLAYER,
                       WASHERWOMAN, LIBRARIAN, INVESTIGATOR, CHEF,
                       EMPATH, FORTUNE_TELLER, UNDERTAKER, MONK,
                       RAVENKEEPER});
  g.AddDemonInfo("P1", {"P2", "P3", "P4"}, {VIRGIN, SOLDIER, MAYOR});
  g.AddMinionInfo("P2", "P1", {"P3", "P4"});
  g.AddMinionInfo("P3", "P1", {"P2", "P4"});
  g.AddMinionInfo("P4", "P1", {"P2", "P3"});
  g.AddPoisonerAction("P4", "P9");
  g.AddSpyInfo("P2", spy_info);
  g.AddWasherwomanInfo("P7", "P1", "P2", SOLDIER);
  g.AddLibrarianInfo("P8", "P5", "P3", BUTLER);
  g.AddInvestigatorInfo("P9", "P11", "P12", SCARLET_WOMAN);  // Poisoned
  g.AddChefInfo("P10", 3);
  g.AddEmpathInfo("P11", 0);
  g.AddFortuneTellerAction("P12", "P11", "P13", true);
  g.AddButlerAction("P5", "P4");
  g.AddDay(1);
  g.AddAllClaims(
      {SOLDIER, MAYOR, SOLDIER, SAINT, BUTLER, SLAYER, WASHERWOMAN,
       LIBRARIAN, INVESTIGATOR, CHEF, EMPATH, FORTUNE_TELLER, UNDERTAKER, MONK,
       RAVENKEEPER},
      "P1");
  g.AddClaimWasherwomanInfo("P7", "P1", "P2", SOLDIER);
  g.AddClaimLibrarianInfo("P8", "P5", "P3", BUTLER);
  g.AddClaimInvestigatorInfo("P9", "P11", "P12", SCARLET_WOMAN);
  g.AddClaimChefInfo("P10", 3);
  g.AddClaimEmpathInfo("P11", 0);
  g.AddClaimFortuneTellerAction("P12", "P11", "P13", true);
  g.AddClaimButlerAction("P5", "P4");
  g.AddSlayerAction("P6", "P1");  // Drunk
  g.AddNomination("P9", "P11");
  g.AddVote({"P10", "P11", "P12", "P1", "P2", "P3", "P5", "P7"}, "P11");
  g.AddExecution("P11");
  g.AddDeath("P11");
  g.AddNight(2);
  g.AddPoisonerAction("P4", "P12");
  g.AddSpyInfo("P2", spy_info);
  g.AddMonkAction("P14", "P13");
  g.AddImpAction("P1", "P15");
  g.AddRavenkeeperAction("P15", "P4", POISONER);
  g.AddUndertakerInfo("P13", EMPATH);
  g.AddFortuneTellerAction("P12", "P9", "P2", false);
  g.AddDay(2);
  g.AddDeath("P15");
  g.AddClaimMonkAction("P14", "P13");
  g.AddClaimRavenkeeperAction("P15", "P4", POISONER);
  g.AddClaimUndertakerInfo("P13", EMPATH);
  g.AddClaimFortuneTellerAction("P12", "P9", "P2", false);

  GameLog pb(g.ToProto());
  // Unfortunately, the testing::EqualsProto matcher is not OSS yet
  // (see https://github.com/google/googletest/issues/1761)
  EXPECT_EQ(pb.DebugString(), GameState::FromProto(pb).ToProto().DebugString());
}

TEST(ValidateSTRoleSetup, Valid5PlayersNoBaron) {
  GameState g = FromRoles({IMP, MONK, SPY, EMPATH, VIRGIN});
  EXPECT_EQ(g.Solve().worlds_size(), 1);
}

TEST(ValidateSTRoleSetup, Valid5PlayersBaron) {
  GameState g = FromRoles({IMP, SAINT, BARON, BUTLER, LIBRARIAN});
  EXPECT_EQ(g.Solve().worlds_size(), 1);
}

TEST(ValidateSTRoleSetup, Valid6PlayersNoBaron) {
  GameState g = FromRoles({DRUNK, SLAYER, MONK, SCARLET_WOMAN, EMPATH, IMP});
  EXPECT_EQ(g.Solve().worlds_size(), 1);
}

TEST(ValidateSTRoleSetup, Valid6PlayersBaron) {
  GameState g = FromRoles({DRUNK, RECLUSE, MONK, BARON, SAINT, IMP});
  EXPECT_EQ(g.Solve().worlds_size(), 1);
}

TEST(ValidateSTRoleSetup, Valid9PlayersNoBaron) {
  GameState g = FromRoles({DRUNK, SLAYER, MONK, SCARLET_WOMAN, EMPATH, IMP,
                           SAINT, WASHERWOMAN, CHEF});
  EXPECT_EQ(g.Solve().worlds_size(), 1);
}

TEST(ValidateSTRoleSetup, Valid9PlayersBaron) {
  GameState g = FromRoles({DRUNK, SLAYER, RECLUSE, BUTLER, EMPATH, IMP, SAINT,
                           WASHERWOMAN, BARON});
  EXPECT_EQ(g.Solve().worlds_size(), 1);
}

TEST(ValidateSTRoleSetup, Invalid6PlayersNoImp) {
  GameState g = FromRoles({DRUNK, SLAYER, MONK, SCARLET_WOMAN, EMPATH, CHEF});
  EXPECT_EQ(g.Solve().worlds_size(), 0);
}

TEST(ValidateSTRoleSetup, Invalid6PlayersNoMinion) {
  GameState g = FromRoles({DRUNK, SLAYER, MONK, CHEF, EMPATH, IMP});
  EXPECT_EQ(g.Solve().worlds_size(), 0);
}

TEST(ValidateSTRoleSetup, Invalid13PlayersTwoMinions) {
  GameState g = FromRoles(
    {VIRGIN, SLAYER, MONK, CHEF, EMPATH, IMP, SPY, SCARLET_WOMAN,
     INVESTIGATOR, WASHERWOMAN, MAYOR, UNDERTAKER, SOLDIER});
  EXPECT_EQ(g.Solve().worlds_size(), 0);
}

TEST(ValidateSTRoleSetup, Invalid5PlayersRoleRepeat) {
  GameState g = FromRoles({IMP, EMPATH, SPY, EMPATH, VIRGIN});
  EXPECT_EQ(g.Solve().worlds_size(), 0);
}

TEST(ValidateSTRoleSetup, ValidFortuneTellerRedHerring) {
  GameState g = FromRolesWithRedHerring(
      {DRUNK, SLAYER, FORTUNE_TELLER, SCARLET_WOMAN, EMPATH, IMP}, "P2");
  EXPECT_EQ(g.Solve().worlds_size(), 1);
}

TEST(ValidateSTRoleSetup, InvalidFortuneTellerRedHerring) {
  GameState g = FromRolesWithRedHerring(
      {DRUNK, SLAYER, FORTUNE_TELLER, SCARLET_WOMAN, EMPATH, IMP}, "P4");
  // The SW can't be a red herring.
  EXPECT_EQ(g.Solve().worlds_size(), 0);
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

TEST(WorldEnumeration, MinionPerspectiveBaronFull) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", BARON);
  g.AddMinionInfo("P1", "P2", {});  // P1 Baron, P2 Imp
  g.AddDay(1);
  g.AddAllClaims(
      {LIBRARIAN, SLAYER, CHEF, SAINT, BUTLER, WASHERWOMAN, MONK}, "P1");
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", BARON}, {"P2", IMP}, {"P3", CHEF}, {"P4", SAINT}, {"P5", BUTLER},
       {"P6", WASHERWOMAN}, {"P7", MONK}}});
  EXPECT_WORLDS_EQ(g.Solve(), expected_worlds);
}

TEST(WorldEnumeration, MinionPerspectiveBaronDrunk) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", BARON);
  g.AddMinionInfo("P1", "P2", {});  // P1 Baron, P2 Imp
  g.AddDay(1);
  g.AddAllClaims(
      {LIBRARIAN, SLAYER, CHEF, SAINT, MAYOR, WASHERWOMAN, MONK}, "P1");
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", BARON}, {"P2", IMP}, {"P3", CHEF}, {"P4", SAINT}, {"P5", MAYOR},
       {"P6", WASHERWOMAN}, {"P7", DRUNK}},
      {{"P1", BARON}, {"P2", IMP}, {"P3", CHEF}, {"P4", SAINT}, {"P5", MAYOR},
       {"P6", DRUNK}, {"P7", MONK}},
      {{"P1", BARON}, {"P2", IMP}, {"P3", DRUNK}, {"P4", SAINT}, {"P5", MAYOR},
       {"P6", WASHERWOMAN}, {"P7", MONK}},
      {{"P1", BARON}, {"P2", IMP}, {"P3", CHEF}, {"P4", SAINT}, {"P5", DRUNK},
       {"P6", WASHERWOMAN}, {"P7", MONK}}});
  EXPECT_WORLDS_EQ(g.Solve(), expected_worlds);
}

TEST(WorldEnumeration, MinionPerspectivePoisonerFull) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", POISONER);
  g.AddMinionInfo("P1", "P2", {});  // P1 Poisoner, P2 Imp
  g.AddDay(1);
  g.AddAllClaims(
      {EMPATH, SLAYER, CHEF, VIRGIN, SOLDIER, WASHERWOMAN, MONK}, "P1");
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", POISONER}, {"P2", IMP}, {"P3", CHEF}, {"P4", VIRGIN},
       {"P5", SOLDIER}, {"P6", WASHERWOMAN}, {"P7", MONK}}});
  EXPECT_WORLDS_EQ(g.Solve(), expected_worlds);
}

TEST(WorldEnumeration, MinionPerspectivePoisoner5Players) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", POISONER);  // P1 Poisoner, but they don't know the Imp
  g.AddDay(1);
  g.AddAllClaims({EMPATH, SLAYER, CHEF, VIRGIN, SOLDIER}, "P1");
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", POISONER}, {"P2", SLAYER}, {"P3", CHEF}, {"P4", VIRGIN},
       {"P5", IMP}},
      {{"P1", POISONER}, {"P2", SLAYER}, {"P3", CHEF}, {"P4", IMP},
       {"P5", SOLDIER}},
      {{"P1", POISONER}, {"P2", IMP}, {"P3", CHEF}, {"P4", VIRGIN},
       {"P5", SOLDIER}},
      {{"P1", POISONER}, {"P2", SLAYER}, {"P3", IMP}, {"P4", VIRGIN},
       {"P5", SOLDIER}}});
  EXPECT_WORLDS_EQ(g.Solve(), expected_worlds);
}

TEST(WorldEnumeration, MinionPerspectivePoisoner5PlayersFull) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", POISONER);  // P1 Poisoner, but they don't know the Imp
  g.AddDay(1);
  g.AddAllClaims({EMPATH, SAINT, CHEF, VIRGIN, SOLDIER}, "P1");
  // P2 claimed outsider, which P1 knows is a lie. Therefore, P1 deduces P2 is
  // the Imp.
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", POISONER}, {"P2", IMP}, {"P3", CHEF}, {"P4", VIRGIN},
       {"P5", SOLDIER}}});
  EXPECT_WORLDS_EQ(g.Solve(), expected_worlds);
}

TEST(WorldEnumeration, DemonPerspective7Players) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2"}, {EMPATH, CHEF, SOLDIER});
  g.AddDay(1);
  g.AddAllClaims(
      {EMPATH, SAINT, UNDERTAKER, VIRGIN, MAYOR, SLAYER, RAVENKEEPER}, "P1");
  // No true outsider claims, so no Baron
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", SPY}, {"P3", UNDERTAKER}, {"P4", VIRGIN},
       {"P5", MAYOR}, {"P6", SLAYER}, {"P7", RAVENKEEPER}},
      {{"P1", IMP}, {"P2", POISONER}, {"P3", UNDERTAKER}, {"P4", VIRGIN},
       {"P5", MAYOR}, {"P6", SLAYER}, {"P7", RAVENKEEPER}},
      {{"P1", IMP}, {"P2", SCARLET_WOMAN}, {"P3", UNDERTAKER}, {"P4", VIRGIN},
       {"P5", MAYOR}, {"P6", SLAYER}, {"P7", RAVENKEEPER}}});
  EXPECT_WORLDS_EQ(g.Solve(), expected_worlds);
}

TEST(WorldEnumeration, InvalidDemonPerspective7Players) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2"}, {EMPATH, CHEF, SOLDIER});
  g.AddDay(1);
  g.AddAllClaims(
      {EMPATH, SAINT, CHEF, VIRGIN, MAYOR, SLAYER, RAVENKEEPER}, "P1");
  // This is impossible, since Chef is a demon bluff.
  EXPECT_EQ(g.ValidWorld().worlds_size(), 0);
}

TEST(Chef, LearnsNumber_0) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", CHEF);
  g.AddChefInfo("P1", 0);
  g.AddDay(1);
  g.AddAllClaims({CHEF, MAYOR, VIRGIN, SLAYER, RECLUSE}, "P1");
  g.AddClaimChefInfo("P1", 0);
  SolverRequest request = SolverRequestBuilder::FromCurrentRoles({
      {"P1", CHEF}, {"P2", IMP}, {"P3", DRUNK}, {"P4", BARON},
      {"P5", RECLUSE}});
  EXPECT_EQ(g.ValidWorld(request).worlds_size(), 1);
  request = SolverRequestBuilder::FromCurrentRoles({
      {"P1", CHEF}, {"P2", IMP}, {"P3", VIRGIN}, {"P4", SLAYER},
      {"P5", POISONER}});
  EXPECT_EQ(g.ValidWorld(request).worlds_size(), 1);
  request = SolverRequestBuilder::FromCurrentRoles({
      {"P1", DRUNK}, {"P2", IMP}, {"P3", BARON}, {"P4", SLAYER},
      {"P5", RECLUSE}});  // Drunk Chef
  EXPECT_EQ(g.ValidWorld(request).worlds_size(), 1);
  request = SolverRequestBuilder::FromCurrentRoles({
      {"P1", CHEF}, {"P2", MAYOR}, {"P3", VIRGIN}, {"P4", POISONER},
      {"P5", IMP}});  // Poisoned Chef.
  EXPECT_EQ(g.ValidWorld(request).worlds_size(), 1);
  request = SolverRequestBuilder::FromCurrentRoles({
      {"P1", CHEF}, {"P2", MAYOR}, {"P3", VIRGIN}, {"P4", SPY},
      {"P5", IMP}});  // Spy reads as Good.
  EXPECT_EQ(g.ValidWorld(request).worlds_size(), 1);
  request = SolverRequestBuilder::FromCurrentRoles({
      {"P1", CHEF}, {"P2", MAYOR}, {"P3", VIRGIN}, {"P4", SCARLET_WOMAN},
      {"P5", IMP}});
  EXPECT_EQ(g.ValidWorld(request).worlds_size(), 0);
}

TEST(Chef, LearnsNumber_1) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P3", CHEF);
  g.AddChefInfo("P3", 0);
  g.AddDay(1);
  g.AddAllClaims({RAVENKEEPER, MAYOR, CHEF, SLAYER, RECLUSE}, "P1");
  g.AddClaimChefInfo("P3", 1);
  SolverRequest request = SolverRequestBuilder::FromCurrentRoles({
      {"P1", DRUNK}, {"P2", IMP}, {"P3", CHEF}, {"P4", BARON},
      {"P5", RECLUSE}});
  EXPECT_EQ(g.ValidWorld(request).worlds_size(), 1);
}

TEST(Investigator, DemonLearnsMinionRole) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2"}, {EMPATH, CHEF, SOLDIER});
  g.AddDay(1);
  g.AddAllClaims(
      {EMPATH, SAINT, INVESTIGATOR, VIRGIN, MAYOR, SLAYER, RAVENKEEPER}, "P1");
  // Minion claims only outsider, so no Baron.
  g.AddClaimInvestigatorInfo("P3", "P2", "P5", POISONER);
  // Minion can only be a Poisoner:
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", POISONER}, {"P3", INVESTIGATOR}, {"P4", VIRGIN},
       {"P5", MAYOR}, {"P6", SLAYER}, {"P7", RAVENKEEPER}}});
  EXPECT_WORLDS_EQ(g.Solve(), expected_worlds);
}

TEST(Washerwoman, VirginConfirmsWasherwoman) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", BARON);
  g.AddMinionInfo("P1", "P2", {});
  g.AddDay(1);
  g.AddAllClaims(
      {EMPATH, SAINT, WASHERWOMAN, VIRGIN, MAYOR, SLAYER, RECLUSE}, "P1");
  // Baron learns that P6 is the Drunk.
  g.AddClaimWasherwomanInfo("P3", "P4", "P5", MAYOR);
  g.AddNomination("P3", "P4");
  g.AddExecution("P3");
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", BARON}, {"P2", IMP}, {"P3", WASHERWOMAN}, {"P4", VIRGIN},
       {"P5", MAYOR}, {"P6", DRUNK}, {"P7", RECLUSE}}});
  EXPECT_WORLDS_EQ(g.Solve(), expected_worlds);
}

TEST(Librarian, VirginConfirmsLibrarian) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", BARON);
  g.AddMinionInfo("P1", "P2", {});
  g.AddDay(1);
  g.AddAllClaims(
      {EMPATH, SAINT, LIBRARIAN, VIRGIN, MAYOR, SLAYER, RECLUSE}, "P1");
  // Baron learns that P6 is the Drunk.
  g.AddClaimLibrarianInfo("P3", "P1", "P6", DRUNK);
  g.AddNomination("P3", "P4");
  g.AddExecution("P3");
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", BARON}, {"P2", IMP}, {"P3", LIBRARIAN}, {"P4", VIRGIN},
       {"P5", MAYOR}, {"P6", DRUNK}, {"P7", RECLUSE}}});
  EXPECT_WORLDS_EQ(g.Solve(), expected_worlds);
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
  g.AddAllClaims(
      {EMPATH, MAYOR, CHEF, VIRGIN, SAINT, SOLDIER, INVESTIGATOR}, "P1");
  g.AddNomination("P3", "P4");
  g.AddExecution("P3");
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", BARON}, {"P2", IMP}, {"P3", CHEF}, {"P4", VIRGIN},
       {"P5", SAINT}, {"P6", DRUNK}, {"P7", INVESTIGATOR}},
      {{"P1", BARON}, {"P2", IMP}, {"P3", CHEF}, {"P4", VIRGIN},
       {"P5", SAINT}, {"P6", SOLDIER}, {"P7", DRUNK}}});
  EXPECT_WORLDS_EQ(g.Solve(), expected_worlds);
}

TEST(Virgin, DrunkVirginNonProc) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", BARON);
  g.AddMinionInfo("P1", "P2", {});  // P1 Baron, P2 Imp
  g.AddDay(1);
  g.AddAllClaims(
      {EMPATH, MAYOR, CHEF, VIRGIN, SAINT, SOLDIER, INVESTIGATOR}, "P1");
  g.AddNomination("P3", "P4");
  g.AddNoStorytellerAnnouncement();  // No proc.
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", BARON}, {"P2", IMP}, {"P3", CHEF}, {"P4", DRUNK},
       {"P5", SAINT}, {"P6", SOLDIER}, {"P7", INVESTIGATOR}},
      {{"P1", BARON}, {"P2", IMP}, {"P3", DRUNK}, {"P4", VIRGIN},
       {"P5", SAINT}, {"P6", SOLDIER}, {"P7", INVESTIGATOR}}});
  EXPECT_WORLDS_EQ(g.Solve(), expected_worlds);
}

TEST(Virgin, PoisonedVirginNonProc) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", POISONER);
  g.AddMinionInfo("P1", "P2", {});
  g.AddPoisonerAction("P1", "P4");
  g.AddDay(1);
  g.AddAllClaims(
      {MONK, SAINT, CHEF, VIRGIN, EMPATH, SOLDIER, INVESTIGATOR}, "P1");
  g.AddNomination("P5", "P4");
  g.AddNoStorytellerAnnouncement();  // No proc.
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", POISONER}, {"P2", IMP}, {"P3", CHEF}, {"P4", VIRGIN},
       {"P5", EMPATH}, {"P6", SOLDIER}, {"P7", INVESTIGATOR}}});
  EXPECT_WORLDS_EQ(g.Solve(), expected_worlds);
}

TEST(Undertaker, HealthyVirginProcDrunkUndertaker) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", BARON);
  g.AddMinionInfo("P1", "P2", {});  // P1 Baron, P2 Imp
  g.AddDay(1);
  g.AddAllClaims(
      {EMPATH, MAYOR, CHEF, VIRGIN, SAINT, SOLDIER, UNDERTAKER}, "P1");
  g.AddNomination("P3", "P4");
  g.AddExecution("P3");
  g.AddDeath("P3");
  g.AddNight(2);
  g.AddDay(2);
  g.AddClaimUndertakerInfo("P7", IMP);  // Storyteller would not do this...
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", BARON}, {"P2", IMP}, {"P3", CHEF}, {"P4", VIRGIN},
       {"P5", SAINT}, {"P6", SOLDIER}, {"P7", DRUNK}}});
  EXPECT_WORLDS_EQ(g.Solve(), expected_worlds);
}

TEST(Undertaker, SpyFalseRegisters) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", UNDERTAKER);
  g.AddDay(1);
  g.AddAllClaims({UNDERTAKER, MAYOR, VIRGIN, SLAYER, INVESTIGATOR}, "P1");
  g.AddClaimInvestigatorInfo("P5", "P1", "P2", POISONER);
  g.AddNomination("P5", "P3");
  g.AddExecution("P5");
  g.AddDeath("P5");
  g.AddNight(2);
  g.AddUndertakerInfo("P1", INVESTIGATOR);
  g.AddDay(2);
  g.AddClaimUndertakerInfo("P1", INVESTIGATOR);
  SolverRequest r = SolverRequestBuilder::FromCurrentRoles("P5", SPY);
  EXPECT_EQ(g.ValidWorld(r).worlds_size(), 1);
  r = SolverRequestBuilder::FromCurrentRoles("P5", INVESTIGATOR);
  EXPECT_EQ(g.ValidWorld(r).worlds_size(), 1);
  r = SolverRequestBuilder()
      .AddRolesNotInPlay({SPY})
      .AddCurrentRolesNot("P5", INVESTIGATOR)
      .Build();
  EXPECT_EQ(g.ValidWorld(r).worlds_size(), 0);
}

TEST(Undertaker, RecluseFalseRegisters) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", UNDERTAKER);
  g.AddDay(1);
  g.AddAllClaims({UNDERTAKER, MAYOR, VIRGIN, SLAYER, RECLUSE}, "P1");
  g.AddNomination("P2", "P5");
  g.AddVote({"P1", "P2", "P3"}, "P5");
  g.AddExecution("P5");
  g.AddDeath("P5");
  g.AddNight(2);
  g.AddUndertakerInfo("P1", IMP);
  g.AddDay(2);
  g.AddClaimUndertakerInfo("P1", IMP);
  SolverRequest r = SolverRequestBuilder::FromCurrentRoles("P5", RECLUSE);
  EXPECT_EQ(g.ValidWorld(r).worlds_size(), 1);
  r = SolverRequestBuilder::FromCurrentRoles("P5", IMP);
  EXPECT_EQ(g.ValidWorld(r).worlds_size(), 1);
  r = SolverRequestBuilder()
      .AddCurrentRolesNot("P5", IMP)
      .AddCurrentRolesNot("P5", RECLUSE)
      .Build();
  EXPECT_EQ(g.ValidWorld(r).worlds_size(), 0);
}

TEST(Undertaker, HealthyUndertakerUseless) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", BARON);
  g.AddMinionInfo("P1", "P2", {});  // P1 Baron, P2 Imp
  g.AddDay(1);
  g.AddAllClaims(
      {EMPATH, MAYOR, CHEF, VIRGIN, SAINT, SOLDIER, UNDERTAKER}, "P1");
  g.AddNomination("P3", "P4");
  g.AddExecution("P3");
  g.AddDeath("P3");
  g.AddNight(2);
  g.AddDay(2);
  g.AddClaimUndertakerInfo("P7", CHEF);  // P7 could still be the DRUNK
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", BARON}, {"P2", IMP}, {"P3", CHEF}, {"P4", VIRGIN},
       {"P5", SAINT}, {"P6", DRUNK}, {"P7", UNDERTAKER}},
      {{"P1", BARON}, {"P2", IMP}, {"P3", CHEF}, {"P4", VIRGIN},
       {"P5", SAINT}, {"P6", SOLDIER}, {"P7", DRUNK}}});
  EXPECT_WORLDS_EQ(g.Solve(), expected_worlds);
  g.AddNomination("P1", "P6");
  g.AddVote({"P1", "P2", "P4"}, "P6");
  g.AddExecution("P6");
  g.AddDeath("P6");
  g.AddNight(3);
  g.AddDay(3);
  g.AddClaimUndertakerInfo("P7", DRUNK);  // And we still can't tell:
  EXPECT_WORLDS_EQ(g.Solve(), expected_worlds);
}

TEST(Executions, CorrectGameState) {
  GameState g = GameState::FromObserverPerspective(MakePlayers(5));
  g.AddNight(1);
  g.AddDay(1);
  g.AddNomination("P1", "P1");
  g.AddVote({"P2", "P3", "P4"}, "P1");
  EXPECT_EQ(g.OnTheBlock(), "P1");
  g.AddExecution("P1");
  EXPECT_EQ(g.Execution(), "P1");
  g.AddDeath("P1");
  EXPECT_EQ(g.ExecutionDeath(), "P1");
  EXPECT_EQ(g.NumAlive(), 4);
  EXPECT_FALSE(g.IsAlive("P1"));
  g.AddNight(2);
  g.AddDay(2);
  EXPECT_EQ(g.NumAlive(), 4);
  EXPECT_EQ(g.OnTheBlock(), "");
  EXPECT_EQ(g.Execution(), "");
  EXPECT_EQ(g.ExecutionDeath(), "");
}

TEST(NightDeaths, CorrectGameState) {
  GameState g = GameState::FromObserverPerspective(MakePlayers(5));
  g.AddNight(1);
  g.AddDay(1);
  g.AddNight(2);
  g.AddDay(2);
  g.AddDeath("P1");
  EXPECT_EQ(g.NumAlive(), 4);
  EXPECT_FALSE(g.IsAlive("P1"));
  EXPECT_EQ(g.NightDeath(), "P1");
}

TEST(NightDeaths, ImpDeducesPoisoner) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2"}, {EMPATH, RECLUSE, MONK});
  g.AddDay(1);
  g.AddAllClaims(
      {MAYOR, SAINT, CHEF, RAVENKEEPER, SOLDIER, SLAYER, FORTUNE_TELLER}, "P1");
  // Minion is not a Baron, but other options are all in.
  g.AddNight(2);
  g.AddImpAction("P1", "P5");
  g.AddDay(2);
  g.AddDeath("P5");  // Imp successfully kills Soldier, hence P2 is Poisoner.
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", POISONER}, {"P3", CHEF}, {"P4", RAVENKEEPER},
       {"P5", SOLDIER}, {"P6", SLAYER}, {"P7", FORTUNE_TELLER}}});
  EXPECT_WORLDS_EQ(g.Solve(), expected_worlds);
}

TEST(NightDeaths, ImpDeducesSoberMonk) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2"}, {EMPATH, RECLUSE, FORTUNE_TELLER});
  g.AddDay(1);
  g.AddAllClaims(
      {MAYOR, RAVENKEEPER, CHEF, SAINT, SOLDIER, SLAYER, MONK}, "P1");
  g.AddNight(2);
  g.AddImpAction("P1", "P6");
  g.AddDay(2);
  g.AddNoStorytellerAnnouncement();  // No deaths -> Monk is sober.
  const SolverRequest& r = SolverRequestBuilder::FromCurrentRoles("P7", MONK);
  EXPECT_EQ(g.Solve(r).worlds_size(), g.Solve().worlds_size());
}

TEST(NightDeaths, ImpDeducesDrunkSoldier) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2"}, {EMPATH, RECLUSE, FORTUNE_TELLER});
  g.AddDay(1);
  g.AddAllClaims(
      {MAYOR, RAVENKEEPER, CHEF, SAINT, SOLDIER, SLAYER, MONK}, "P1");
  g.AddNight(2);
  g.AddImpAction("P1", "P5");
  g.AddDay(2);
  g.AddDeath("P5");
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", BARON}, {"P3", CHEF}, {"P4", SAINT},
       {"P5", DRUNK}, {"P6", SLAYER}, {"P7", MONK}}});
  EXPECT_WORLDS_EQ(g.Solve(), expected_worlds);
}

TEST(NightDeaths, MayorBounceToSoldier) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2"}, {EMPATH, RECLUSE, FORTUNE_TELLER});
  g.AddDay(1);
  g.AddAllClaims(
      {EMPATH, RAVENKEEPER, CHEF, SAINT, SOLDIER, MAYOR, UNDERTAKER}, "P1");
  g.AddNight(2);
  g.AddImpAction("P1", "P6");
  g.AddDay(2);
  g.AddNoStorytellerAnnouncement();  // No deaths: Mayor->Soldier bounce.
  // That means both the Mayor and the Soldier are sober:
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", BARON}, {"P3", CHEF}, {"P4", SAINT},
       {"P5", SOLDIER}, {"P6", MAYOR}, {"P7", DRUNK}},
      {{"P1", IMP}, {"P2", BARON}, {"P3", DRUNK}, {"P4", SAINT},
       {"P5", SOLDIER}, {"P6", MAYOR}, {"P7", UNDERTAKER}}});
  EXPECT_WORLDS_EQ(g.Solve(), expected_worlds);
}

TEST(NightDeaths, MayorBounceToMonkProtectedTarget) {
  GameState g = FromRoles({IMP, BARON, MONK, MAYOR, SAINT, RECLUSE, EMPATH});
  g.AddNight(1);
  g.AddAllShownTokens({IMP, BARON, MONK, MAYOR, SAINT, RECLUSE, EMPATH});
  g.AddDemonInfo("P1", {"P2"}, {RAVENKEEPER, FORTUNE_TELLER, SOLDIER});
  g.AddMinionInfo("P2", "P1", {});
  g.AddDay(1);
  g.AddAllClaims(
      {RAVENKEEPER, SOLDIER, MONK, MAYOR, SAINT, RECLUSE, EMPATH}, "P1");
  g.AddNight(2);
  g.AddMonkAction("P3", "P7");  // Monk is protecting P7, not the Mayor.
  g.AddImpAction("P1", "P4");  // Imp tries to kill the Mayor.
  g.AddDay(2);
  g.AddClaimMonkAction("P3", "P7");  // Kill bounced to P7.
  EXPECT_EQ(g.ValidWorld().worlds_size(), 1);
}

TEST(NightDeaths, MayorBounce) {
  GameState g = FromRoles({IMP, BARON, MAYOR, SAINT, RECLUSE});
  g.AddNight(1);
  g.AddAllShownTokens({IMP, BARON, MAYOR, SAINT, RECLUSE});
  g.AddDay(1);
  g.AddAllClaims({RAVENKEEPER, SOLDIER, MAYOR, SAINT, RECLUSE}, "P1");
  g.AddNight(2);
  g.AddImpAction("P1", "P3");  // Imp tries to kill the Mayor.
  g.AddDay(2);
  g.AddDeath("P5");
  EXPECT_EQ(g.ValidWorld().worlds_size(), 1);
}

TEST(Ravenkeeper, SpyFalseRegisters) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", RAVENKEEPER);
  g.AddDay(1);
  g.AddAllClaims({RAVENKEEPER, MAYOR, VIRGIN, SLAYER, INVESTIGATOR}, "P1");
  g.AddClaimInvestigatorInfo("P5", "P1", "P2", POISONER);
  g.AddNomination("P5", "P3");
  g.AddExecution("P5");
  g.AddDeath("P5");
  g.AddNight(2);
  g.AddRavenkeeperAction("P1", "P5", INVESTIGATOR);
  g.AddDay(2);
  g.AddDeath("P1");
  g.AddClaimRavenkeeperAction("P1", "P5", INVESTIGATOR);
  SolverRequest request = SolverRequestBuilder::FromCurrentRoles("P5", SPY);
  EXPECT_EQ(g.ValidWorld(request).worlds_size(), 1);
  request = SolverRequestBuilder::FromCurrentRoles("P5", INVESTIGATOR);
  EXPECT_EQ(g.ValidWorld(request).worlds_size(), 1);
  request = SolverRequestBuilder()
      .AddRolesNotInPlay({SPY})
      .AddCurrentRolesNot("P5", INVESTIGATOR)
      .Build();
  EXPECT_EQ(g.ValidWorld(request).worlds_size(), 0);
}

TEST(Ravenkeeper, RecluseFalseRegisters) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", RAVENKEEPER);
  g.AddDay(1);
  g.AddAllClaims({RAVENKEEPER, MAYOR, VIRGIN, SLAYER, RECLUSE}, "P1");
  g.AddNomination("P5", "P5");
  g.AddVote({"P1", "P2", "P3"}, "P5");
  g.AddExecution("P5");
  g.AddDeath("P5");
  g.AddNight(2);
  g.AddRavenkeeperAction("P1", "P5", IMP);
  g.AddDay(2);
  g.AddDeath("P1");
  g.AddClaimRavenkeeperAction("P1", "P5", IMP);
  SolverRequest request =
      SolverRequestBuilder::FromCurrentRoles("P5", RECLUSE);
  EXPECT_EQ(g.ValidWorld(request).worlds_size(), 1);
  request = SolverRequestBuilder::FromCurrentRoles("P5", IMP);
  EXPECT_EQ(g.ValidWorld(request).worlds_size(), 1);
  request = SolverRequestBuilder()
      .AddCurrentRolesNot("P5", RECLUSE)
      .AddCurrentRolesNot("P5", IMP)
      .Build();
  EXPECT_EQ(g.ValidWorld(request).worlds_size(), 0);
}

TEST(Ravenkeeper, LearnsTrueRole) {
  GameState g = FromRoles({IMP, BARON, RAVENKEEPER, RECLUSE, SAINT});
  g.AddNight(1);
  g.AddAllShownTokens({IMP, BARON, RAVENKEEPER, RECLUSE, SAINT});
  g.AddDay(1);
  g.AddAllClaims({MAYOR, SLAYER, RAVENKEEPER, RECLUSE, SAINT}, "P1");
  g.AddNight(2);
  g.AddImpAction("P1", "P3");
  g.AddRavenkeeperAction("P3", "P2", BARON);
  g.AddDay(2);
  g.AddDeath("P3");
  g.AddClaimRavenkeeperAction("P3", "P2", BARON);
  EXPECT_EQ(g.ValidWorld().worlds_size(), 1);
}

TEST(Ravenkeeper, DrunkRavenkeeperLearnsFalseRole) {
  GameState g = FromRoles({IMP, BARON, DRUNK, SLAYER, SAINT});
  g.AddNight(1);
  g.AddAllShownTokens({IMP, BARON, RAVENKEEPER, SLAYER, SAINT});
  g.AddDay(1);
  g.AddAllClaims({MAYOR, UNDERTAKER, RAVENKEEPER, SLAYER, SAINT}, "P1");
  g.AddNight(2);
  g.AddImpAction("P1", "P3");
  g.AddRavenkeeperAction("P3", "P2", UNDERTAKER);
  g.AddDay(2);
  g.AddDeath("P3");
  g.AddClaimRavenkeeperAction("P3", "P2", UNDERTAKER);
  EXPECT_EQ(g.ValidWorld().worlds_size(), 1);
}

TEST(Ravenkeeper, PoisonedRavenkeeperLearnsFalseRole) {
  GameState g = FromRoles({IMP, POISONER, RAVENKEEPER, SLAYER, UNDERTAKER});
  g.AddNight(1);
  g.AddAllShownTokens({IMP, POISONER, RAVENKEEPER, SLAYER, UNDERTAKER});
  g.AddDay(1);
  g.AddAllClaims({MAYOR, SAINT, RAVENKEEPER, SLAYER, UNDERTAKER}, "P1");
  g.AddNight(2);
  g.AddPoisonerAction("P2", "P3");
  g.AddImpAction("P1", "P3");
  g.AddRavenkeeperAction("P3", "P2", SAINT);
  g.AddDay(2);
  g.AddDeath("P3");
  g.AddClaimRavenkeeperAction("P3", "P2", SAINT);
  EXPECT_EQ(g.ValidWorld().worlds_size(), 1);
}

TEST(Ravenkeeper, InvalidRavenkeeperLearnsFalseRole) {
  GameState g = FromRoles({IMP, POISONER, RAVENKEEPER, SLAYER, UNDERTAKER});
  g.AddNight(1);
  g.AddAllShownTokens({IMP, POISONER, RAVENKEEPER, SLAYER, UNDERTAKER});
  g.AddDay(1);
  g.AddAllClaims({MAYOR, SAINT, RAVENKEEPER, SLAYER, UNDERTAKER}, "P1");
  g.AddNight(2);
  g.AddPoisonerAction("P2", "P4");  // Ravenkeeper is healthy.
  g.AddImpAction("P1", "P3");
  g.AddRavenkeeperAction("P3", "P2", SAINT);  // Should learn POISONER.
  g.AddDay(2);
  g.AddDeath("P3");
  g.AddClaimRavenkeeperAction("P3", "P2", SAINT);
  EXPECT_EQ(g.ValidWorld().worlds_size(), 0);
}

TEST(FortuneTeller, LearnsTrueInfo) {
  GameState g = FromRolesWithRedHerring(
      {IMP, BARON, FORTUNE_TELLER, RECLUSE, SAINT}, "P5");
  g.AddNight(1);
  g.AddAllShownTokens({IMP, BARON, FORTUNE_TELLER, RECLUSE, SAINT});
  g.AddFortuneTellerAction("P3", "P1", "P2", true);
  g.AddDay(1);
  g.AddAllClaims({MAYOR, SLAYER, FORTUNE_TELLER, RECLUSE, SAINT}, "P1");
  g.AddClaimFortuneTellerAction("P3", "P1", "P2", true);  // P1 is Imp
  g.AddNight(2);
  g.AddImpAction("P1", "P2");
  g.AddFortuneTellerAction("P3", "P3", "P4", true);
  g.AddDay(2);
  g.AddDeath("P2");
  g.AddClaimFortuneTellerAction("P3", "P3", "P4", true);  // P4 is Recluse
  g.AddNight(3);
  g.AddImpAction("P1", "P2");
  g.AddFortuneTellerAction("P3", "P3", "P5", true);
  g.AddDay(3);
  g.AddClaimFortuneTellerAction("P3", "P3", "P5", true);  // P5 is red herring
  g.AddNight(4);
  g.AddImpAction("P1", "P2");
  g.AddFortuneTellerAction("P3", "P4", "P2", false);
  g.AddDay(4);
  g.AddClaimFortuneTellerAction("P3", "P4", "P2", false);  // Recluse no-proc.
  EXPECT_EQ(g.ValidWorld().worlds_size(), 1);
}

TEST(Empath, LearnsTrueInfo) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(10));
  g.AddNight(1);
  g.AddShownToken("P1", EMPATH);
  g.AddEmpathInfo("P1", 2);
  g.AddDay(1);
  // Virgin (P2) is actually the Spy, Saint (P7): Imp, Mayor (P8): Baron.
  g.AddAllClaims({EMPATH, VIRGIN, SOLDIER, MAYOR, SLAYER, RAVENKEEPER,
                  SAINT, MAYOR, CHEF, RECLUSE}, "P1");
  g.AddClaimEmpathInfo("P1", 2);  // Both Spy and Recluse proc evil
  g.AddNomination("P10", "P4");
  g.AddVote({"P1", "P2", "P3", "P8", "P9"}, "P4");
  g.AddExecution("P4");
  g.AddDeath("P4");
  g.AddNight(2);
  g.AddEmpathInfo("P1", 0);
  g.AddDay(2);
  g.AddDeath("P5");
  g.AddClaimEmpathInfo("P1", 0);  // Both Spy and Recluse proc good
  g.AddNomination("P10", "P10");
  g.AddVote({"P1", "P2", "P3", "P8"}, "P10");
  g.AddExecution("P10");
  g.AddDeath("P10");
  g.AddNight(3);
  g.AddEmpathInfo("P1", 1);  // Spy Evil again
  g.AddDay(3);
  g.AddClaimEmpathInfo("P1", 1);
  g.AddNomination("P1", "P9");
  g.AddVote({"P1", "P2", "P3", "P8"}, "P9");
  g.AddExecution("P9");
  g.AddDeath("P9");
  g.AddNight(4);
  g.AddEmpathInfo("P1", 1);  // Spy Good again, pings off Baron
  g.AddDay(4);
  g.AddClaimEmpathInfo("P1", 1);
  g.AddNomination("P1", "P8");
  g.AddVote({"P1", "P2", "P3"}, "P8");
  g.AddExecution("P8");
  g.AddDeath("P8");
  g.AddNight(5);
  g.AddEmpathInfo("P1", 1);  // Spy Good, ping off Imp
  g.AddDay(5);
  g.AddClaimEmpathInfo("P1", 1);
  const SolverRequest& r = SolverRequestBuilder::FromCurrentRoles({
      {"P1", EMPATH}, {"P2", SPY}, {"P3", SOLDIER}, {"P4", MAYOR},
      {"P5", SLAYER}, {"P6", RAVENKEEPER}, {"P7", IMP}, {"P8", BARON},
      {"P9", DRUNK}, {"P10", RECLUSE}});
  EXPECT_EQ(g.ValidWorld(r).worlds_size(), 1);
}

TEST(Slayer, ImpDeducesDrunkSlayer) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2"}, {EMPATH, RECLUSE, FORTUNE_TELLER});
  g.AddDay(1);
  g.AddAllClaims(
      {MAYOR, RAVENKEEPER, CHEF, SAINT, SOLDIER, SLAYER, MONK}, "P1");
  g.AddSlayerAction("P6", "P1");
  g.AddNoStorytellerAnnouncement();
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", BARON}, {"P3", CHEF}, {"P4", SAINT},
       {"P5", SOLDIER}, {"P6", DRUNK}, {"P7", MONK}}});
  EXPECT_WORLDS_EQ(g.Solve(), expected_worlds);
}

TEST(ScarletWomanProc, ExecuteImp) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P5", SCARLET_WOMAN);
  g.AddMinionInfo("P5", "P1", {});  // P5 SW, P1 Imp
  g.AddDay(1);
  g.AddAllClaims(
      {SOLDIER, MAYOR, CHEF, VIRGIN, FORTUNE_TELLER, SLAYER, MONK}, "P1");
  g.AddNomination("P2", "P1");
  g.AddVote({"P2", "P3", "P4", "P6"}, "P1");
  g.AddExecution("P1");
  g.AddDeath("P1");
  g.AddNight(2);
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", MAYOR}, {"P3", CHEF}, {"P4", VIRGIN},
       {"P5", IMP}, {"P6", SLAYER}, {"P7", MONK}}});
  EXPECT_WORLDS_EQ(g.Solve(), expected_worlds);
}

TEST(ScarletWomanProc, SlayerKillsImp) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P5", SCARLET_WOMAN);
  g.AddMinionInfo("P5", "P1", {});  // P5 SW, P1 Imp
  g.AddDay(1);
  g.AddAllClaims(
      {SOLDIER, MAYOR, CHEF, VIRGIN, FORTUNE_TELLER, SLAYER, MONK}, "P1");
  g.AddSlayerAction("P6", "P1");
  g.AddDeath("P1");
  g.AddNight(2);
  vector<unordered_map<string, Role>> expected_worlds({
      {{"P1", IMP}, {"P2", MAYOR}, {"P3", CHEF}, {"P4", VIRGIN},
       {"P5", IMP}, {"P6", SLAYER}, {"P7", MONK}}});
  EXPECT_WORLDS_EQ(g.Solve(), expected_worlds);
}

TEST(Soldier, InvalidImpKillsHealthySoldier) {
  GameState g = GameState::FromObserverPerspective(MakePlayers(5));
  g.AddNight(1);
  g.AddDay(1);
  g.AddAllClaims({WASHERWOMAN, RECLUSE, SAINT, BUTLER, SOLDIER}, "P1");
  g.AddClaimWasherwomanInfo("P1", "P4", "P5", SOLDIER);
  const SolverRequest& r =
      SolverRequestBuilder::FromCurrentRoles("P5", SOLDIER);
  // P5 is SOLDIER in all worlds:
  EXPECT_EQ(g.Solve(r).worlds_size(), g.Solve().worlds_size());
  g.AddNight(2);
  g.AddDay(2);
  g.AddDeath("P5");
  EXPECT_EQ(g.ValidWorld().worlds_size(), 0);
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
  EXPECT_EQ(g.ValidWorld().worlds_size(), 1);
  SolverRequest request;
  auto* pr = request.mutable_assumptions()->add_current_roles();
  pr->set_player("P1");
  pr->set_role(IMP);
  pr->set_is_not(true);
  EXPECT_EQ(g.ValidWorld(request).worlds_size(), 0);
  pr->set_is_not(false);
  request.mutable_assumptions()->add_roles_in_play(SCARLET_WOMAN);
  // Because the SW would have proc-ed.
  EXPECT_EQ(g.ValidWorld(request).worlds_size(), 0);
}

TEST(GameEndConditions, InvalidExecuteImpOn4GameNotOver) {
  GameState g = GameState::FromObserverPerspective(MakePlayers(5));
  g.AddNight(1);
  g.AddDay(1);
  g.AddNight(2);
  g.AddDay(2);
  g.AddDeath("P1");
  g.AddNomination("P2", "P3");
  g.AddVote({"P4", "P5"}, "P3");
  g.AddExecution("P3");
  g.AddDeath("P3");
  g.AddNight(3);  // The game continues, so P3 could not have been the Imp.
  SolverRequest r = SolverRequestBuilder::FromCurrentRoles("P3", IMP);
  EXPECT_EQ(g.ValidWorld(r).worlds_size(), 0);
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
  SolverRequest request = SolverRequestBuilder::FromCurrentRoles("P1", IMP);
  EXPECT_EQ(g.ValidWorld(request).worlds_size(), 1);
  request.mutable_assumptions()->add_roles_not_in_play(SCARLET_WOMAN);
  EXPECT_EQ(g.ValidWorld(request).worlds_size(), 0);
}

TEST(GameEndConditions, ExecuteSaintGameOver) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", SAINT);
  g.AddDay(1);
  g.AddAllClaims({SAINT, MAYOR, SOLDIER, SLAYER, RECLUSE}, "P1");
  g.AddNomination("P2", "P1");
  g.AddVote({"P2", "P3", "P4"}, "P1");
  g.AddExecution("P1");
  g.AddDeath("P1");
  g.AddVictory(EVIL);
  EXPECT_EQ(g.ValidWorld().worlds_size(), 1);
}

TEST(GameEndConditions, ExecuteSaintGameNotOverPoisoner) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(6));
  g.AddNight(1);
  g.AddShownToken("P1", SAINT);
  g.AddDay(1);
  g.AddAllClaims({SAINT, MAYOR, SOLDIER, SLAYER, MONK}, "P1");
  g.AddNomination("P2", "P1");
  g.AddVote({"P2", "P3", "P4"}, "P1");
  g.AddExecution("P1");
  g.AddDeath("P1");
  g.AddNight(2);
  EXPECT_EQ(g.ValidWorld().worlds_size(), 1);
  // Poisoner must have got us:
  SolverRequest request = SolverRequestBuilder()
      .AddRolesNotInPlay({POISONER}).Build();
  EXPECT_EQ(g.ValidWorld(request).worlds_size(), 0);
}

TEST(GameEndConditions, MayorWin) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", MAYOR);
  g.AddDay(1);
  g.AddAllClaims({MAYOR, SAINT, SOLDIER, SLAYER, RECLUSE}, "P1");
  g.AddNomination("P2", "P5");
  g.AddVote({"P3", "P4", "P5"}, "P5");
  g.AddExecution("P5");
  g.AddDeath("P5");
  g.AddNight(2);
  g.AddDay(2);
  g.AddDeath("P4");  // Final 3.
  g.AddVictory(GOOD);
  EXPECT_EQ(g.ValidWorld().worlds_size(), 1);
}

TEST(GameEndConditions, PoisonedMayorNoWin) {
  GameState g = GameState::FromPlayerPerspective(MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", MAYOR);
  g.AddDay(1);
  g.AddAllClaims({MAYOR, MONK, SOLDIER, SLAYER, RAVENKEEPER}, "P1");
  g.AddNomination("P2", "P5");
  g.AddVote({"P3", "P4", "P5"}, "P5");
  g.AddExecution("P5");
  g.AddDeath("P5");
  g.AddNight(2);
  g.AddDay(2);
  g.AddDeath("P4");  // Final 3.
  g.AddNight(3);
  g.AddDay(3);
  g.AddDeath("P2");
  g.AddVictory(EVIL);
  EXPECT_EQ(g.ValidWorld().worlds_size(), 1);
  // Poisoner must have got us:
  SolverRequest request = SolverRequestBuilder()
      .AddRolesNotInPlay({POISONER}).Build();
  EXPECT_EQ(g.ValidWorld(request).worlds_size(), 0);
}
}  // namespace
}  // namespace botc

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
