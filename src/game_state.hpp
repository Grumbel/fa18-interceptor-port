#pragma once
#include <string>
#include <vector>

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
  const char* state_name() const;
  void request_next();
  void request_prev();
  const std::vector<MissionInfo>& missions() const { return missions_; }

private:
  GameState state_ = GameState::Startup;
  std::vector<MissionInfo> missions_;
  void enter(GameState s);
};
