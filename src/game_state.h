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

#ifndef SRC_GAME_STATE_H_
#define SRC_GAME_STATE_H_

#include <algorithm>
#include <string>
#include <iostream>
#include <memory>
#include <vector>
#include <unordered_map>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "ortools/base/logging.h"
#include "src/game_log.pb.h"
#include "src/util.h"

namespace botc {

using std::string;
using std::vector;
using std::map;
using std::pair;
using std::ostream;
using std::unordered_map;
using std::shared_ptr;
using std::set;
using Audience = botc::Claim::Audience;
using SoftRole = botc::Claim::SoftRole;

const int kNoPlayer =  - 1;  // Used in place of player index.

const int kNumTownsfolk[] = {3, 3, 5, 5, 5, 7, 7, 7, 9, 9, 9};
const int kNumOutsiders[] = {0, 1, 0, 1, 2, 0, 1, 2, 0, 1, 2};
const int kNumMinions[] = {1, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3};

// Describes a supported role.
struct RoleMetadata {
  RoleType role_type = ROLE_TYPE_UNSPECIFIED;
  // The order in which the role awakes at night (0 for not waking).
  // The numbers here are copied from Bra1n's tool at:
  // https://github.com/bra1n/townsquare/blob/main/src/roles.json
  int first_night = 0, other_night = 0;
  bool day_action = false;  // Whether the role has an optional daytime action.
  bool optional_trigger = false;  // May have an action or not, custom considion
  bool public_action = false;  // E.g. Slayer, Gossip, Juggler.
};

// TODO(olaola): make this into a constexpr map!
const RoleMetadata kRoleMetadata[] = {
  {},  // ROLE_UNSPECIFIED

  // Trouble Brewing roles:
  // WASHERWOMAN
  {.role_type = TOWNSFOLK, .first_night = 32},
  // LIBRARIAN
  {.role_type = TOWNSFOLK, .first_night = 33},
  // INVESTIGATOR
  {.role_type = TOWNSFOLK, .first_night = 34},
  // CHEF
  {.role_type = TOWNSFOLK, .first_night = 35},
  // EMPATH
  {.role_type = TOWNSFOLK, .first_night = 36, .other_night = 53},
  // FORTUNE_TELLER
  {.role_type = TOWNSFOLK, .first_night = 37, .other_night = 54},
  // UNDERTAKER
  {.role_type = TOWNSFOLK, .other_night = 56, .optional_trigger = true},
  // MONK
  {.role_type = TOWNSFOLK, .other_night = 13},
  // RAVENKEEPER
  {.role_type = TOWNSFOLK, .other_night = 42, .optional_trigger = true},
  // VIRGIN
  {.role_type = TOWNSFOLK},
  // SLAYER
  {.role_type = TOWNSFOLK, .day_action = true, .optional_trigger = true,
   .public_action = true},
  // SOLDIER
  {.role_type = TOWNSFOLK},
  // MAYOR
  {.role_type = TOWNSFOLK},
  // BUTLER
  {.role_type = OUTSIDER, .first_night = 38, .other_night = 55},
  // DRUNK
  {.role_type = OUTSIDER},
  // RECLUSE
  {.role_type = OUTSIDER},
  // SAINT
  {.role_type = OUTSIDER},
  // POISONER
  {.role_type = MINION, .first_night = 17, .other_night = 8},
  // SPY
  {.role_type = MINION, .first_night = 48, .other_night = 68},
  // SCARLET_WOMAN
  {.role_type = MINION, .other_night = 20, .optional_trigger = true},
  // BARON
  {.role_type = MINION},
  // IMP
  {.role_type = DEMON, .other_night = 24},
};

const Role kTroubleBrewingRoles[] = {
    WASHERWOMAN, LIBRARIAN, INVESTIGATOR, CHEF, EMPATH, FORTUNE_TELLER,
    UNDERTAKER, MONK, RAVENKEEPER, VIRGIN, SLAYER, SOLDIER, MAYOR,
    BUTLER, DRUNK, RECLUSE, SAINT, POISONER, SPY, SCARLET_WOMAN, BARON,
    IMP
};

bool IsRoleInRoles(Role role, absl::Span<const Role> roles);
bool IsDemonRole(Role role);
bool IsMinionRole(Role role);
bool IsTownsfolkRole(Role role);
bool IsOutsiderRole(Role role);
bool IsGoodRole(Role role);
bool IsEvilRole(Role role);
bool IsDayActionRole(Role role);
bool IsPublicActionRole(Role role);
bool IsFirstNightOnlyRole(Role role);
const absl::Span<const Role> AllRoles(Script s);

typedef bool (*RoleFilter)(Role);

const vector<Role> FilterRoles(Script s, RoleFilter f);
const vector<Role> GoodRoles(Script s);
const vector<Role> EvilRoles(Script s);
const vector<Role> TownsfolkRoles(Script s);
const vector<Role> OutsiderRoles(Script s);
const vector<Role> MinionRoles(Script s);
const vector<Role> DemonRoles(Script s);

// In-game current time. The game starts with Night 1, after which Day x follows
// Night x, and is followed by Night x + 1, etc.
struct Time {
  bool is_day = true;
  int count = 0;
  bool Initialized() const { return count > 0; }
  operator string() const {
    return absl::StrFormat("%s_%d", is_day ? "day" : "night", count);
  }
  Time operator+(int n) const {
    if (n < 0) {
      return *this - (-n);
    }
    Time t = *this;
    t.count += n / 2;
    if (n % 2 == 1) {
      if (t.is_day) {
        t.count++;
      }
      t.is_day = !t.is_day;
    }
    return t;
  }
  Time operator-(int n) const {
    if (n < 0) {
      return *this + (-n);
    }
    Time t = *this;
    t.count -= n / 2;
    if (n % 2 == 1) {
      if (!t.is_day) {
        t.count--;
      }
      t.is_day = !t.is_day;
    }
    if (t.count < 0) {
      t.count = 0;
      t.is_day = true;
    }
    return t;
  }
  Time& operator+=(int n) { return (*this = (*this + n)); }
  Time& operator-=(int n) { return (*this = (*this - n)); }
  Time& operator++() { return (*this += 1); }
  Time& operator--() { return (*this -= 1); }
  static Time Day(int day) { return {.is_day = true, .count = day}; }
  static Time Night(int night) { return {.is_day = false, .count = night}; }
};

ostream& operator<<(ostream& os, const Time& t);
bool operator<(const Time& l, const Time& r);
bool operator>(const Time& l, const Time& r);
bool operator<=(const Time& l, const Time& r);
bool operator>=(const Time& l, const Time& r);
bool operator==(const Time& l, const Time& r);

const Script kSupportedScripts[] = { TROUBLE_BREWING };
bool IsSupportedScript(Script s);

namespace internal {
// Here and below, the structs are translations of the corresponding proto
// messages, with player names replaced by indices.
struct MinionInfo {
  int player = kNoPlayer, demon = kNoPlayer;
  vector<int> minions;
};

struct DemonInfo {
  int player = kNoPlayer;
  vector<int> minions;
  vector<Role> bluffs;
};

struct Nomination {
  Time time;
  int nominator = kNoPlayer, nominee = kNoPlayer;
  vector<int> votes;
  bool virgin_proc = false;
};

struct RoleAction {
  // Information from the surrounding ST interaction.
  Time time;
  int player = kNoPlayer;

