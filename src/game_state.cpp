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

void Game::update() {
  // Placeholder for per-frame logic
}
