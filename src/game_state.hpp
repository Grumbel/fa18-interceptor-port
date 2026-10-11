#pragma once
#include <string>
#include <vector>
#include "input_state.hpp"

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
  void update(fa18::InputState& input);
  GameState state() const { return state_; }
  const char* state_name() const;
  void request_next();
  void request_prev();
  const std::vector<MissionInfo>& missions() const { return missions_; }

private:
  GameState state_ = GameState::Startup;
  std::vector<MissionInfo> missions_;
  void enter(GameState s);

  // Mirrors the mode dispatch at file 0x15f0:
  //   input_mode 1 → $5b6 (service), 2 → $64c (title/proceed)
  void handle_input(fa18::InputState& input);
};