  // Same info as in the RoleAction proto (but with player indices).
  Role acting = ROLE_UNSPECIFIED;
  // Some of the fields below will be populated based on acting role.
  vector<int> players;  // Picks or pings (e.g. Washerwoman, Ravenkeeper).
  vector<Role> roles;  // Role picks or learned roles (e.g. Undertaker, Sage).
  int number = 0;  // e.g. Chef, Empath, Clockmaker, etc.
  bool yes;  // e.g. Fortune Teller, Seamstress, Artist, etc.
  Team team;  // e.g. Goon team changes, or weak role action claims.
  GrimoireInfo grimoire_info;  // Spy/Widow are special.

  bool IsWellDefined() const;
};

struct Audience {
  vector<int> players;
  bool townsquare = true;
  bool IsEmpty() const { return !townsquare && players.empty(); }
  static Audience Nobody() { return {.townsquare = false}; }
  static Audience Townsquare() { return {.townsquare = true}; }
};

// Same info as in the Claim proto, translating player names into indices.
struct Claim {
  Time claim_time;  // The time the claim was made (always during the day).
  int player = kNoPlayer;  // The player making the claim.
  internal::Audience audience = internal::Audience::Townsquare();
  Time time;  // The time the claim pertains to. Only required for claims not
              // containing a RoleAction field (otherwise time is part of RA).
  botc::Claim::DetailsCase claim_case;
  Role role = ROLE_UNSPECIFIED;
  SoftRole soft_role;
  RoleAction role_action;  // Used for both action and effect claims.
  shared_ptr<Claim> claim;  // Used for both claim propagations and retractions.
};

struct Whisper {
  vector<int> players;
  int initiator = kNoPlayer;
};

SoftRole NewSoftRole(absl::Span<const Role> roles);
SoftRole NewSoftRoleNot(absl::Span<const Role> roles);
SoftRole NewSoftRole(RoleType rt);
SoftRole NewSoftRoleNot(RoleType rt);
}  // namespace internal

// This contains an instance of a BOTC game on a particular time.
class GameState {
 public:
  GameState(
      Perspective perspective, Script script, absl::Span<const string> players);
  GameState& SetRedHerring(const string& red_herring);
  GameState& SetRoles(const unordered_map<string, Role>& roles);
  GameState& SetRoles(absl::Span<const Role> roles);  // In order of players.
  static GameState ReadFromFile(const path& filename) {
    GameLog log;
    ReadProtoFromFile(filename, &log);
    return FromProto(log);
  }

