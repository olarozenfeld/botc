#include "src/game.h"

#include <iostream>

using botc::GameLog;
using botc::GameState;
using std::cout;

int main(int argc, char** argv) {
  GameLog log;
  GameState g(log);
  cout << "g.ToProto()";
  return 0;
}