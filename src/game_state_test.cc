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

#include "src/game_state.h"

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

TEST(GameTime, TimeOperationsWork) {
  vector<string> times;
  for (Time t = Time::Night(1); t < Time::Day(3); ++t) {
    times.push_back(string(t));
  }
  EXPECT_THAT(times, testing::ElementsAreArray({
    "night_1", "day_1", "night_2", "day_2", "night_3"}));
  EXPECT_EQ(Time::Night(2) - 1, Time::Day(1));
  EXPECT_EQ(Time::Night(2) - 2, Time::Night(1));
  EXPECT_EQ(Time::Day(2) - 1, Time::Night(2));
  EXPECT_EQ(Time::Day(2) - 2, Time::Day(1));
  times.clear();
  for (Time t = Time::Night(1); t <= Time::Night(4); t += 2) {
    times.push_back(string(t));
  }
  EXPECT_THAT(times, testing::ElementsAreArray({
    "night_1", "night_2", "night_3", "night_4"}));
}

TEST(Proto, ToAndFromProto) {
  GameState g(STORYTELLER, TROUBLE_BREWING, MakePlayers(15));
  g.SetRoles({IMP, SPY, SCARLET_WOMAN, POISONER, BUTLER, DRUNK, WASHERWOMAN,
            LIBRARIAN, INVESTIGATOR, CHEF, EMPATH, FORTUNE_TELLER,
            UNDERTAKER, MONK, RAVENKEEPER});
  g.SetRedHerring("P11");
  g.AddNight(1);
  g.AddAllShownTokens({IMP, SPY, SCARLET_WOMAN, POISONER, BUTLER, SLAYER,
                      WASHERWOMAN, LIBRARIAN, INVESTIGATOR, CHEF,
                      EMPATH, FORTUNE_TELLER, UNDERTAKER, MONK,
                      RAVENKEEPER});
  g.AddDemonInfo("P1", {"P2", "P3", "P4"}, {VIRGIN, SOLDIER, MAYOR});
  g.AddMinionInfo("P2", "P1", {"P3", "P4"});
  g.AddMinionInfo("P3", "P1", {"P2", "P4"});
  g.AddMinionInfo("P4", "P1", {"P2", "P3"});
  g.AddRoleAction("P4", g.NewPoisonerAction("P9"));
  GrimoireInfo spy_info = g.GrimoireInfoFromRoles(
      {IMP, SPY, SCARLET_WOMAN, POISONER, BUTLER, DRUNK, WASHERWOMAN,
       LIBRARIAN, INVESTIGATOR, CHEF, EMPATH, FORTUNE_TELLER,
       UNDERTAKER, MONK, RAVENKEEPER}, SLAYER);
  g.AddRoleAction("P2", g.NewSpyInfo(spy_info));
  g.AddRoleAction("P7", g.NewWasherwomanInfo("P1", "P2", SOLDIER));
  g.AddRoleAction("P8", g.NewLibrarianInfo("P5", "P3", BUTLER));
  g.AddRoleAction("P9", g.NewInvestigatorInfo("P11", "P12", SCARLET_WOMAN));
  g.AddRoleAction("P10", g.NewChefInfo(3));
  g.AddRoleAction("P11", g.NewEmpathInfo(0));
  g.AddRoleAction("P12", g.NewFortuneTellerAction("P11", "P13", true));
  g.AddRoleAction("P5", g.NewButlerAction("P4"));
  g.AddDay(1);
  g.AddAllClaims(
    {SOLDIER, MAYOR, SOLDIER, SAINT, BUTLER, SLAYER, WASHERWOMAN,
      LIBRARIAN, INVESTIGATOR, CHEF, EMPATH, FORTUNE_TELLER, UNDERTAKER, MONK,
      RAVENKEEPER},
    "P1");
  g.AddClaim("P7", g.NewWasherwomanInfo("P1", "P2", SOLDIER));
  g.AddClaim("P8", g.NewLibrarianInfo("P5", "P3", BUTLER));
  g.AddClaim("P9", g.NewInvestigatorInfo("P11", "P12", SCARLET_WOMAN));
  g.AddClaim("P10", g.NewChefInfo(3));
  g.AddClaim("P11", g.NewEmpathInfo(0));
  g.AddClaim("P12", g.NewFortuneTellerAction("P11", "P13", true));
  g.AddClaim("P5", g.NewButlerAction("P4"));
  g.AddRoleAction("P6", g.NewSlayerAction("P1"));  // Drunk Slayer fails.
  g.AddNominationVoteExecution("P9", "P11");
  g.AddDeath("P11");
  g.AddNight(2);
  g.AddRoleAction("P4", g.NewPoisonerAction("P12"));
  g.AddRoleAction("P2", g.NewSpyInfo(spy_info));
  g.AddRoleAction("P14", g.NewMonkAction("P13"));
  g.AddRoleAction("P1", g.NewImpAction("P15"));
  g.AddRoleAction("P15", g.NewRavenkeeperAction("P4", POISONER));
  g.AddRoleAction("P13", g.NewUndertakerInfo(EMPATH));
  g.AddRoleAction("P12", g.NewFortuneTellerAction("P9", "P2", false));
  g.AddDay(2);
  g.AddNightDeath("P15");
  g.AddClaim("P14", g.NewMonkAction("P13"));
  g.AddClaim("P15", g.NewRavenkeeperAction("P4", POISONER));
  g.AddClaim("P13", g.NewUndertakerInfo(EMPATH));
  g.AddClaim("P12", g.NewFortuneTellerAction("P9", "P2", false));

  GameLog pb(g.ToProto());
  // Unfortunately, the testing::EqualsProto matcher is not OSS yet
  // (see https://github.com/google/googletest/issues/1761)
  EXPECT_EQ(pb.DebugString(), GameState::FromProto(pb).ToProto().DebugString());
}