  void WriteToFile(const path& filename) const {
    WriteProtoToFile(ToProto(), filename);
  }

  static GameState FromProto(const GameLog& log);
  const GameLog& ToProto() const { return log_; }

  // Game events.
  GameState& AddEvent(const Event& event);
  GameState& AddDay(int count);
  GameState& AddNight(int count);
  GameState& AddStorytellerInteraction(
      const StorytellerInteraction& interaction);
  GameState& AddNomination(const Nomination& nomination);
  GameState& AddNomination(const string& nominator, const string& nominee);
  GameState& AddVote(const Vote& vote);
  GameState& AddVote(int num_votes, const string& on_the_block) {
    return AddVote({}, num_votes, on_the_block);
  }
  GameState& AddVote(absl::Span<const string> votes,
                     const string& on_the_block) {
    return AddVote(votes, votes.size(), on_the_block);
  }
  GameState& AddVote(absl::Span<const string> votes,
                     int num_votes,
                     const string& on_the_block);
  GameState& AddExecution(const string& name);
  // Syntactic sugar for adding nomination, vote and execution in short form,
  // for cases where Virgin doesn't proc (in Trouble Brewing actual votes rarely
  // matter for the solve):
  GameState& AddNominationVoteExecution(const string& nominator,
                                        const string& executee);
  GameState& AddDeath(const string& name);
  GameState& AddNightDeath(const string& name);
  GameState& AddClaim(const Claim& claim) {
    return AddClaim(ClaimFromProto(claim));
  }
  GameState& AddClaim(const internal::Claim& claim);

