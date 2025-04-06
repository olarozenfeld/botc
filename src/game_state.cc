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

#include <algorithm>
#include <set>
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "ortools/base/logging.h"
#include "src/game_log.pb.h"
#include "src/util.h"

// TODO(olaola):
// * Validate player interactions (e.g. exactly 1 FT action per night)
// * Validate night order.
namespace botc {
template<typename E> vector<E> RepeatedFieldToVector(
    google::protobuf::RepeatedField<int> const& rf) {
  vector<E> r;
  for (int v : rf) {
    r.push_back(E(v));
  }
  return r;
}

bool IsRoleInRoles(Role role, absl::Span<const Role> roles) {
  return std::find(roles.begin(), roles.end(), role) != roles.end();
}

bool IsDemonRole(Role role) {
  return kRoleMetadata[role].role_type == DEMON;
}

bool IsMinionRole(Role role) {
  return kRoleMetadata[role].role_type == MINION;
}

bool IsTownsfolkRole(Role role) {
  return kRoleMetadata[role].role_type == TOWNSFOLK;
}

bool IsOutsiderRole(Role role) {
  return kRoleMetadata[role].role_type == OUTSIDER;
}

bool IsGoodRole(Role role) {
  RoleType t = kRoleMetadata[role].role_type;
  return t == TOWNSFOLK || t == OUTSIDER;
}

bool IsEvilRole(Role role) {
  return !IsGoodRole(role);
}

bool IsDayActionRole(Role role) {
  return kRoleMetadata[role].day_action;
}

bool IsPublicActionRole(Role role) {
  return kRoleMetadata[role].public_action;
}

bool IsFirstNightOnlyRole(Role role) {
  const auto& m = kRoleMetadata[role];
  return m.first_night > 0 && m.other_night == 0;
}

// TODO(olaola): here and below, initialize statically and cache.
const absl::Span<const Role> AllRoles(Script s) {
  switch (s) {
    case TROUBLE_BREWING:
      return kTroubleBrewingRoles;
    default:
      CHECK(false) << "Unsupported script: " << Script_Name(s);
  }
  return {};
}

const vector<Role> FilterRoles(Script s, RoleFilter f) {
  const absl::Span<const Role> all = AllRoles(s);
  vector<Role> r;
  std::copy_if(all.begin(), all.end(), std::back_inserter(r), f);
  return r;
}

const vector<Role> GoodRoles(Script s) {
  return FilterRoles(s, IsGoodRole);
}

const vector<Role> EvilRoles(Script s) {
  return FilterRoles(s, IsEvilRole);
}

const vector<Role> TownsfolkRoles(Script s) {
  return FilterRoles(s, IsTownsfolkRole);
}

const vector<Role> OutsiderRoles(Script s) {
  return FilterRoles(s, IsOutsiderRole);
}

const vector<Role> MinionRoles(Script s) {
  return FilterRoles(s, IsMinionRole);
}

const vector<Role> DemonRoles(Script s) {
  return FilterRoles(s, IsDemonRole);
}

ostream& operator<<(ostream& os, const Time& t) {
  os << string(t);
  return os;
}
bool operator<(const Time& l, const Time& r) {
  return l.count < r.count || (l.count == r.count && !l.is_day && r.is_day);
}
bool operator>(const Time& l, const Time& r) { return r < l; }
bool operator<=(const Time& l, const Time& r) { return !(l > r); }
bool operator>=(const Time& l, const Time& r) { return !(l < r); }
bool operator==(const Time& l, const Time& r) {
  return l.is_day == r.is_day && l.count == r.count;
}

bool IsSupportedScript(Script s) {
  absl::Span<const Script> supported(kSupportedScripts);
  return std::find(supported.begin(), supported.end(), s) != supported.end();
}

namespace {
Time ClaimTime(const Claim& claim) {
  switch (claim.time_case()) {
    case Claim::kDay:
      return Time::Day(claim.day());
    case Claim::kNight:
      return Time::Night(claim.night());
    default:
      return Time();  // Empty time, will be deduced based on the claim.
  }
}

void SetClaimTime(const Time& t, Claim* claim) {
  if (t.Initialized()) {
    if (t.is_day) {
      claim->set_day(t.count);
    } else {
      claim->set_night(t.count);
    }
  }
}
}  // namespace

namespace internal {

SoftRole NewSoftRole(absl::Span<const Role> roles) {
  SoftRole res;
  for (Role role : roles) {
    res.add_roles(role);
  }
  return res;
}

SoftRole NewSoftRoleNot(absl::Span<const Role> roles) {
  SoftRole res = NewSoftRole(roles);
  res.set_is_not(true);
  return res;
}

SoftRole NewSoftRole(RoleType rt) {
  SoftRole res;
  res.set_role_type(rt);
  return res;
}

SoftRole NewSoftRoleNot(RoleType rt) {
  SoftRole res = NewSoftRole(rt);
  res.set_is_not(true);
  return res;
}
}  // namespace internal

GameState::GameState(
    Perspective perspective, Script script, absl::Span<const string> players)
    : perspective_(perspective), script_(script), num_players_(players.size()),
      cur_time_(), dead_vote_used_(num_players_), on_the_block_(kNoPlayer),
      victory_(TEAM_UNSPECIFIED), perspective_player_(kNoPlayer),
      minion_info_(), demon_info_(), st_red_herring_(kNoPlayer) {
  CHECK_NE(script_, SCRIPT_UNSPECIFIED)
      << "Need to specify script";
  CHECK(IsSupportedScript(script_))
      << Script_Name(script_) << " script is not currently supported";
  CHECK_NE(perspective_, PERSPECTIVE_UNSPECIFIED)
      << "Need to specify perspective";
  CHECK_GE(num_players_, 5);
  CHECK_LE(num_players_, 15);
  log_.set_perspective(perspective_);
  log_.set_script(script_);
  num_outsiders_ = kNumOutsiders[num_players_ - 5];
  num_minions_ = kNumMinions[num_players_ - 5];
  int player_index = 0;
  for (const auto& name : players) {
    CHECK(!name.empty()) << "Player name cannot be empty string";
    players_.push_back(name);
    player_index_[name] = player_index++;
    log_.add_players(name);
  }
}

GameState GameState::FromProto(const GameLog& log) {
  vector<string> players(log.players().begin(), log.players().end());
  GameState g = GameState(log.perspective(), log.script(), players);
  if (log.setup().player_roles_size() > 0) {
    unordered_map<string, Role> player_roles(log.setup().player_roles().begin(),
                                             log.setup().player_roles().end());
    g.SetRoles(player_roles);
  }
  if (!log.setup().red_herring().empty()) {
    g.SetRedHerring(log.setup().red_herring());
  }
  for (const auto& event : log.events()) {
    g.AddEvent(event);
  }
  return g;
}

GameState& GameState::SetRoles(const unordered_map<string, Role>& roles) {
  if (perspective_ == STORYTELLER) {
    CHECK_EQ(roles.size(), num_players_)
        << "Expected fully assigned player roles in storyteller perspective";
  } else {
    CHECK_EQ(roles.size(), 0)
        << "Player roles assigned in non-storyteller perspective.";
  }
  vector<Role> player_roles(num_players_);
  for (const auto& pr : roles) {
    const string& name = pr.first;
    CHECK_NE(pr.second, ROLE_UNSPECIFIED)
        << "Got unassigned role for player " << name;
    player_roles[PlayerIndex(name)] = pr.second;
  }
  *(log_.mutable_setup()->mutable_player_roles()) =
      google::protobuf::Map<string, Role>(roles.begin(), roles.end());
  st_night_roles_.push_back(player_roles);
  return *this;
}

GameState& GameState::SetRoles(absl::Span<const Role> roles) {
  CHECK_EQ(roles.size(), num_players_);
  unordered_map<string, Role> pr;
  int i = 0;
  for (Role role : roles) {
    pr[players_[i++]] = role;
  }
  return SetRoles(pr);
}

GameState& GameState::SetRedHerring(const string& red_herring) {
  if (perspective_ != STORYTELLER) {
    CHECK(red_herring.empty())
        << "Red-herring info in non-storyteller perspective.";
  }
  CHECK_EQ(st_night_roles_.size(), 1)
      << "Red herring info needs to be set after roles";
  const bool has_ft = IsRoleInRoles(FORTUNE_TELLER, st_night_roles_.back());
  CHECK_EQ(has_ft, !red_herring.empty())
      << "A game needs to have a red herring if and only if a Fortune Teller "
      << "is in play";
  log_.mutable_setup()->set_red_herring(red_herring);
  st_red_herring_ = PlayerIndex(red_herring, true);
  return *this;
}

GameState& GameState::AddEvent(const Event& event) {
  vector<string> players(event.whisper().players().begin(),
                         event.whisper().players().end());
  switch (event.details_case()) {
    case Event::kDay:
      AddDay(event.day());
      break;
    case Event::kNight:
      AddNight(event.night());
      break;
    case Event::kStorytellerInteraction:
      AddStorytellerInteraction(event.storyteller_interaction());
      break;
    case Event::kNomination:
      AddNomination(event.nomination());
      break;
    case Event::kVote:
      AddVote(event.vote());
      break;
    case Event::kExecution:
      AddExecution(event.execution());
      break;
    case Event::kDeath:
      AddDeath(event.death());
      break;
    case Event::kNightDeath:
      AddNightDeath(event.night_death());
      break;
    case Event::kClaim:
      AddClaim(event.claim());
      break;
    case Event::kWhisper:
      AddWhisper(players, event.whisper().initiator());
      break;
    case Event::kVictory:
      AddVictory(event.victory());
      break;
    default:
      CHECK(false) << "Expected a valid event details, got: "
                    << event.details_case();
  }
  return *this;
}

GameState& GameState::AddNight(int count) {
  CHECK_EQ(cur_time_ + 1, Time::Night(count))
    << cur_time_ << " needs to be followed by " << cur_time_ + 1;
  log_.add_events()->set_night(count);
  ++cur_time_;
  if (perspective_ == STORYTELLER) {
    if (st_night_roles_.size() < count) {
      st_night_roles_.push_back(st_day_roles_.back());
    }
    vector<Role> shown_tokens = (count == 1 ?
        vector<Role>(num_players_, ROLE_UNSPECIFIED) : st_shown_tokens_.back());
    st_shown_tokens_.push_back(shown_tokens);
  }
  if (perspective_ == PLAYER) {
    perspective_player_shown_token_.push_back(count == 1 ? ROLE_UNSPECIFIED :
        perspective_player_shown_token_.back());
  }
  if (count == 1) {
    is_alive_night_.push_back(vector<bool>(num_players_, true));
    num_alive_night_.push_back(num_players_);
  } else if (is_alive_night_.size() < cur_time_.count) {
    is_alive_night_.push_back(is_alive_day_.back());
    num_alive_night_.push_back(num_alive_day_.back());
  }
  return *this;
}

GameState& GameState::AddDay(int count) {
  CHECK_EQ(cur_time_ + 1, Time::Day(count))
    << cur_time_ << " needs to be followed by " << cur_time_ + 1;
  log_.add_events()->set_day(count);
  ++cur_time_;
  on_the_block_ = kNoPlayer;
  declare_no_executions_ = false;
  executions_.push_back(kNoPlayer);
  execution_deaths_.push_back(kNoPlayer);
  night_deaths_.push_back(kNoPlayer);
  if (perspective_ == STORYTELLER && st_day_roles_.size() < count) {
    st_day_roles_.push_back(st_night_roles_.back());
  }
  if (count == 1) {
    is_alive_day_.push_back(vector<bool>(num_players_, true));
    num_alive_day_.push_back(num_players_);
  } else {
    is_alive_day_.push_back(is_alive_night_.back());
    num_alive_day_.push_back(num_alive_night_.back());
  }
  return *this;
}

GameState& GameState::AddStorytellerInteraction(
    const StorytellerInteraction& interaction) {
  switch (interaction.details_case()) {
    case StorytellerInteraction::kShownToken:
      AddShownToken(interaction.player(), interaction.shown_token());
      break;
    case StorytellerInteraction::kShownAlignment:
      CHECK(false) << "Not implemented yet";
      break;
    case StorytellerInteraction::kMinionInfo:
      AddMinionInfo(interaction.player(), interaction.minion_info());
      break;
    case StorytellerInteraction::kDemonInfo:
      AddDemonInfo(interaction.player(), interaction.demon_info());
      break;
    case StorytellerInteraction::kRoleAction:
      AddRoleAction(interaction.player(), interaction.role_action());
      break;
    case StorytellerInteraction::kRoleActionEffect:
      CHECK(false) << "Not implemented yet";
      break;
    default:
      CHECK(false) << "Expected a valid interaction details, got: "
                   << interaction.details_case();
  }
  return *this;
}

GameState& GameState::AddNomination(const Nomination& nomination) {
  return AddNomination(nomination.nominator(), nomination.nominee());
}

GameState& GameState::AddNomination(const string& nominator,
                                    const string& nominee) {
  auto* nomination_pb = log_.add_events()->mutable_nomination();
  nomination_pb->set_nominator(nominator);
  nomination_pb->set_nominee(nominee);
  CHECK(cur_time_.is_day) << "Nominations can only occur during the day.";
  const int nominator_index = PlayerIndex(nominator);
  const int nominee_index = PlayerIndex(nominee);
  CHECK(IsAlive(nominator_index))
      << nominator << " is dead and cannot nominate.";
  for (auto i = nominations_.rbegin(); i != nominations_.rend(); ++i) {
    if (i->time != cur_time_) {
      break;
    }
    CHECK_NE(i->nominator, nominator_index)
      << nominator << " has already nominated today.";
    CHECK_NE(i->nominee, nominee_index)
        << nominee << " has already been nominated today.";
  }
  nominations_.push_back({.time = cur_time_, .nominator = nominator_index,
                          .nominee = nominee_index});
  return *this;
}

GameState& GameState::AddVote(const Vote& vote) {
  // I should be able to use the votes repeated field as absl::Span, but I
  // failed at that, so I'll just copy it to a vector:
  return AddVote(vector<string>(vote.votes().begin(), vote.votes().end()),
                 vote.num_votes(), vote.on_the_block());
}

GameState& GameState::AddVote(absl::Span<const string> votes,
                              int num_votes,
                              const string& on_the_block) {
  Vote* vote_pb = log_.add_events()->mutable_vote();
  vote_pb->set_num_votes(num_votes);
  vote_pb->mutable_votes()->Assign(votes.begin(), votes.end());
  vote_pb->set_on_the_block(on_the_block);
  CHECK(!nominations_.empty()) << "A vote must have a preceding nomination.";
  internal::Nomination& nomination = nominations_.back();
  nomination.virgin_proc = false;  // Otherwise we'd have an execution.
  // Computing votes needed to put on the block.
  int needed_votes = 0;
  for (auto n = nominations_.rbegin(); n != nominations_.rend(); ++n) {
    if (n->time != cur_time_) {
      break;
    }
    if (needed_votes < n->votes.size()) {
      needed_votes = n->votes.size();
    }
  }
  // TODO(olaola): validate vote correctness better!
  for (const string& name : votes) {
    const int i = PlayerIndex(name);
    CHECK(!dead_vote_used_[i]) << name << " has already used their dead vote";
    if (!IsAlive(i)) {
      dead_vote_used_[i] = true;
    }
    nomination.votes.push_back(i);
  }
  int cur_block = PlayerIndex(on_the_block, true);
  int cur_votes = num_votes >= votes.size() ? num_votes : votes.size();
  if (on_the_block_ == kNoPlayer) {
    int votes_required =
        needed_votes == 0 ? (NumAlive() + 1) / 2 : needed_votes + 1;
    if (cur_votes < votes_required) {
      // Vote fails, nothing changed.
      CHECK_EQ(cur_block, on_the_block_)
          << absl::StrFormat("Needed %d votes to put %s on the block, got %d",
                             votes_required, players_[nomination.nominee],
                             cur_votes);
    } else {
      // Vote succeeds.
      CHECK_EQ(cur_block, nomination.nominee)
          << absl::StrFormat("%s expected to go on the block, got: %s",
                             players_[nomination.nominee], on_the_block);
    }
  } else {
    if (cur_votes < needed_votes) {
      // Vote fails, nothing changed.
      CHECK_EQ(cur_block, on_the_block_)
          << absl::StrFormat("Needed %d votes to put %s on the block, got %d",
                             needed_votes + 1, players_[nomination.nominee],
                             cur_votes);
    } else if (cur_votes == needed_votes) {
      // Tied vote, no one on the block.
      CHECK_EQ(cur_block, kNoPlayer)
          << absl::StrFormat("Tied vote, no one goes on the block, got: %s",
                             on_the_block);
    } else {
      // Vote succeeds.
      CHECK_EQ(cur_block, nomination.nominee)
          << absl::StrFormat("%s expected to go on the block, got: %s",
                             players_[nomination.nominee], on_the_block);
    }
  }
  on_the_block_ = cur_block;
  // TODO(olaola): add Butler constraints here (could only vote with master).
  return *this;
}

GameState& GameState::AddExecution(const string& name) {
  log_.add_events()->set_execution(name);
  CHECK(cur_time_.is_day) << "Executions can only occur during the day.";
  CHECK(!declare_no_executions_) << "No executions was already declared.";
  const int executee = PlayerIndex(name, true);
  if (executee == kNoPlayer) {
    declare_no_executions_ = true;
    return *this;
  }
  CHECK_EQ(executions_.back(), kNoPlayer)
      << "More than one execution attempted.";
  CHECK(!nominations_.empty()) << "Execution must have a preceding nomination.";
  internal::Nomination& nomination = nominations_.back();

  if (executee != on_the_block_) {
    CHECK_EQ(executee, nomination.nominator)
        << absl::StrFormat("Execution needs to be either of %s (who is on the "
                            "block), or of %s who is last to nominate, got %s",
                            (on_the_block_ == kNoPlayer ? "nobody" :
                             players_[on_the_block_]),
                            players_[nomination.nominator], name);
    nomination.virgin_proc = true;
  }
  executions_.back() = executee;
  return *this;
}

GameState& GameState::AddNominationVoteExecution(const string& nominator,
                                                 const string& executee) {
  AddNomination(nominator, executee);
  AddVote({}, NumAlive() - Deaths().size(), executee);
  AddExecution(executee);
  return *this;
}

GameState& GameState::AddNightDeath(const string& name) {
  log_.add_events()->set_night_death(name);
  // Deaths are Storyteller announcements of deaths, hence they only occur
  // during the day.
  CHECK(cur_time_.is_day)
      << "Death annoucements can only occur during the day.";
  const int i = PlayerIndex(name);
  CHECK(IsAlive(i)) << "What is dead may never die: " << name;
  int& night_death = night_deaths_.back();
  CHECK_EQ(night_death, kNoPlayer)
      << "No two night deaths in Trouble Brewing";
  night_death = i;
  is_alive_day_.back()[i] = false;
  --(num_alive_day_.back());
  return *this;
}

GameState& GameState::AddDeath(const string& name) {
  log_.add_events()->set_death(name);
  // Deaths are Storyteller announcements of deaths, hence they only occur
  // during the day.
  CHECK(cur_time_.is_day)
      << "Death annoucements can only occur during the day.";
  const int i = PlayerIndex(name);
  CHECK(IsAlive(i)) << "What is dead may never die: " << name;
  // Day deaths in TB are either executions or Slayer shots.
  const int executee = executions_.back();
  if (executee != kNoPlayer) {
    CHECK_EQ(i, executee)
        << absl::StrFormat("Expected death of executee %s, got %s.",
                           players_[executee], name);
    execution_deaths_.back() = executee;
  } else {
    // Must be a Slayer kill, due to order.
    CHECK(role_actions_.size() > 0 && role_actions_.back().acting == SLAYER)
        << name << ": no possible death cause";
    auto& slayer_shot = role_actions_.back();
    slayer_shot.yes = true;  // Killed.
    int target = slayer_shot.players[0], slayer = slayer_shot.player;
    CHECK_EQ(i, target)
        << absl::StrFormat("Expected death of Slayer shot %s, got %s.",
                           players_[target], name);
    CHECK(IsAlive(slayer))
        << "Slayer " << players_[slayer] << " needs to be alive to proc.";
  }
  if (is_alive_night_.size() <= cur_time_.count) {
    is_alive_night_.push_back(is_alive_day_.back());
    num_alive_night_.push_back(num_alive_day_.back());
  }
  is_alive_night_.back()[i] = false;
  --(num_alive_night_.back());
  return *this;
}

GameState& GameState::AddRoleClaims(absl::Span<const Role> roles,
                                    const string& starting_player) {
  int i = PlayerIndex(starting_player);
  for (Role role : roles) {
    AddClaimRole(players_[i], role);
    i = (i + 1) % num_players_;
  }
  return *this;
}

GameState& GameState::AddClaim(const internal::Claim& claim) {
  *(log_.add_events()->mutable_claim()) = ClaimToProto(claim);
  if (!IsStrongClaim(claim)) {
    return *this;
  }
  claims_.push_back(claim);
  auto& c = claims_.back();
  CHECK(cur_time_.is_day) << absl::StrFormat(
      "Claims only occur during the day, got %s claiming on %s.",
      PlayerName(c.player), cur_time_);
  c.claim_time = cur_time_;
  auto& ra = c.role_action;
  const bool day_role = IsDayActionRole(ra.acting);
  switch (claim.claim_case) {
    case Claim::kRole:
      CHECK_NE(claim.role, ROLE_UNSPECIFIED) << "Invalid claimed role";
      if (!c.time.Initialized()) {
        // Guess the role claim time if omitted, from either night 1 or last
        // night. In TB, omitted means night 1, except for the Imp->Recluse
        // starpass. In BMR, no roles can change, so always assume night 1.
        c.time = cur_time_ - 1;
        if (script_ == BAD_MOON_RISING ||
            (script_ == TROUBLE_BREWING && claim.role != IMP)) {
          c.time = Time::Night(1);
        }
      }
      CHECK(!c.time.is_day)
          << "Role claims need to be for nights, when role tokens are shown";
      break;
    case Claim::kRoleAction:
      CHECK_NE(ra.acting, ROLE_UNSPECIFIED)
          << "Each role action claim needs to specify an acting role";
      if (!c.time.Initialized()) {
        // Guess the action claim time if omitted, from either day 1 or today.
        c.time = IsFirstNightOnlyRole(ra.acting) ? Time::Night(1) :
            (day_role ? cur_time_ : cur_time_ - 1);
      }
      CHECK_EQ(c.time.is_day, day_role)
          << absl::StrFormat("Role action claims for %s need to be by %s",
                             Role_Name(ra.acting), day_role ? "day" : "night");
      break;
    default:
      CHECK(false)
          << "Unexpected claim details: " << claim.claim_case;
  }
  ra.time = c.time;
  if (ra.player == kNoPlayer) {
    ra.player = c.player;
  }
  if (ra.acting == UNDERTAKER) {
    ra.players = {execution_deaths_[ra.time.count - 2]};
  }
  return *this;
}

vector<int> GameState::AliveNeighbors(int player, const Time& time) const {
  CHECK_GE(NumAlive(time), 3)
      << "Less than 3 alive players, game didn't end";
  vector<int> result;
  int i = (player + 1) % num_players_;
  while (!IsAlive(i, time)) {
    i = (i + 1) % num_players_;
  }
  result.push_back(i);
  i = (player + num_players_ - 1) % num_players_;
  while (!IsAlive(i, time)) {
    i = (i  + num_players_ - 1) % num_players_;
  }
  result.push_back(i);
  return result;
}

GameState& GameState::AddWhisper(absl::Span<const string> players,
                                 const string& initiator) {
  return *this;
}

GameState& GameState::AddVictory(Team victory) {
  log_.add_events()->set_victory(victory);
  CHECK_NE(victory, TEAM_UNSPECIFIED) << "Victory needs to be GOOD or EVIL";
  CHECK_EQ(victory_, TEAM_UNSPECIFIED)
      << "Team " << Team_Name(victory_) << " has already won.";
  CHECK(cur_time_.is_day) << "Victory can only be announced during the day.";
  victory_ = victory;
  return *this;
}

// Syntactic sugar for the Storyteller perspective.
GameState& GameState::AddAllShownTokens(absl::Span<const Role> roles) {
  CHECK_EQ(perspective_, STORYTELLER)
      << "Only the Storyteller perspective can show all tokens";
  int i = 0;
  for (Role role : roles) {
    AddShownToken(players_[i++], role);
  }
  return *this;
}

// Returns the deaths chronologically. Execution is always the last death.
vector<int> GameState::Deaths(const Time& time) const {
  vector<int> result;
  if (time.is_day) {
    const auto slayer_shots = GetRoleActions(SLAYER);
    for (const auto* shot : slayer_shots) {
      if (shot->yes && shot->time == time) {
        result.push_back(shot->players[0]);
      }
    }
    if (execution_deaths_.size() >= time.count) {
      int death = execution_deaths_[time.count - 1];
      if (death != kNoPlayer) {
        result.push_back(death);
      }
    }
  } else {
    if (night_deaths_.size() >= time.count) {
      int death = night_deaths_[time.count - 1];
      if (death != kNoPlayer) {
        result.push_back(death);
      }
    }
  }
  return result;
}

vector<string> GameState::DeathsNames(const Time& time) const {
  vector<string> result;
  for (int i : Deaths(time)) {
    result.push_back(PlayerName(i));
  }
  return result;
}

bool GameState::DemonDayKilled() const {
  // Assumes ST perspective, night time.
  for (int i : Deaths(cur_time_ - 1)) {
    if (IsDemonRole(st_day_roles_.back()[i])) {
      return true;
    }
  }
  return false;
}

bool GameState::ImpStarpassed() const {
  // Assumes ST perspective, night time.
  const auto imp_picks = GetRoleActions(IMP);
  CHECK_GT(imp_picks.size(), 0) << "Missing Imp action " << cur_time_;
  const auto& imp_pick = imp_picks.back();
  return imp_pick->player == imp_pick->players[0];
  // TODO(olaola): check not poisoned.
}

void GameState::ValidateRoleChange(int player, Role prev, Role role) {
  if (cur_time_ == Time::Night(1) || prev == ROLE_UNSPECIFIED) {
    return;
  }
  if (script_ == TROUBLE_BREWING) {
    CHECK(cur_time_ == Time::Night(1) || role == IMP)
        << "Tokens other than Imp are only shown on night 1 in Trouble Brewing";
  } else if (script_ == BAD_MOON_RISING) {
    CHECK(cur_time_ == Time::Night(1))
        << "Tokens are only shown on night 1 in Bad Moon Rising";
  }
  // TODO(olaola): support S&V role changes.
  if (cur_time_ > Time::Night(1) && IsDemonRole(role)) {
    CHECK(IsMinionRole(prev) || prev == RECLUSE)
        << "Only minions or the Recluse can become the Imp";
    if (perspective_ == STORYTELLER) {
      // The Imp must be either day killed or self-pick tonight.
      if (DemonDayKilled()) {
        CHECK_EQ(prev, SCARLET_WOMAN)
            << absl::StrFormat("Only the Scarlet Woman can become the Demon "
                               "after a day death, got %s", PlayerName(player));
        // TODO(olaola): validate health/players alive.
        st_night_roles_.back()[player] = role;
        return;
      }
      CHECK(ImpStarpassed())
          << absl::StrFormat("Imp needs to starpass in order for %s to become "
                             "the Imp", PlayerName(player));
      CHECK_EQ(st_day_roles_.size(), cur_time_.count - 1)
          << "An Imp can starpass to only one player";
      st_day_roles_.push_back(st_night_roles_.back());
      st_day_roles_.back()[player] = role;
    }
    return;
  }
  CHECK(prev == DRUNK && IsTownsfolkRole(role) || role == prev)
      << absl::StrFormat("Expected %s to be shown %s, got %s",
                         PlayerName(player), Role_Name(prev), Role_Name(role));
}

GameState& GameState::AddShownToken(const string& player, Role role) {
  auto* si_pb = log_.add_events()->mutable_storyteller_interaction();
  si_pb->set_player(player);
  si_pb->set_shown_token(role);
  CHECK_NE(role, ROLE_UNSPECIFIED) << "Need to specify a role";
  CHECK_NE(role, DRUNK) << "No one can be shown the DRUNK token";
  CHECK_NE(perspective_, OBSERVER) << "Observer cannot be shown tokens";
  CHECK(!cur_time_.is_day) << "Tokens are only shown at night";
  const int i = PlayerIndex(player);
  const Role prev = (perspective_ == STORYTELLER ? st_night_roles_.back()[i] :
                     perspective_player_shown_token_.back());
  ValidateRoleChange(i, prev, role);
  if (perspective_ == STORYTELLER) {
    st_shown_tokens_.back()[i] = role;
  } else {  // PLAYER
    CHECK(perspective_player_ == kNoPlayer || perspective_player_ == i)
        << "Only " << player << " can be shown a token in player perspective";
    perspective_player_ = i;  // Deducing the perspective player.
    // TODO(olaola): validate role change.
    perspective_player_shown_token_.back() = role;
  }
  return *this;
}

GameState& GameState::AddMinionInfo(const string& player, const string& demon,
                                    absl::Span<const string> minions) {
  auto* si_pb = log_.add_events()->mutable_storyteller_interaction();
  si_pb->set_player(player);
  MinionInfo* info_pb = si_pb->mutable_minion_info();
  info_pb->set_demon(demon);
  info_pb->mutable_minions()->Assign(minions.begin(), minions.end());
  CHECK_GE(num_players_, 7) << "Minion info unavailable for < 7 players";
  CHECK(perspective_ == STORYTELLER || perspective_ == PLAYER)
      << "Minion info can only be shown in player or storyteller perspective";
  CHECK_EQ(cur_time_, Time::Night(1)) << "Minion info is only shown on night 1";
  CHECK(IsMinionRole(ShownToken(player)))
      << "Player " << player
      << " needs to be shown a minion token in order to get minion info";
  CHECK_EQ(minions.size(), num_minions_ - 1)
      << absl::StrFormat("Expected %d fellow minions, got %d", num_minions_ - 1,
                         minions.size());
  CHECK_NE(player, demon) << "Demon needs to be different than all minions";
  vector<int> ms;
  for (const string& m : minions) {
    CHECK_NE(m, player)
        << absl::StrFormat("Minion info for %s should contain %d other minions",
                           player, num_minions_ - 1);
    CHECK_NE(m, demon) << "Demon needs to be different than all minions";
    ms.push_back(PlayerIndex(m));
  }
  minion_info_ = {.player = PlayerIndex(player), .demon = PlayerIndex(demon),
                  .minions = ms};
  // In the Storyteller perspective we don't keep the minion info, because we
  // can just re-create it.
  return *this;
}

GameState& GameState::AddMinionInfo(const string& player,
                                    const MinionInfo& minion_info) {
  const auto& minions = minion_info.minions();
  return AddMinionInfo(player, minion_info.demon(),
                       vector<string>(minions.begin(), minions.end()));
}

GameState& GameState::AddDemonInfo(const string& player,
                                   absl::Span<const string> minions,
                                   absl::Span<const Role> bluffs) {
  auto* si_pb = log_.add_events()->mutable_storyteller_interaction();
  si_pb->set_player(player);
  DemonInfo* info_pb = si_pb->mutable_demon_info();
  info_pb->mutable_minions()->Assign(minions.begin(), minions.end());
  info_pb->mutable_bluffs()->Assign(bluffs.begin(), bluffs.end());
  CHECK_GE(num_players_, 7) << "Demon info unavailable for < 7 players";
  CHECK(perspective_ == STORYTELLER || perspective_ == PLAYER)
      << "Demon info can only be shown in player or storyteller perspective";
  CHECK_EQ(cur_time_, Time::Night(1)) << "Demon info is only shown on night 1";
  const int demon = PlayerIndex(player);
  CHECK(IsDemonRole(ShownToken(demon)))
      << "Player " << player
      << " needs to be shown a Demon token in order to get demon info";
  CHECK_EQ(minions.size(), num_minions_)
      << "Demon info should have " << num_minions_ << " minions";
  vector<int> ms;
  for (const string& m : minions) {
    CHECK_NE(m, player) << "Demon needs to be different than all minions";
    ms.push_back(PlayerIndex(m));
  }
  CHECK_EQ(bluffs.size(), 3) << "Demon info should have 3 bluffs";
  for (Role bluff : bluffs) {
    CHECK(IsGoodRole(bluff))
        << "Expected demon bluffs good roles only, got " << Role_Name(bluff);
  }
  demon_info_ = {.player = demon, .minions = ms};
  for (Role bluff : bluffs) {
    demon_info_.bluffs.push_back(bluff);
  }
  return *this;
}

GameState& GameState::AddDemonInfo(const string& player,
                                   const DemonInfo& demon_info) {
  const auto& minions = demon_info.minions();
  vector<Role> bluffs = RepeatedFieldToVector<Role>(demon_info.bluffs());
  return AddDemonInfo(
      player, vector<string>(minions.begin(), minions.end()), bluffs);
}

GameState& GameState::AddRoleAction(const string& player,
                                    const internal::RoleAction& role_action) {
  auto* si_pb = log_.add_events()->mutable_storyteller_interaction();
  si_pb->set_player(player);
  *(si_pb->mutable_role_action()) = RoleActionToProto(role_action);
  role_actions_.push_back(role_action);
  auto& ra = role_actions_.back();
  ra.player = PlayerIndex(player);
  if (!ra.time.Initialized()) {
    ra.time = cur_time_;
  }
  if (ra.acting == ROLE_UNSPECIFIED) {
    // Infer role from previous info.
    ra.acting = ShownToken(ra.player);
    CHECK_NE(ra.acting, ROLE_UNSPECIFIED)
        << "Cannot infer role for player action " << player;
  }
  if (ra.acting == UNDERTAKER) {
    ra.players = {execution_deaths_[ra.time.count - 1]};
  }
  ValidateRoleAction(ra);
  return *this;
}

GameState& GameState::AddRoleAction(const string& player,
                                    const RoleAction& ra) {
  return AddRoleAction(player, RoleActionFromProto(ra));
}

void GameState::ValidateRoleAction(const internal::RoleAction& ra) const {
  const string role_name = Role_Name(ra.acting);
  const string player = PlayerName(ra.player);
  const int i = ra.player;
  const bool day_role = IsDayActionRole(ra.acting);
  CHECK_EQ(cur_time_.is_day, day_role)
      << role_name << " actions only occur during the "
      << (day_role ? "day" : "night");
  // There are exceptions to this, e.g. Klutz.
  CHECK(IsAlive(i)) << "Dead players don't get role actions";
  CHECK(IsPublicActionRole(ra.acting) || perspective_ != OBSERVER)
      << absl::StrFormat("Observer cannot see %s actions", role_name);
  if (perspective_ == STORYTELLER) {
    const Role st_player_role = st_shown_tokens_.back()[i];
    CHECK_EQ(st_player_role, ra.acting)
        << absl::StrFormat("%s needs to be the %s, got %s", player,
                           role_name, Role_Name(st_player_role));
  } else if (perspective_ == PLAYER && !IsPublicActionRole(ra.acting)) {
    CHECK_EQ(i, perspective_player_)
        << absl::StrFormat("Only the %s or Storyteller perspective can see "
                          "%s actions", role_name, role_name);
    CHECK_EQ(perspective_player_shown_token_.back(), ra.acting)
        << absl::StrFormat("%s needs to be the %s, got %s", player,
                          role_name,
                          Role_Name(perspective_player_shown_token_.back()));
  }
  // TODO(olaola): check action used (day, night, once, etc.)
  const int max_chef_number = num_minions_ + 1;  // Possible Recluse
  switch (ra.acting) {
    case WASHERWOMAN:
      CHECK_EQ(ra.players.size(), 2)
          << "Washerwoman should have exactly 2 pings";
      for (int p : ra.players) {
        CHECK_NE(p, kNoPlayer);
      }
      CHECK_EQ(ra.roles.size(), 1) << "Washerwoman should learn 1 role";
      CHECK(IsTownsfolkRole(ra.roles[0]));
      break;
    case LIBRARIAN:
      if (ra.roles.empty()) {
        CHECK(ra.players.empty())
            << "Librarian with no outsiders learns no pings";
        break;
      }
      CHECK_EQ(ra.players.size(), 2) << "Librarian should have exactly 2 pings";
      for (int p : ra.players) {
        CHECK_NE(p, kNoPlayer);
      }
      CHECK_EQ(ra.roles.size(), 1) << "Librarian should learn 1 role";
      CHECK(IsOutsiderRole(ra.roles[0]));
      break;
    case INVESTIGATOR:
      CHECK_EQ(ra.players.size(), 2)
          << "Investigator should have 2 pings";
      for (int p : ra.players) {
        CHECK_NE(p, kNoPlayer);
      }
      CHECK_EQ(ra.roles.size(), 1) << "Investigator should learn 1 role";
      CHECK(IsMinionRole(ra.roles[0]));
      break;
    case CHEF:
      CHECK_GE(ra.number, 0) << "Expected Chef number >=0, got " << ra.number;
      CHECK_LE(ra.number, max_chef_number)
          << absl::StrFormat("Expected Chef number <=%d, got %d",
                             max_chef_number, ra.number);
      break;
    case EMPATH:
      // We don't check that empath_info is in [0,2], because in rare cases the
      // Storyteller technically could give a higher number to inform the Empath
      // that they are drunk or poisoned (if Good really needs help).
      CHECK_GE(ra.number, 0) << "Expected non-negative Empath info";
      break;
    case FORTUNE_TELLER:
      CHECK_EQ(ra.players.size(), 2)
          << "Fortune Teller should have 2 picks";
      CHECK_NE(ra.players[0], ra.players[1])
          << "Fortune Teller needs to pick two different players";
      break;
    case UNDERTAKER:
      CHECK_GE(cur_time_.count, 2) << "Undertaker gets info starting night 2";
      CHECK_NE(execution_deaths_.back(), kNoPlayer)
          << "Nobody was executed, no Undertaker info";
      CHECK_EQ(ra.roles.size(), 1) << "Undertaker should learn 1 role";
      CHECK_NE(ra.roles[0], ROLE_UNSPECIFIED)
          << "Undertaker needs to learn a valid role";
      break;
    case MONK:
      CHECK_EQ(ra.players.size(), 1) << "Monk should have 1 pick";
      CHECK_NE(i, ra.players[0]) << "Monk cannot pick themselves";
      break;
    case RAVENKEEPER:
      CHECK_EQ(ra.players.size(), 1) << "Ravenkeeper should have 1 pick";
      CHECK_EQ(ra.roles.size(), 1) << "Ravenkeeper should learn 1 role";
      CHECK_NE(ra.roles[0], ROLE_UNSPECIFIED)
          << "Ravenkeeper needs to learn a valid role";
      break;
    case SLAYER:
      CHECK_EQ(ra.players.size(), 1) << "Slayer should have 1 pick";
      break;
    case BUTLER:
      CHECK_EQ(ra.players.size(), 1) << "Butler should have 1 pick";
      CHECK_NE(i, ra.players[0]) << "Butler cannot pick themselves";
      break;
    case POISONER:
      CHECK_EQ(ra.players.size(), 1) << "Poisoner should have 1 pick";
      break;
    case SPY:
      ValidateGrimoireInfo(ra.grimoire_info);
      break;
    case IMP:
      CHECK_EQ(ra.players.size(), 1) << "Imp should have 1 pick";
      break;
    default:
      CHECK(false) << "Invalid role action role: " << Role_Name(ra.acting);
  }
}

void GameState::ValidateGrimoireInfo(const GrimoireInfo& info) const {
  vector<bool> player_info(num_players_);
  for (const auto& pi : info.player_info()) {
    const int i = PlayerIndex(pi.player());
    CHECK_NE(pi.role(), ROLE_UNSPECIFIED)
        << "Invalid Spy info role token for " << players_[i];
    player_info[i] = true;
    // TODO(olaola): validate shrouds info (needs to look into the future).
    // Ditto for real roles.
    if (perspective_ == STORYTELLER) {
      const Role shown_role = st_shown_tokens_.back()[i];
      CHECK_EQ(pi.role(), shown_role) << absl::StrFormat(
          "Spy info has %s shown %s, but should be shown %s", players_[i],
          Role_Name(pi.role()), Role_Name(shown_role));
    }
  }
  for (int i = 0; i < num_players_; ++i) {
    CHECK(player_info[i]) << "Missing Spy info for " << players_[i];
  }
}

GrimoireInfo GameState::GrimoireInfoFromRoles(absl::Span<const Role> roles,
                                              Role drunk_shown_token) const {
  GrimoireInfo gi;
  for (int i = 0; i < num_players_; ++i) {
    auto* pi = gi.add_player_info();
    pi->set_player(players_[i]);
    pi->set_role(roles[i] == DRUNK ? drunk_shown_token : roles[i]);
    if (roles[i] == DRUNK) {
      pi->add_tokens(IS_DRUNK);
    }
  }
  return gi;
}

RoleAction GameState::RoleActionToProto(const internal::RoleAction& ra) const {
  RoleAction pb;
  pb.set_acting(ra.acting);
  for (int p : ra.players) {
    pb.add_players(PlayerName(p));
  }
  pb.mutable_roles()->Assign(ra.roles.begin(), ra.roles.end());
  pb.set_number(ra.number);
  pb.set_yes(ra.yes);
  pb.set_team(ra.team);
  if (ra.grimoire_info.DebugString() != "") {  // Horrible hack.
    *(pb.mutable_grimoire_info()) = ra.grimoire_info;
  }
  return pb;
}

internal::RoleAction GameState::RoleActionFromProto(
    const RoleAction& pb) const {
  internal::RoleAction ra({.acting = pb.acting(), .number = pb.number(),
                           .yes = pb.yes(), .team = pb.team(),
                           .grimoire_info = pb.grimoire_info()});
  for (const string& p : pb.players()) {
    ra.players.push_back(PlayerIndex(p));
  }
  for (int role : pb.roles()) {
    ra.roles.push_back(Role(role));
  }
  return ra;
}

internal::Audience GameState::AudienceFromProto(const Audience& a) const {
  if (a.nobody()) {
    return internal::Audience::Nobody();
  }
  if (a.players_size() == 0) {
    return internal::Audience::Townsquare();
  }
  internal::Audience res;
  res.townsquare = false;
  for (const string& p : a.players()) {
    res.players.push_back(PlayerIndex(p));
  }
  return res;
}

Audience GameState::AudienceToProto(const internal::Audience& a) const {
  Audience res;
  if (a.IsEmpty()) {
    res.set_nobody(true);
    return res;
  }
  if (a.players.size() == 0) {
    res.set_townsquare(true);
    return res;
  }
  for (int p : a.players) {
    res.add_players(PlayerName(p));
  }
  return res;
}

internal::Whisper GameState::WhisperFromProto(const Whisper& w) const {
  CHECK_GE(w.players_size(), 2) << "A whisper needs to have at least 2 players";
  internal::Whisper res;
  for (const string& p : w.players()) {
    res.players.push_back(PlayerIndex(p));
  }
  res.initiator = PlayerIndex(w.initiator(), true);
  return res;
}

Whisper GameState::WhisperToProto(const internal::Whisper& w) const {
  Whisper res;
  for (int p : w.players) {
    res.add_players(PlayerName(p));
  }
  if (w.initiator != kNoPlayer) {
    res.set_initiator(PlayerName(w.initiator));
  }
  return res;
}

internal::Claim GameState::ClaimFromProto(const Claim& pb) const {
  internal::Claim claim({.player = PlayerIndex(pb.player(), true),
                         .audience = AudienceFromProto(pb.audience()),
                         .time = ClaimTime(pb),
                         .claim_case = pb.details_case(),
                         .role = pb.role(),
                         .soft_role = pb.soft_role()});
  if (pb.details_case() == Claim::kRoleAction) {
    claim.role_action = RoleActionFromProto(pb.role_action());
  }
  if (pb.details_case() == Claim::kRoleEffect) {
    claim.role_action = RoleActionFromProto(pb.role_effect());
  }
  if (pb.details_case() == Claim::kClaim) {
    claim.claim.reset(new internal::Claim(ClaimFromProto(pb.claim())));
  }
  if (pb.details_case() == Claim::kRetraction) {
    claim.claim.reset(new internal::Claim(ClaimFromProto(pb.retraction())));
  }
  return claim;
}

Claim GameState::ClaimToProto(const internal::Claim& claim) const {
  Claim pb;
  if (claim.player != kNoPlayer) {
    pb.set_player(PlayerName(claim.player));
  }
  if (!claim.audience.townsquare) {
    *(pb.mutable_audience()) = AudienceToProto(claim.audience);
  }
  SetClaimTime(claim.time, &pb);
  switch (claim.claim_case) {
    case Claim::kRole:
      pb.set_role(claim.role);
      break;
    case Claim::kSoftRole:
      *(pb.mutable_soft_role()) = claim.soft_role;
      break;
    case Claim::kRoleAction:
      *(pb.mutable_role_action()) = RoleActionToProto(claim.role_action);
      break;
    case Claim::kRoleEffect:
      *(pb.mutable_role_effect()) = RoleActionToProto(claim.role_action);
      break;
    case Claim::kClaim:
      *(pb.mutable_claim()) = ClaimToProto(*(claim.claim));
      break;
    case Claim::kRetraction:
      *(pb.mutable_retraction()) = ClaimToProto(*(claim.claim));
      break;
    default:
      CHECK(false) << "Invalid claim case: " << claim.claim_case;
      break;
  }
  return pb;
}

// Strong claims are the claims used for determining all mechanically possible
// solutions. Other claims may be used for determining the likelihood of the
// solutions. For now, all non-strong claims are ignored.
bool GameState::IsStrongClaim(const internal::Claim& c) const {
  if (c.player == kNoPlayer || !c.audience.townsquare) {
    // TODO(olaola): allow Evil to share info in a trusted way.
    return false;
  }
  return (c.claim_case == Claim::kRole ||
          (c.claim_case == Claim::kRoleAction &&
           c.role_action.IsWellDefined()));
  // TODO(olaola): support retractions and role effect claim.
}

bool internal::RoleAction::IsWellDefined() const {
  switch (acting) {
    case WASHERWOMAN:
    case INVESTIGATOR:
      return players.size() == 2 && roles.size() == 1;
    case LIBRARIAN:
      return ((players.size() == 2 && roles.size() == 1) ||
              (players.size() == 0 && roles.size() == 0));  // No outsiders.
    case CHEF:
    case EMPATH:
      return true;  // The number may be 0
    case FORTUNE_TELLER:
      return players.size() == 2;
    case UNDERTAKER:
      return roles.size() == 1;
    case MONK:
    case BUTLER:
      return players.size() == 1;
    case RAVENKEEPER:
      return players.size() == 1 && roles.size() == 1;
    default:
      return false;
  }
}

vector<const internal::RoleAction*> GameState::GetRoleActions(
    int player) const {
  vector<const internal::RoleAction*> result;
  for (const auto& ra : role_actions_) {
    if (ra.player == player) {
      result.push_back(&ra);
    }
  }
  return result;
}

vector<const internal::RoleAction*> GameState::GetRoleActions(Role role) const {
  vector<const internal::RoleAction*> result;
  for (const auto& ra : role_actions_) {
    if (ra.acting == role) {
      result.push_back(&ra);
    }
  }
  return result;
}

vector<Role> GameState::GetRoleClaimsByNight(int player) const {
  vector<Role> result(cur_time_.count);
  for (const auto& c : claims_) {
    if (c.claim_case == Claim::kRole && c.player == player) {
      // Override earlier role claims:
      for (Time n = c.time; n < cur_time_; n += 2) {
        result[n.count - 1] = c.role;
      }
    }
  }
  return result;
}

vector<vector<Role>> GameState::GetRoleClaimsByNight() const {
  vector<vector<Role>> result(num_players_, vector<Role>(cur_time_.count));
  for (const auto& c : claims_) {
    if (c.claim_case == Claim::kRole) {
      // Override earlier role claims:
      for (Time n = c.time; n < cur_time_; n += 2) {
        result[c.player][n.count - 1] = c.role;
      }
    }
  }
  return result;
}

unordered_map<Role, vector<vector<const internal::RoleAction*>>>
    GameState::GetRoleActionClaimsByNight() const {
  vector<vector<Role>> role_claims = GetRoleClaimsByNight();
  unordered_map<Role, vector<vector<const internal::RoleAction*>>> result;
  for (const auto& c : claims_) {
    if (c.claim_case != Claim::kRoleAction) {
      continue;
    }
    const internal::RoleAction& ra = c.role_action;
    const int n = ra.time.count;
    if (role_claims[ra.player][n - 1] == ra.acting) {
      auto& actions = result[ra.acting];
      if (actions.empty()) {
        actions.resize(cur_time_.count);
      }
      auto& night_actions = actions[n - 1];
      auto it = night_actions.begin();
      while (it != night_actions.end()) {
        if ((*it)->player == ra.player) {
          // Override earlier action claim, if exists (will need to be modified
          // for roles that may have multiple night actions):
          *it = &ra;
          break;
        }
        ++it;
      }
      if (it == night_actions.end()) {
        night_actions.push_back(&ra);
      }
    }
  }
  return result;
}

// TODO(olaola): generalize this, as possible, to other scripts. The problem is
// that on a role change, sometimes info will be expected on the previous role
// and sometimes not, depending on the cause of the role change and the night
// order. So it is, in fact, impossible to 100% generalize. Example: in a custom
// script, Jake claims to have been the Monk turned into the Fortune Teller on
// night 4. Whether we expect him to have Monk info or Fortune Teller info or
// both on that night is unclear -- it depends on who turned him and when do
// they go in the night order relative to the Monk and the Fortune Teller.
// For now, this supports Trouble Brewing only!
bool GameState::IsInfoExpected(int player, Role role, const Time& t) const {
  CHECK(!t.is_day) << "Only night roles are supported for now";
  const RoleMetadata& m = kRoleMetadata[role];
  if (t == Time::Night(1)) {
    return m.first_night > 0;
  }
  if (role == RAVENKEEPER) {
    // Only if died exactly the night before.
    return night_deaths_[t.count - 1] == player;
  }
  // For an Imp claim after Recluse starpass, we only turn into the Imp as a
  // result of the Imp action, so we're not expected info on the starpass night.
  if (role == IMP) {
    const auto role_claims = GetRoleClaimsByNight(player);
    if (t.count >= 2 && role_claims[t.count - 2] == RECLUSE) {
      return false;
    }
  }
  // Otherwise, player needs to be alive for info, or to go before the Imp.
  if (!IsAlive(player, t + 1) &&
      (night_deaths_[t.count - 1] != player ||
       m.other_night >= kRoleMetadata[IMP].other_night)) {
    return false;
  }
  if (m.optional_trigger) {
    if (role == UNDERTAKER) {
      // The trigger is public, need an execution.
      return execution_deaths_[t.count - 2] != kNoPlayer;
    }
    return false;  // In general, we cannot expect info for sure.
  }
  return m.other_night > 0;  // The character woke before the Imp.
}

// TODO(olaola): if ever needed, optimize this.
absl::Status GameState::IsFullyClaimed() const {
  auto role_claims = GetRoleClaimsByNight();
  auto role_action_claims = GetRoleActionClaimsByNight();
  vector<string> missing_claims;
  // Missing role claims for nights.
  for (int i = 0; i < num_players_; ++i) {
    Time n = Time::Night(1);
    while (n <= cur_time_ && role_claims[i][n.count - 1] == ROLE_UNSPECIFIED) {
      n += 2;
    }
    if (n > Time::Night(1)) {
      string error = absl::StrCat(players_[i], " is missing a role claim");
      if (n <= cur_time_) {
        absl::StrAppend(&error, absl::StrFormat(" for nights 1-%d",
                                                n.count - 1));
      }
      missing_claims.push_back(error);
    }
    // For info roles, check that we have all the expected info:
    while (n <= cur_time_) {
      const Role role = role_claims[i][n.count - 1];
      if (IsInfoExpected(i, role, n)) {
        auto it = role_action_claims.find(role);
        bool found = false;
        if (it != role_action_claims.end()) {
          for (const auto* ra : it->second[n.count - 1]) {
            if (ra->player == i) {
              found = true;
              break;
            }
          }
        }
        if (!found) {
          missing_claims.push_back(absl::StrFormat(
              "%s is missing a %s role action claim for night %d", players_[i],
              Role_Name(role), n.count));
        }
      }
      n += 2;
    }
  }

  return (missing_claims.size() == 0 ? absl::OkStatus() :
          absl::InvalidArgumentError(
              absl::StrCat("Missing claims: ",
                           absl::StrJoin(missing_claims, ", "))));
}

Time GameState::TimeOfDeath(int player) const {
  for (Time t = Time::Night(1); t < cur_time_; ++t) {
    if (IsAlive(player, t) && !IsAlive(player, t + 1)) {
      return t;
    }
  }
  return Time();
}

bool GameState::IsKnownDemonBluff(Role role) const {
  const auto& bluffs = demon_info_.bluffs;
  if (bluffs.empty()) {
    return false;
  }
  return (std::find(bluffs.begin(), bluffs.end(), role) != bluffs.end());
}

bool GameState::IsKnownStartingDemon(int player) const {
  set<int> known_demons;
  if (minion_info_.demon != kNoPlayer) {
    known_demons.insert(minion_info_.demon);
  }
  if (perspective_ == PLAYER) {
    Role player_role = ShownToken(perspective_player_, Time::Night(1));
    if (IsDemonRole(player_role)) {
      known_demons.insert(perspective_player_);
    }
  }
  return known_demons.find(player) != known_demons.end();
}

bool GameState::IsKnownStartingMinion(int player) const {
  set<int> known_minions;
  known_minions.insert(minion_info_.minions.begin(),
                       minion_info_.minions.end());
  known_minions.insert(demon_info_.minions.begin(),
                       demon_info_.minions.end());
  if (perspective_ == PLAYER) {
    Role player_role = ShownToken(perspective_player_, Time::Night(1));
    if (IsMinionRole(player_role)) {
      known_minions.insert(perspective_player_);
    }
  }
  return known_minions.find(player) != known_minions.end();
}

bool GameState::IsKnownEvil(int player) const {
  return IsKnownStartingDemon(player) || IsKnownStartingMinion(player);
}

// From the current game perspective, is it possible that player has the role.
// The purpose of this function is optimization only! It must only be correct
// when returning false. It relies on the game being fully claimed. The main
// purpose is to filter out options using known Evil info.
bool GameState::IsRolePossible(int player, Role role, const Time& time) const {
  if (perspective_ == OBSERVER) {
    return true;  // Observer makes no inferences for now.
  }
  Role player_role = ShownToken(player, time);
  if (player_role != ROLE_UNSPECIFIED) {
    return (player_role == role ||
            (role == DRUNK && IsTownsfolkRole(player_role)));
  }
  // From now on, this is player perspective.
  Role my_role = ShownToken(perspective_player_, time);
  if (IsGoodRole(role)) {
    if (IsEvilRole(my_role)) {
      // We can rule out known Evil players and demon bluff roles.
      return !(IsKnownEvil(player) || IsKnownDemonBluff(role));
    }
    if (role == my_role) {
      return player == perspective_player_;  // Nobody else can be my role.
    }
    return true;
  }
  if (IsGoodRole(my_role)) {
    return true;  // no inferences.
  }
  // From now on, both mine and role are Evil roles.
  if (IsMinionRole(role)) {
    return role != my_role && IsKnownStartingMinion(player);
  }
  // From now on, we try to rule out a possible demon.
  if (time == Time::Night(1)) {
    return role != my_role && IsKnownStartingDemon(player);
  }
  const auto claims = GetRoleClaimsByNight()[player];
  const bool claim_recluse_starpass =
      claims.front() == RECLUSE && claims.back() == IMP;
  const bool known_minion = IsKnownStartingMinion(player);
  const bool possible_starpass = known_minion || claim_recluse_starpass;
  if (IsMinionRole(my_role)) {
    // They could be the starting demon, or the starting demon might be dead
    // and they caught a starpass, being either a starting minion or a Recluse,
    // or they caught a SW proc.
    if (IsKnownStartingDemon(player) || minion_info_.demon == kNoPlayer) {
      return true;
    }
    const Time tod = TimeOfDeath(minion_info_.demon);
    const bool sw_valid = tod.Initialized() && tod < time && NumAlive(tod) >= 5;
    return ((sw_valid && IsRolePossible(player, SCARLET_WOMAN, tod)) ||
            (DiedAtNight(minion_info_.demon, time) && possible_starpass));
  }
  // They were a demon (at some point) and died, and I caught a starpass (or SW
  // proc), or I'm a dead Imp and they caught the starpass.
  const Time tod = TimeOfDeath(player);
  const bool starting_sw =
      ShownToken(perspective_player_, Time::Night(1)) == SCARLET_WOMAN;
  const bool sw_valid = tod.Initialized() && tod < time && NumAlive(tod) >= 5;
  return ((DiedAtNight(perspective_player_, time) && possible_starpass) ||
          (sw_valid && starting_sw) ||
          (DiedAtNight(player, time) && IsRolePossible(player, IMP, time - 1)));
}
}  // namespace botc