TEST(VotingProcess, ProgressiveVotes) {
  GameState g(OBSERVER, TROUBLE_BREWING, MakePlayers(5));
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
  g.AddExecution("P4");
  g.AddDeath("P4");
  g.AddNight(2);
  g.AddDay(2);
  g.AddNightDeath("P1");
  g.AddNomination("P2", "P2");
  g.AddVote({"P1"}, "");
  EXPECT_TRUE(g.UsedDeadVote("P1"));
  EXPECT_FALSE(g.UsedDeadVote("P4"));
  g.AddNomination("P3", "P3");
  g.AddVote({"P4", "P3"}, "P3");
  EXPECT_TRUE(g.UsedDeadVote("P4"));
}

TEST(LifeAndDeath, DayAndNightDeaths) {
  GameState g(OBSERVER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddDay(1);
  g.AddNominationVoteExecution("P1", "P2");
  g.AddDeath("P2");
  EXPECT_TRUE(g.IsAlive("P2"));  // Was alive at start of day.
  g.AddNight(2);
  EXPECT_FALSE(g.IsAlive("P2"));  // Was dead at start of night.
  EXPECT_EQ(g.TimeOfDeath("P2"), Time::Day(1));
  g.AddDay(2);
  g.AddNightDeath("P1");
  EXPECT_EQ(g.TimeOfDeath("P1"), Time::Night(2));
  g.AddRoleAction("P3", g.NewSlayerAction("P4"));
  g.AddDeath("P4");
  g.AddNominationVoteExecution("P3", "P3");
  g.AddDeath("P3");
  g.AddNight(3);
  EXPECT_EQ(g.TimeOfDeath("P3"), Time::Day(2));
  EXPECT_EQ(g.TimeOfDeath("P4"), Time::Day(2));
  EXPECT_THAT(g.DeathsNames(Time::Day(1)), testing::ElementsAreArray({"P2"}));
  EXPECT_THAT(g.DeathsNames(Time::Night(2)), testing::ElementsAreArray({"P1"}));
  EXPECT_THAT(g.DeathsNames(Time::Day(2)),
              testing::ElementsAreArray({"P4", "P3"}));
}

TEST(GetRoleClaimsByNight, GetRoleClaimsByNightWorks) {
  // The game flow does not make any sense here, this is only to test the logic
  // of determining latest role claims per day.
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDay(1);
  g.AddClaim("P1", MAYOR);
  g.AddClaim("P2", WASHERWOMAN);
  g.AddNight(2);
  g.AddDay(2);
  g.AddClaim("P3", SOLDIER, Time::Night(2));
  g.AddClaim("P3", CHEF);  // Retract
  g.AddNight(3);
  g.AddDay(3);
  g.AddClaim("P4", SAINT, Time::Night(3));
  g.AddClaim("P5", FORTUNE_TELLER, Time::Night(2));
  g.AddClaim("P1", SLAYER, Time::Night(2));
  auto role_claims = g.GetRoleClaimsByNight();
  vector<vector<Role>> expected({
    {MAYOR, SLAYER, SLAYER},
    {WASHERWOMAN, WASHERWOMAN, WASHERWOMAN},
    {CHEF, CHEF, CHEF},
    {ROLE_UNSPECIFIED, ROLE_UNSPECIFIED, SAINT},
    {ROLE_UNSPECIFIED, FORTUNE_TELLER, FORTUNE_TELLER},
  });
  EXPECT_EQ(g.GetRoleClaimsByNight(), expected);
}

TEST(IsInfoExpected, IsInfoExpectedWorks) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", CHEF);
  g.AddDay(1);
  g.AddNominationVoteExecution("P2", "P3");
  g.AddDeath("P3");
  g.AddNight(2);
  g.AddDay(2);
  g.AddNightDeath("P2");
  g.AddNight(3);
  g.AddDay(3);
  g.AddNightDeath("P4");
  for (const string& player : {"P1", "P2", "P3", "P4", "P5"}) {
    for (Role role : {FORTUNE_TELLER, LIBRARIAN, CHEF, EMPATH}) {
      EXPECT_TRUE(g.IsInfoExpected(g.PlayerIndex(player), role, Time::Night(1)))
          << absl::StrFormat("Expected %s info for %s on night 1, got false",
                             Role_Name(role), player);
    }
    for (Role role : {MONK, RAVENKEEPER, UNDERTAKER, IMP, SLAYER}) {
      const bool info_expected = g.IsInfoExpected(
          g.PlayerIndex(player), role, Time::Night(1));
      EXPECT_FALSE(info_expected)
          << absl::StrFormat("Expected no %s info for %s on night 1, got true",
                            Role_Name(role), player);
    }
  }
  for (const string& player : {"P1", "P4", "P5"}) {
    const bool info_expected = g.IsInfoExpected(
        g.PlayerIndex(player), UNDERTAKER, Time::Night(2));
    EXPECT_TRUE(info_expected)
        << absl::StrFormat("Expected %s info for %s on night 2, got false",
                            Role_Name(UNDERTAKER), player);
  }
  for (const string& player : {"P1", "P3", "P4", "P5"}) {
    const bool info_expected = g.IsInfoExpected(
        g.PlayerIndex(player), RAVENKEEPER, Time::Night(2));
    EXPECT_FALSE(info_expected)
        << absl::StrFormat("Expected no %s info for %s on night 1, got true",
                          Role_Name(RAVENKEEPER), player);
  }
  const bool info_expected = g.IsInfoExpected(
      g.PlayerIndex("P2"), RAVENKEEPER, Time::Night(2));
  EXPECT_TRUE(info_expected);
  EXPECT_TRUE(g.IsInfoExpected(g.PlayerIndex("P4"), MONK, Time::Night(3)));
  for (Role role : {FORTUNE_TELLER, CHEF, EMPATH, UNDERTAKER, SLAYER}) {
    EXPECT_FALSE(g.IsInfoExpected(g.PlayerIndex("P4"), role, Time::Night(3)))
        << absl::StrFormat("Expected no %s info for P4 on night 3, got true",
                          Role_Name(role));
  }
}