  internal::Claim NewClaimRole(const string& player, Role role, const Time& t) {
    return {.player = PlayerIndex(player), .time = t,
            .claim_case = Claim::kRole, .role = role};
  }
  internal::Claim NewClaimSoftRole(const string& player,
                                   const SoftRole& sr,
                                   const Time& t) {
    return {.player = PlayerIndex(player), .time = t,
            .claim_case = Claim::kSoftRole, .soft_role = sr};
  }
  internal::Claim NewClaimRoleAction(const string& player,
                                     const internal::RoleAction& ra,
                                     const Time& t) {
    return {.player = PlayerIndex(player), .time = t,
            .claim_case = Claim::kRoleAction, .role_action = ra};
  }
  internal::Claim NewClaimRoleEffect(const string& player,
                                     const internal::RoleAction& ra,
                                     const Time& t) {
    return {.player = PlayerIndex(player), .time = t,
            .claim_case = Claim::kRoleEffect, .role_action = ra};
  }
  internal::Claim NewClaimPropagation(const string& player,
                                      const internal::Claim& claim,
                                      const Time& t) {
    internal::Claim res({.player = PlayerIndex(player), .time = t,
                         .claim_case = Claim::kClaim});
    res.claim.reset(new internal::Claim(claim));
    return res;
  }
  internal::Claim NewClaimRetraction(const string& player,
                                     const internal::Claim& claim,
                                     const Time& t) {
    internal::Claim res({.player = PlayerIndex(player), .time = t,
                         .claim_case = Claim::kRetraction});
    res.claim.reset(new internal::Claim(claim));
    return res;
  }
  GameState& AddClaimRole(const string& player, Role role, const Time& t) {
    return AddClaim(NewClaimRole(player, role, t));
  }
  GameState& AddClaimRole(const string& player, Role role) {
    return AddClaimRole(player, role, Time());
  }
  GameState& AddClaimSoftRole(
      const string& player, const SoftRole& sr, const Time& t);
  GameState& AddClaimSoftRole(const string& player, const SoftRole& sr) {
    return AddClaimSoftRole(player, sr, Time());
  }
  GameState& AddClaimRoleAction(const string& player,
                                const internal::RoleAction& ra,
                                const Time& t) {
    return AddClaim(NewClaimRoleAction(player, ra, t));
  }
  GameState& AddClaimRoleAction(const string& player,
                                const internal::RoleAction& ra) {
    return AddClaimRoleAction(player, ra, Time());
  }
  GameState& AddClaimRoleEffect(const string& player,
                                const internal::RoleAction& re,
                                const Time& t) {
    return AddClaim(NewClaimRoleEffect(player, re, t));
  }
  GameState& AddClaimRoleEffect(const string& player,
                                const internal::RoleAction& re) {
    return AddClaimRoleEffect(player, re, Time());
  }
  // Syntactic sugar for a few role claims at once.
  GameState& AddRoleClaims(absl::Span<const Role> roles,
                           const string& starting_player);
  GameState& AddVictory(Team victory);

  // Syntactic sugar for the Storyteller perspective.
  GameState& AddAllShownTokens(absl::Span<const Role> roles);
  GameState& AddShownToken(const string& player, Role role);
  GameState& AddMinionInfo(const string& player,
                           const MinionInfo& minion_info);
  GameState& AddMinionInfo(const string& player, const string& demon,
                           absl::Span<const string> minions);
  GameState& AddDemonInfo(const string& player, const DemonInfo& demon_info);
  GameState& AddDemonInfo(const string& player,
                          absl::Span<const string> minions,
                          absl::Span<const Role> bluffs);
  GameState& AddRoleAction(const string& player, const RoleAction& ra);
  GameState& AddRoleAction(const string& player,
                           const internal::RoleAction& ra);
  GameState& AddWhisper(absl::Span<const string> players,
                        const string& initiator);
  GameState& AddWhisper(absl::Span<const string> players) {
    return AddWhisper(players, "");
  }

