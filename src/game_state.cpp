#include "game_state.hpp"
#include <iostream>

Game::Game() {
  // Missions recovered from strings (F-keys mapping is approximate)
  missions_ = {
    {1, "Visual Confirmation", "F1 - VISUAL CONFIRMATION MISSION"},
    {3, "Intercept Stolen Aircraft", "F3 - INTERCEPT STOLEN AIRCRAFT"},
    {5, "Intercept Cruise Missile", "F5 - INTERCEPT INCOMING CRUISE MISSILE"},
    {6, "Carrier Sub", "F6 - CARRIER SUB MISSION"},
  };
  enter(GameState::Startup);
}

void Game::enter(GameState s) {
  state_ = s;
  switch (s) {
    case GameState::Startup:
      std::cout << "[state] Startup – OpenLibrary / init\n";
      break;
    case GameState::Title:
      std::cout << "[state] Title / splash\n";
      break;
    case GameState::Enlistment:
      std::cout << "[state] YOU ARE NOW OFFICIALLY ENLISTED\n";
      break;
    case GameState::Qualification:
      std::cout << "[state] Qualification – basic maneuvers / carrier landing\n";
      break;
    case GameState::MissionSelect:
      std::cout << "[state] Selectable missions:\n";
      for (const auto& m : missions_)
        std::cout << "  " << m.description << "\n";
      break;
    case GameState::Flight:
      std::cout << "[state] Flight\n";
      break;
    case GameState::FlightLog:
      std::cout << "[state] Flight log / statistics\n";
      break;
    case GameState::Exit:
      std::cout << "[state] Exit\n";
      break;
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

void Game::update() {
  // Placeholder for per-frame logic
}