TEST(IsFullyClaimed, IsFullyClaimedWorks) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDay(1);
  g.AddClaim("P2", WASHERWOMAN);
  g.AddNight(2);
  g.AddDay(2);
  g.AddClaim("P3", SOLDIER, Time::Night(2));
  g.AddClaim("P3", CHEF);  // Retract
  g.AddNight(3);
  g.AddDay(3);
  g.AddNightDeath("P4");
  g.AddClaim("P4", SAINT, Time::Night(3));
  g.AddClaim("P5", FORTUNE_TELLER);
  EXPECT_EQ(g.IsFullyClaimed().ToString(), "INVALID_ARGUMENT: Missing claims: "
      "P1 is missing a role claim, "
      "P2 is missing a WASHERWOMAN role action claim for night 1, "
      "P3 is missing a CHEF role action claim for night 1, "
      "P4 is missing a role claim for nights 1-2, "
      "P5 is missing a FORTUNE_TELLER role action claim for night 1, "
      "P5 is missing a FORTUNE_TELLER role action claim for night 2, "
      "P5 is missing a FORTUNE_TELLER role action claim for night 3");
  g.AddClaim("P1", MAYOR);
  g.AddClaim("P2", g.NewWasherwomanInfo("P1", "P3", CHEF));
  g.AddClaim("P3", g.NewChefInfo(1));
  g.AddClaim("P4", RAVENKEEPER);
  g.AddClaim("P5", g.NewFortuneTellerAction("P1", "P2", true), Time::Night(1));
  g.AddClaim("P5", g.NewFortuneTellerAction("P3", "P4", true), Time::Night(2));
  g.AddClaim("P5", g.NewFortuneTellerAction("P2", "P5", false));
  EXPECT_EQ(g.IsFullyClaimed().ToString(), "INVALID_ARGUMENT: Missing claims: "
      "P4 is missing a RAVENKEEPER role action claim for night 3");
  g.AddClaim("P4", g.NewRavenkeeperAction("P4", POISONER));
  EXPECT_TRUE(g.IsFullyClaimed().ok());
  g.AddClaim("P4", WASHERWOMAN);  // Double-claim.
  EXPECT_EQ(g.IsFullyClaimed().ToString(), "INVALID_ARGUMENT: Missing claims: "
      "P4 is missing a WASHERWOMAN role action claim for night 1");
  g.AddClaim("P4", g.NewWasherwomanInfo("P1", "P3", CHEF));
  EXPECT_TRUE(g.IsFullyClaimed().ok());
}