  // Syntactic sugar for creating role actions or info.
  internal::RoleAction NewWasherwomanInfo(const string& ping1,
                                          const string& ping2, Role role) {
    return {.acting = WASHERWOMAN,
            .players = {PlayerIndex(ping1), PlayerIndex(ping2)},
            .roles = {role}};
  }
  internal::RoleAction NewLibrarianInfo(const string& ping1,
                                        const string& ping2, Role role) {
    return {.acting = LIBRARIAN,
            .players = {PlayerIndex(ping1), PlayerIndex(ping2)},
            .roles = {role}};
  }
  internal::RoleAction NewLibrarianInfoNoOutsiders() {
    return {.acting = LIBRARIAN};
  }
  internal::RoleAction NewInvestigatorInfo(const string& ping1,
                                           const string& ping2, Role role) {
    return {.acting = INVESTIGATOR,
            .players = {PlayerIndex(ping1), PlayerIndex(ping2)},
            .roles = {role}};
  }
  internal::RoleAction NewChefInfo(int number) {
    return {.acting = CHEF, .number = number};
  }
  internal::RoleAction NewEmpathInfo(int number) {
    return {.acting = EMPATH, .number = number};
  }
  internal::RoleAction NewFortuneTellerAction(const string& pick1,
                                              const string& pick2, bool yes) {
    return {.acting = FORTUNE_TELLER,
            .players = {PlayerIndex(pick1), PlayerIndex(pick2)},
            .yes = yes};
  }
  internal::RoleAction NewMonkAction(const string& pick) {
    return {.acting = MONK, .players = {PlayerIndex(pick)}};
  }
  internal::RoleAction NewButlerAction(const string& pick) {
    return {.acting = BUTLER, .players = {PlayerIndex(pick)}};
  }
  internal::RoleAction NewRavenkeeperAction(const string& pick, Role role) {
    return {.acting = RAVENKEEPER,
            .players = {PlayerIndex(pick)}, .roles = {role}};
  }
  internal::RoleAction NewUndertakerInfo(Role role) {
    return {.acting = UNDERTAKER, .roles = {role}};
  }
  internal::RoleAction NewSlayerAction(const string& pick) {
    return {.acting = SLAYER, .players = {PlayerIndex(pick)}};
  }
  internal::RoleAction NewPoisonerAction(const string& pick) {
    return {.acting = POISONER, .players = {PlayerIndex(pick)}};
  }
  internal::RoleAction NewImpAction(const string& pick) {
    return {.acting = IMP, .players = {PlayerIndex(pick)}};
  }
  internal::RoleAction NewSpyInfo(const GrimoireInfo& spy_info) {
    return {.acting = SPY, .grimoire_info = spy_info};
  }
  GrimoireInfo GrimoireInfoFromRoles(absl::Span<const Role> roles,
                                     Role drunk_shown_token) const;
  GrimoireInfo GrimoireInfoFromRoles(absl::Span<const Role> roles) const {
    return GrimoireInfoFromRoles(roles, ROLE_UNSPECIFIED);
  }
  // Public state accessors.
  const Time& CurrentTime() const { return cur_time_; }
  int NumPlayers() const { return num_players_; }
  int NumMinions() const { return num_minions_; }
  int NumOutsiders() const { return num_outsiders_; }
  int NumTownsfolk() const { return kNumTownsfolk[num_players_ - 5]; }
  // Returns the # of players alive at the start of day/night.
  int NumAlive() const { return NumAlive(cur_time_); }
  int NumAlive(const Time& time) const {
    return (time.is_day ? num_alive_day_ :  num_alive_night_)[time.count - 1];
  }
  // Returns true if the player was alive at the *start* of day/night.
  bool IsAlive(const string& player) const {
    return IsAlive(PlayerIndex(player));
  }
  bool IsAlive(int i) const { return IsAlive(i, cur_time_); }
  bool IsAlive(int i, const Time& time) const {
    CHECK_LE(time, cur_time_);
    return (time.is_day ? is_alive_day_ :  is_alive_night_)[time.count - 1][i];
  }
  // Returns invalid time if player is still alive.
  Time TimeOfDeath(const string& player) const {
    return TimeOfDeath(PlayerIndex(player));
  }
  Time TimeOfDeath(int player) const;
  bool UsedDeadVote(const string& player) const {
    return dead_vote_used_[PlayerIndex(player)];
  }

  string OnTheBlockName() const { return PlayerName(OnTheBlock()); }
  int OnTheBlock() const { return on_the_block_; }

