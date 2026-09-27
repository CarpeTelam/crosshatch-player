#pragma once

namespace GameCore {

// Port: where a game's ch.log and print lines go. The provider in src/games
// writes each as LOG_INF tagged with the game id; host tests capture them. One
// call is one line, already bounded and free of control bytes other than tabs.
class IGameLog {
 public:
  virtual ~IGameLog() = default;
  virtual void write(const char* line) = 0;
};

}  // namespace GameCore