TEST(RoleChanges, ScarletWomanProcExecution) {
  GameState g(STORYTELLER, TROUBLE_BREWING, MakePlayers(5));
  g.SetRoles({IMP, SCARLET_WOMAN, MAYOR, MONK, RAVENKEEPER});
  g.AddNight(1);
  g.AddAllShownTokens({IMP, SCARLET_WOMAN, MAYOR, MONK, RAVENKEEPER});
  g.AddDay(1);
  g.AddNominationVoteExecution("P3", "P1");
  g.AddDeath("P1");
  g.AddNight(2);
  g.AddShownToken("P2", IMP);
  g.AddRoleAction("P2", g.NewImpAction("P4"));
  g.AddDay(2);
  EXPECT_EQ(g.GetRole("P2"), IMP);
}

TEST(RoleChanges, ScarletWomanProcSlayer) {
  GameState g(STORYTELLER, TROUBLE_BREWING, MakePlayers(5));
  g.SetRoles({IMP, SCARLET_WOMAN, SLAYER, MONK, RAVENKEEPER});
  g.AddNight(1);
  g.AddAllShownTokens({IMP, SCARLET_WOMAN, SLAYER, MONK, RAVENKEEPER});
  g.AddDay(1);
  g.AddRoleAction("P3", g.NewSlayerAction("P1"));
  g.AddDeath("P1");
  g.AddNight(2);
  g.AddShownToken("P2", IMP);
  g.AddRoleAction("P2", g.NewImpAction("P3"));
  g.AddDay(2);
  EXPECT_EQ(g.GetRole("P2"), IMP);
}

TEST(RoleChanges, ImpStarpass) {
  GameState g(STORYTELLER, TROUBLE_BREWING, MakePlayers(5));
  g.SetRoles({IMP, SCARLET_WOMAN, SLAYER, MONK, RAVENKEEPER});
  g.AddNight(1);
  g.AddAllShownTokens({IMP, SCARLET_WOMAN, SLAYER, MONK, RAVENKEEPER});
  g.AddDay(1);
  g.AddNight(2);
  g.AddRoleAction("P1", g.NewImpAction("P1"));
  g.AddShownToken("P2", IMP);
  g.AddDay(2);
  g.AddNightDeath("P1");
  EXPECT_EQ(g.GetRole("P2"), IMP);
}