  string ExecutionName(const Time& time) const {
    return PlayerName(Execution(time));
  }
  string ExecutionName() const { return ExecutionName(cur_time_); }
  int Execution(const Time& time) const {
    int day = time.is_day ? time.count : time.count - 1;
    if (executions_.size() < day) {
      return kNoPlayer;
    }
    return executions_[day - 1];
  }
  int Execution() const { return Execution(cur_time_); }

  int ExecutionDeath(const Time& time) const {
    int day = time.is_day ? time.count : time.count - 1;
    if (execution_deaths_.size() < day) {
      return kNoPlayer;
    }
    return execution_deaths_[day - 1];
  }
  int ExecutionDeath() const { return ExecutionDeath(cur_time_); }
  string ExecutionDeathName(const Time& time) const {
    return PlayerName(ExecutionDeath(time));
  }
  string ExecutionDeathName() const {
    return PlayerName(ExecutionDeath());
  }

  const vector<internal::Nomination>& Nominations() const {
    return nominations_;
  }
  vector<int> Deaths() const { return Deaths(cur_time_); }
  vector<int> Deaths(const Time& time) const;
  vector<string> DeathsNames(const Time& time) const;
  vector<string> DeathsNames() const { return DeathsNames(cur_time_); }

  bool IsGameOver() const { return victory_ != TEAM_UNSPECIFIED; }
  Team WinningTeam() const { return victory_; }

  string PlayerName(int i) const { return i == kNoPlayer ? "" : players_[i]; }
  const vector<string>& PlayerNames() { return players_; }
  int PlayerIndex(const string& name) const { return PlayerIndex(name, false); }
  int PlayerIndex(const string& name, bool allow_empty) const {
    if (name.empty() && allow_empty) {
      return kNoPlayer;
    }
    const auto& it = player_index_.find(name);
    CHECK(it != player_index_.end()) << "Invalid player name: " << name;
    return it->second;
  }

  vector<int> AliveNeighbors(int player, const Time& time) const;
  vector<int> AliveNeighbors(int player) const {
    return AliveNeighbors(player, cur_time_);
  }
  Script GetScript() const { return script_; }
  Perspective GetPerspective() const { return perspective_; }
  int PerspectivePlayer() const { return perspective_player_; }
  string PerspectivePlayerName() const {
    return PlayerName(perspective_player_);
  }
  const internal::MinionInfo GetMinionInfo() const { return minion_info_; }
  const internal::DemonInfo GetDemonInfo() const { return demon_info_; }
  int RedHerring() const { return st_red_herring_; }
  string RedHerringName() const { return PlayerName(RedHerring()); }
  Role GetRole(const string& player) const {
    return GetRole(player, cur_time_);
  }
  Role GetRole(const string& player, const Time& time) const {
    return GetRole(PlayerIndex(player), time);
  }
  Role GetRole(int player, const Time& time) const {
    CHECK_EQ(perspective_, STORYTELLER)
        << "Roles are only available in the Storyteller perspective";
    const auto& roles = time.is_day ? st_day_roles_ : st_night_roles_;
    return roles[time.count - 1][player];
  }
  Role ShownToken(const string& player) const {
    return ShownToken(PlayerIndex(player));
  }
  Role ShownToken(int player) const {
    return ShownToken(player, cur_time_);
  }
  Role ShownToken(int player, const Time& time) const {
    switch (perspective_) {
      case OBSERVER:
        return ROLE_UNSPECIFIED;
      case PLAYER:
        return (player == perspective_player_ ?
                perspective_player_shown_token_[time.count - 1] :
                ROLE_UNSPECIFIED);
      case STORYTELLER:
        return st_shown_tokens_[time.count - 1][player];
      default:
        CHECK(false) << "Invalid game state: unset perspective";
    }
    return ROLE_UNSPECIFIED;
  }

