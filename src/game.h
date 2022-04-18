#ifndef GAME_H_
#define GAME_H_

#include <string>
#include <vector>

#include "ortools/constraint_solver/constraint_solver.h"
#include "src/game_log.pb.h"

namespace botc {

using operations_research::Constraint;
using operations_research::IntVar;
using operations_research::Solver;
using std::string;
using std::vector;

// This contains an instance of a BOTC game on a particular time.
class GameState {
public:
    GameState(Perspective perspective, const Setup& setup);
    explicit GameState(const GameLog& game_log);

    void AddEvent(const Event& event);

    GameLog ToProto() const;
private:
    void InitRoleVarsConstraints();

    Perspective perspective_;
    vector<string> players_;
    int num_players_, num_outsiders_, num_townsfolk_, num_minions_;
    vector<Event> events_;
    vector<vector<Constraint*>> constraints_;
    Solver solver_;
    vector<vector<IntVar*>> player_role_vars_;
};

}  // namespace botc

#endif  // GAME_H_