TEST(IsRolePossible, MinionPerspectiveStarpass) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(10));
  g.AddNight(1);
  g.AddShownToken("P1", BARON);
  g.AddMinionInfo("P1", "P2", {"P3"});
  g.AddDay(1);
  g.AddClaim("P4", RECLUSE);
  EXPECT_TRUE(g.IsRolePossible("P1", BARON, Time::Day(1)));
  EXPECT_FALSE(g.IsRolePossible("P1", POISONER, Time::Day(1)));
  EXPECT_TRUE(g.IsRolePossible("P3", POISONER, Time::Day(1)));
  EXPECT_TRUE(g.IsRolePossible("P2", IMP, Time::Day(1)));
  for (auto& p : {"P1", "P3", "P4", "P5", "P6", "P7", "P8", "P9", "P10"}) {
    EXPECT_FALSE(g.IsRolePossible(p, IMP, Time::Day(1)));
  }
  for (auto& p : {"P1", "P2", "P4", "P5", "P6", "P7", "P8", "P9", "P10"}) {
    EXPECT_FALSE(g.IsRolePossible(p, POISONER, Time::Day(1)));
  }
  for (Role role : GoodRoles(TROUBLE_BREWING)) {
    for (const string& p : {"P1", "P2", "P3"}) {
      EXPECT_FALSE(g.IsRolePossible(p, role, Time::Day(1)))
          << absl::StrFormat("Known evil %s cannot be a good role %s",
                             p, Role_Name(role));
    }
  }
  g.AddNight(2);
  g.AddDay(2);
  g.AddNightDeath("P2");
  g.AddClaim("P4", IMP);  // Claiming Recluse starpass.
  for (auto& p : {"P2", "P3", "P4"}) {
    EXPECT_TRUE(g.IsRolePossible(p, IMP, Time::Day(2)));
  }
  for (auto& p : {"P1", "P5", "P6", "P7", "P8", "P9", "P10"}) {
    EXPECT_FALSE(g.IsRolePossible(p, IMP, Time::Day(2)));
  }
}

TEST(IsRolePossible, MinionPerspectiveCatchStarpass) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(10));
  g.AddNight(1);
  g.AddShownToken("P1", SCARLET_WOMAN);
  g.AddMinionInfo("P1", "P2", {"P3"});
  g.AddDay(1);
  g.AddClaim("P4", RECLUSE);
  EXPECT_TRUE(g.IsRolePossible("P1", SCARLET_WOMAN, Time::Day(1)));
  EXPECT_FALSE(g.IsRolePossible("P1", POISONER, Time::Day(1)));
  EXPECT_TRUE(g.IsRolePossible("P3", POISONER, Time::Day(1)));
  EXPECT_TRUE(g.IsRolePossible("P2", IMP, Time::Day(1)));
  for (auto& p : {"P1", "P3", "P4", "P5", "P6", "P7", "P8", "P9", "P10"}) {
    EXPECT_FALSE(g.IsRolePossible(p, IMP, Time::Day(1))) << p;
  }
  for (auto& p : {"P1", "P2", "P4", "P5", "P6", "P7", "P8", "P9", "P10"}) {
    EXPECT_FALSE(g.IsRolePossible(p, POISONER, Time::Day(1))) << p;
  }
  g.AddNight(2);
  g.AddShownToken("P1", IMP);
  g.AddDay(2);
  g.AddNightDeath("P2");
  EXPECT_TRUE(g.IsRolePossible("P1", IMP, Time::Day(2)));
  EXPECT_TRUE(g.IsRolePossible("P2", IMP, Time::Night(1)));
  EXPECT_TRUE(g.IsRolePossible("P2", IMP, Time::Day(2)));
  for (auto& p : {"P3", "P4", "P5", "P6", "P7", "P8", "P9", "P10"}) {
    EXPECT_FALSE(g.IsRolePossible(p, IMP, Time::Day(2))) << p;
  }
}

TEST(IsRolePossible, MinionPerspectiveCatchStarpassNoInfo) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(5));
  g.AddNight(1);
  g.AddShownToken("P1", SCARLET_WOMAN);
  g.AddDay(1);
  g.AddAllClaims({MAYOR, RAVENKEEPER, VIRGIN, UNDERTAKER, SOLDIER}, "P1");
  g.AddNight(2);
  g.AddShownToken("P1", IMP);
  g.AddDay(2);
  g.AddNightDeath("P4");
  EXPECT_TRUE(g.IsRolePossible("P4", IMP, Time::Night(2)));
}