  bool IsRolePossible(int player, Role role, const Time& time) const;
  bool IsRolePossible(const string& player, Role role, const Time& time) const {
    return IsRolePossible(PlayerIndex(player), role, time);
  }
  vector<const internal::RoleAction*> GetRoleActions(int player) const;
  vector<const internal::RoleAction*> GetRoleActions(Role role) const;

  vector<Role> GetRoleClaimsByNight(int player) const;
  vector<vector<Role>> GetRoleClaimsByNight() const;
  // Only supports night-time actions.
  // For every role, returns a x night vector of all the role action claims
  // for that role/night. Can be more than 1 for two reasons: double-claims, and
  // also there are some rare roles that can get more than one role action per
  // night (Philosopher).
  unordered_map<Role, vector<vector<const internal::RoleAction*>>>
      GetRoleActionClaimsByNight() const;
  bool IsInfoExpected(int player, Role role, const Time& t) const;

  // Returns whether everyone has made claims regarding their role and the
  // resulting info for every day. Only fully claimed games are solvable
  // (otherwise, too many possibilities).
  absl::Status IsFullyClaimed() const;

 private:
  bool IsStrongClaim(const internal::Claim& c) const;
  RoleAction RoleActionToProto(const internal::RoleAction& ra) const;
  internal::RoleAction RoleActionFromProto(const RoleAction& pb) const;
  internal::Audience AudienceFromProto(const Audience& a) const;
  Audience AudienceToProto(const internal::Audience& a) const;
  internal::Whisper WhisperFromProto(const Whisper& w) const;
  Whisper WhisperToProto(const internal::Whisper& w) const;
  internal::Claim ClaimFromProto(const Claim& pb) const;
  Claim ClaimToProto(const internal::Claim& claim) const;
  void ValidateRoleAction(const internal::RoleAction& ra) const;
  void ValidateGrimoireInfo(const GrimoireInfo& info) const;
  void ValidateRoleChange(int player, Role prev, Role role);
  bool DemonDayKilled() const;
  bool ImpStarpassed() const;
  bool IsKnownStartingDemon(int player) const;
  bool IsKnownStartingMinion(int player) const;
  bool IsKnownEvil(int player) const;
  bool IsKnownDemonBluff(Role role) const;
  bool DiedAtNight(int player, const Time& time) const {
    Time death = TimeOfDeath(player);
    return death.Initialized() && death <= time && !death.is_day;
  }

  GameLog log_;  // Current state as a proto.
  Perspective perspective_;
  Script script_;
  vector<string> players_;
  unordered_map<string, int> player_index_;
  int num_players_, num_outsiders_, num_minions_;
  Time cur_time_;
  vector<vector<bool>> is_alive_day_;  // Is player alive on start of day x.
  vector<vector<bool>> is_alive_night_;  // Is player alive on start of night x.
  vector<int> num_alive_day_;  // # of alive players at start of day x.
  vector<int> num_alive_night_;  // # of alive players at start of night x.
  vector<internal::Nomination> nominations_;  // All Nominations.
  vector<bool> dead_vote_used_;  // x player, true when dead and voted.
  int on_the_block_;  // player currently on the block.
  vector<int> executions_;  // x day
  // Not the same to executions_, because executing dead players is valid.
  vector<int> execution_deaths_;  // x day
  vector<int> night_deaths_;  // x day
  Team victory_;  // The winning team, if the game is over.
  // In player perspective, the player whose perspective this is.
  int perspective_player_;
  vector<Role> perspective_player_shown_token_;  // x night.
  vector<internal::RoleAction> role_actions_;
  vector<internal::Claim> claims_;
  internal::MinionInfo minion_info_;
  internal::DemonInfo demon_info_;  // Both ST and demon perspective.
  // These variables are only used in the storyteller perspective.
  vector<vector<Role>> st_night_roles_;  // Player roles, x night.
  vector<vector<Role>> st_day_roles_;  // Player roles, x day.
  vector<vector<Role>> st_shown_tokens_;  // Player shown tokens, x night.
  int st_red_herring_;
};
}  // namespace botc

#endif  // SRC_GAME_STATE_H_
