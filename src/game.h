#ifndef GAME_H_
#define GAME_H_

#include <string>
#include <vector>

#include "src/game_log.pb.h"

namespace botc {

using std::string;
using std::vector;

// This contains an instance of a BOTC game on a particular time.
class GameState {
public:
    GameState(Perspective perspective, const Setup& setup);
    explicit GameState(const GameLog& game_log);

    void AddEvent(const Event& event);
    void RemoveLastEvent();

    GameLog ToProto() const;
private:
    Perspective perspective_;
    vector<string> players_;
};

}  // namespace botc

#endif  // GAME_H_