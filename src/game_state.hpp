#pragma once
#include <string>
#include <vector>

// High-level game states recovered from strings and early analysis.
// This is a skeleton for the eventual reimplementation.

enum class GameState {
  Startup,
  Title,
  Enlistment,
  Qualification,
  MissionSelect,
  Flight,
  FlightLog,
  Exit
};

struct MissionInfo {
  int id;
  std::string name;
  std::string description;
};

class Game {
public:
  Game();
  void update();
  GameState state() const { return state_; }

  // Placeholder – will be driven by real input / RE’d logic later
  void request_next();

private:
  GameState state_ = GameState::Startup;
  std::vector<MissionInfo> missions_;
  void enter(GameState s);
};