TEST(IsRolePossible, DemonPerspective) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(10));
  g.AddNight(1);
  g.AddShownToken("P1", IMP);
  g.AddDemonInfo("P1", {"P2", "P3"}, {EMPATH, SLAYER, MAYOR});
  g.AddDay(1);
  g.AddClaim("P4", RECLUSE);
  EXPECT_TRUE(g.IsRolePossible("P2", SCARLET_WOMAN, Time::Day(1)));
  EXPECT_TRUE(g.IsRolePossible("P3", POISONER, Time::Day(1)));
  EXPECT_TRUE(g.IsRolePossible("P1", IMP, Time::Day(1)));
  for (auto& p : {"P2", "P3", "P4", "P5", "P6", "P7", "P8", "P9", "P10"}) {
    EXPECT_FALSE(g.IsRolePossible(p, IMP, Time::Day(1)));
  }
  for (auto& p : {"P1", "P4", "P5", "P6", "P7", "P8", "P9", "P10"}) {
    EXPECT_FALSE(g.IsRolePossible(p, POISONER, Time::Day(1)));
  }
  for (Role bluff : {EMPATH, SLAYER, MAYOR}) {
    for (const string& p : g.PlayerNames()) {
      EXPECT_FALSE(g.IsRolePossible(p, bluff, Time::Day(1)))
          << absl::StrFormat("%s cannot be a demon bluff %s",
                             p, Role_Name(bluff));
    }
  }
  for (Role role : GoodRoles(TROUBLE_BREWING)) {
    for (const string& p : {"P1", "P2", "P3"}) {
      EXPECT_FALSE(g.IsRolePossible(p, role, Time::Day(1)))
          << absl::StrFormat("Known evil %s cannot be a good role %s",
                             p, Role_Name(role));
    }
  }
  g.AddNight(2);
  g.AddDay(2);
  g.AddNightDeath("P1");
  g.AddClaim("P4", IMP);  // Claiming Recluse starpass.
  for (auto& p : {"P1", "P2", "P3", "P4"}) {
    EXPECT_TRUE(g.IsRolePossible(p, IMP, Time::Day(2)));
  }
  for (auto& p : {"P5", "P6", "P7", "P8", "P9", "P10"}) {
    EXPECT_FALSE(g.IsRolePossible(p, IMP, Time::Day(2)));
  }
}

TEST(IsRolePossible, MinionStarpassChain) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(13));
  g.AddNight(1);
  g.AddShownToken("P4", BARON);
  g.AddMinionInfo("P4", "P2", {"P1", "P13"});
  g.AddDay(1);
  g.AddAllClaims(
      {WASHERWOMAN, MAYOR, BUTLER, RECLUSE, SOLDIER, SLAYER, UNDERTAKER,
       SAINT, VIRGIN, RAVENKEEPER, CHEF, MONK, EMPATH}, "P1");
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
  EXPECT_TRUE(g.IsRolePossible("P2", IMP, Time::Night(2)));
  EXPECT_TRUE(g.IsRolePossible("P13", IMP, Time::Day(2)));
  EXPECT_TRUE(g.IsRolePossible("P13", IMP, Time::Night(3)));
  EXPECT_TRUE(g.IsRolePossible("P4", IMP, Time::Day(3)));
  EXPECT_TRUE(g.IsRolePossible("P4", IMP, Time::Night(4)));
  EXPECT_TRUE(g.IsRolePossible("P1", IMP, Time::Day(4)));
}

TEST(IsRolePossible, ScarletWomanProcExecuteImp) {
  GameState g(PLAYER, TROUBLE_BREWING, MakePlayers(7));
  g.AddNight(1);
  g.AddShownToken("P5", SCARLET_WOMAN);
  g.AddMinionInfo("P5", "P1", {});  // P5 SW, P1 Imp
  g.AddDay(1);
  g.AddAllClaims(
      {SOLDIER, MAYOR, RAVENKEEPER, VIRGIN, UNDERTAKER, SLAYER, MONK}, "P1");
  g.AddNominationVoteExecution("P2", "P1");
  g.AddDeath("P1");
  g.AddNight(2);
  g.AddShownToken("P5", IMP);
  g.AddRoleAction("P5", g.NewImpAction("P5"));
  g.AddDay(2);
  g.AddNightDeath("P5");
  g.AddClaim("P7", g.NewMonkAction("P6"));
  EXPECT_TRUE(g.IsRolePossible("P1", IMP, Time::Day(2)));
  EXPECT_FALSE(g.IsRolePossible("P5", IMP, Time::Day(1)));
  EXPECT_TRUE(g.IsRolePossible("P5", IMP, Time::Night(2)));
  EXPECT_TRUE(g.IsRolePossible("P5", IMP, Time::Day(2)));
}
}  // namespace
}  // namespace botc

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
