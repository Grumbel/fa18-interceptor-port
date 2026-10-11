#include "game_state.hpp"
#include <iostream>

Game::Game() {
  missions_ = {
    {1, "Visual Confirmation", "F1 - VISUAL CONFIRMATION MISSION"},
    {3, "Intercept Stolen Aircraft", "F3 - INTERCEPT STOLEN AIRCRAFT"},
    {5, "Intercept Cruise Missile", "F5 - INTERCEPT INCOMING CRUISE MISSILE"},
    {6, "Carrier Sub", "F6 - CARRIER SUB MISSION"},
  };
  enter(GameState::Startup);
}

const char* Game::state_name() const {
  switch (state_) {
    case GameState::Startup:       return "Startup";
    case GameState::Title:          return "Title";
    case GameState::Enlistment:     return "Enlistment";
    case GameState::Qualification:  return "Qualification";
    case GameState::MissionSelect:  return "Mission Select";
    case GameState::Flight:         return "Flight";
    case GameState::FlightLog:      return "Flight Log";
    case GameState::Exit:           return "Exit";
  }
  return "?";
}

void Game::enter(GameState s) {
  state_ = s;
  std::cout << "[state] " << state_name() << "\n";
  if (s == GameState::MissionSelect) {
    for (const auto& m : missions_)
      std::cout << "  " << m.description << "\n";
  }
}

void Game::request_next() {
  switch (state_) {
    case GameState::Startup:       enter(GameState::Title); break;
    case GameState::Title:          enter(GameState::Enlistment); break;
    case GameState::Enlistment:     enter(GameState::Qualification); break;
    case GameState::Qualification:  enter(GameState::MissionSelect); break;
    case GameState::MissionSelect:  enter(GameState::Flight); break;
    case GameState::Flight:         enter(GameState::FlightLog); break;
    case GameState::FlightLog:      enter(GameState::Exit); break;
    default: break;
  }
}

void Game::request_prev() {
  switch (state_) {
    case GameState::Title:          enter(GameState::Startup); break;
    case GameState::Enlistment:     enter(GameState::Title); break;
    case GameState::Qualification:  enter(GameState::Enlistment); break;
    case GameState::MissionSelect:  enter(GameState::Qualification); break;
    case GameState::Flight:         enter(GameState::MissionSelect); break;
    case GameState::FlightLog:      enter(GameState::Flight); break;
    case GameState::Exit:           enter(GameState::FlightLog); break;
    default: break;
  }
}

// Mirrors the keyboard dispatch recovered at file 0x1590 / 0x15f0:
//   key available ($155 ≠ 0) && input enabled ($206 ≠ 0)
//     → mode from $207 selects handler
// For the port we map SPACE/ENTER (any non-zero key while enabled)
// to the title-proceed path that advances state.
void Game::handle_input(fa18::InputState& input) {
  if (input.key_code == 0 || !input.input_enabled)
    return;

  // Mode 2 mirrors $64c (title/proceed → action 35/36 path)
  // Mode 0/1 are service ticks; we treat any key as proceed for now.
  if (input.input_mode <= 2) {
    // Consume key
    uint8_t key = input.key_code;
    input.key_code = 0;

    if (key == 8 || key == 127) {          // BACKSPACE
      request_prev();
    } else if (key == 27) {                // ESC
      enter(GameState::Exit);
    } else {
      // SPACE, ENTER, or any other key → proceed (like $64c path)
      request_next();
    }
  }
}

void Game::update(fa18::InputState& input) {
  handle_input(input);
}
