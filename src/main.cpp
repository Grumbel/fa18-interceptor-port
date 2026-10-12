#include <SDL.h>
#include <iostream>
#include <string>
#include <sstream>
#include "amiga_hunk.hpp"
#include "game_state.hpp"
#include "ilbm.hpp"
#include "display.hpp"
#include "draw.hpp"
#include "gfx_state.hpp"
#include "input_state.hpp"
#include "transform.hpp"
#include "flight.hpp"
#include "audio.hpp"
#include "mesh.hpp"

int main(int argc, char* argv[]) {
  std::cout << "FA-18 Interceptor - modern port (skeleton)\n"
            << "Keys: SPACE/ENTER = next, BACKSPACE = prev, ESC = quit\n"
            << "Flight: arrows/WASD hold = pitch/yaw (release centres)\n\n";

  std::string exe_path = "assets/F-18_Interceptor_amiga";
  if (argc > 1) exe_path = argv[1];

  AmigaExecutable exe;
  if (!exe.load(exe_path)) {
    std::cerr << "Failed to load " << exe_path << "\n";
    return 1;
  }
  exe.dump_summary();
  std::cout << "\n";

  Game game;
  fa18::GfxState gfx;
  gfx.view_z = 0;  // camera; object pos.z supplies depth
  fa18::InputState input;
  fa18::AudioSystem audio;  // mirrors $00C07478 channel table

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
    return 1;
  }

  SDL_Window* win = SDL_CreateWindow(
      "FA-18 Interceptor (port) - Startup",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      fa18::WINDOW_W, fa18::WINDOW_H, SDL_WINDOW_SHOWN);
  if (!win) {
    std::cerr << "SDL_CreateWindow failed\n";
    SDL_Quit();
    return 1;
  }
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
  if (!ren) {
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 1;
  }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

  IlbmImage frnt5, inst5;
  SDL_Texture* tex_frnt = nullptr;
  SDL_Texture* tex_inst = nullptr;
  if (frnt5.load("assets/pix/frnt5")) {
    tex_frnt = frnt5.to_texture(ren);
    std::cout << "[ilbm] frnt5 " << frnt5.width << "x" << frnt5.height
              << " planes=" << frnt5.planes
              << " colors=" << frnt5.palette.size() << "\n";
  } else {
    std::cerr << "[ilbm] failed to load frnt5\n";
  }
  if (inst5.load("assets/pix/inst5")) {
    tex_inst = inst5.to_texture(ren);
    std::cout << "[ilbm] inst5 " << inst5.width << "x" << inst5.height
              << " planes=" << inst5.planes
              << " colors=" << inst5.palette.size() << "\n";
  } else {
    std::cerr << "[ilbm] failed to load inst5\n";
  }

  auto colour_for = [](GameState s) -> SDL_Color {
    switch (s) {
      case GameState::Startup:       return {0x00, 0x20, 0x40, 255};
      case GameState::Title:          return {0x00, 0x55, 0xaa, 255};
      case GameState::Enlistment:     return {0x20, 0x60, 0x20, 255};
      case GameState::Qualification:  return {0x60, 0x40, 0x00, 255};
      case GameState::MissionSelect:  return {0x40, 0x00, 0x60, 255};
      case GameState::Flight:         return {0x00, 0x30, 0x60, 255};
      case GameState::FlightLog:      return {0x40, 0x40, 0x40, 255};
      case GameState::Exit:           return {0x20, 0x00, 0x00, 255};
    }
    return {0, 0, 0, 255};
  };

  auto update_title = [&]() {
    std::ostringstream oss;
    oss << "FA-18 Interceptor (port) - " << game.state_name();
    SDL_SetWindowTitle(win, oss.str().c_str());
  };
  update_title();

  bool running = true;
  while (running) {
    input.key_code = 0;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = false;
      if (e.type == SDL_KEYDOWN) {
        // Map to $155-style key code (ASCII where applicable)
        if (e.key.keysym.sym < 128)
          input.key_code = static_cast<uint8_t>(e.key.keysym.sym);
        else if (e.key.keysym.sym == SDLK_ESCAPE)
          input.key_code = 27;
        else if (e.key.keysym.sym == SDLK_BACKSPACE)
          input.key_code = 8;
        input.input_enabled = true;
        // Mode 2 ≈ $64c title/proceed path
        input.input_mode = 2;
      }
    }

    // Flight axes from held keys (0–255, centre 128). Release → centre.
    // Stand-in for JOY0DAT path; continuous hold matches stick better than steps.
    if (game.state() == GameState::Flight ||
        game.state() == GameState::Qualification) {
      const Uint8* kb = SDL_GetKeyboardState(nullptr);
      int pitch = 128, yaw = 128;
      if (kb[SDL_SCANCODE_UP] || kb[SDL_SCANCODE_W])    pitch -= 80;
      if (kb[SDL_SCANCODE_DOWN] || kb[SDL_SCANCODE_S])  pitch += 80;
      if (kb[SDL_SCANCODE_LEFT] || kb[SDL_SCANCODE_A])  yaw -= 80;
      if (kb[SDL_SCANCODE_RIGHT] || kb[SDL_SCANCODE_D]) yaw += 80;
      if (pitch < 0) pitch = 0;
      if (pitch > 255) pitch = 255;
      if (yaw < 0) yaw = 0;
      if (yaw > 255) yaw = 255;
      input.axis_0 = static_cast<uint8_t>(pitch);
      input.axis_1 = static_cast<uint8_t>(yaw);
    }

    // Drive transitions from recovered keyboard path ($155 / $206 / $207)
    GameState prev = game.state();
    game.update(input);
    if (game.state() != prev) {
      gfx.phase = static_cast<uint16_t>(game.state());
      update_title();
      // Action-table style sound trigger ($00C16684 queue path).
      // No PCM yet — logs the channel enable / INTREQ sequence.
      if (game.state() == GameState::Flight)
        audio.enable(0);
      else if (prev == GameState::Flight)
        audio.stop(0);
      else
        audio.request(static_cast<int>(game.state()) & 3);
    }
    if (game.state() == GameState::Exit) running = false;

    if (game.state() != GameState::Flight &&
        game.state() != GameState::Qualification) {
      SDL_Color c = colour_for(game.state());
      SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 255);
      SDL_RenderClear(ren);
    }

    if (game.state() == GameState::Title && tex_frnt) {
      int w = frnt5.width * 2, h = frnt5.height * 2;
      SDL_Rect dst = { (fa18::WINDOW_W-w)/2, (fa18::WINDOW_H-h)/2, w, h };
      SDL_RenderCopy(ren, tex_frnt, nullptr, &dst);
    } else if (game.state() == GameState::Flight ||
               game.state() == GameState::Qualification) {
      // Attitude: axes → rate deltas → filter → integrate (docs/transform_3d.md)
      static fa18::PlayerAttitude att;
      int16_t deltas[3] = {
        static_cast<int16_t>((int(input.axis_0) - 128) * 4),  // pitch rate
        static_cast<int16_t>((int(input.axis_1) - 128) * 4),  // yaw rate
        0
      };
      fa18::filter_rates(att, deltas);
      fa18::integrate_attitude(att);
      fa18::auto_center_rates(att, 32);

      // Sky / ground fill from pitch (7200-circle; ~20 units/degree)
      // horizon_y shifts with pitch so the band moves on the screen.
      {
        int horizon_y = fa18::PLAYFIELD_CY + att.angle[1] / 20;
        if (horizon_y < 0) horizon_y = 0;
        if (horizon_y > fa18::PLAYFIELD_H) horizon_y = fa18::PLAYFIELD_H;
        // COLOR06 ≈ sky blue-ish, COLOR09 ≈ ground brown in recovered palette
        uint32_t sky = fa18::amiga_to_rgb24(fa18::kPaletteAmiga[6]);
        uint32_t gnd = fa18::amiga_to_rgb24(fa18::kPaletteAmiga[9]);
        SDL_SetRenderDrawColor(ren,
            (sky >> 16) & 0xFF, (sky >> 8) & 0xFF, sky & 0xFF, 255);
        SDL_Rect sky_rc{0, 0, fa18::WINDOW_W, horizon_y * fa18::SCALE};
        SDL_RenderFillRect(ren, &sky_rc);
        SDL_SetRenderDrawColor(ren,
            (gnd >> 16) & 0xFF, (gnd >> 8) & 0xFF, gnd & 0xFF, 255);
        SDL_Rect gnd_rc{0, horizon_y * fa18::SCALE,
                        fa18::WINDOW_W,
                        (fa18::PLAYFIELD_H - horizon_y) * fa18::SCALE};
        SDL_RenderFillRect(ren, &gnd_rc);
      }

      fa18::Mat3Fixed view =
          fa18::mat_from_angles_q14(att.angle[0], att.angle[1], att.angle[2]);

      auto draw_world_line = [&](fa18::Vec3 a, fa18::Vec3 b, int col) {
        auto ta = fa18::transform_point_fp(view,
            static_cast<int16_t>(a.x * 64),
            static_cast<int16_t>(a.y * 64),
            static_cast<int16_t>(a.z * 64));
        auto tb = fa18::transform_point_fp(view,
            static_cast<int16_t>(b.x * 64),
            static_cast<int16_t>(b.y * 64),
            static_cast<int16_t>(b.z * 64));
        ta.z += 4.f; tb.z += 4.f;
        if (ta.z < 0.2f && tb.z < 0.2f) return;
        int sx0, sy0, sx1, sy1;
        fa18::project(ta, sx0, sy0);
        fa18::project(tb, sx1, sy1);
        fa18::draw_line_pal(ren, sx0, sy0, sx1, sy1, col);
      };

      // Horizon line — COLOR05
      draw_world_line({-20.f, 0.f, 8.f}, {20.f, 0.f, 8.f}, 5);

      // Runway: edges + markers + centerline
      const float half_w = 1.2f;
      for (int i = 0; i < 10; ++i) {
        float z0 = 2.f + i * 3.f;
        float z1 = z0 + 3.f;
        draw_world_line({-half_w, 1.5f, z0}, {-half_w, 1.5f, z1}, 11);
        draw_world_line({ half_w, 1.5f, z0}, { half_w, 1.5f, z1}, 11);
        draw_world_line({-half_w, 1.5f, z0}, { half_w, 1.5f, z0}, 11);
      }
      draw_world_line({0.f, 1.5f, 2.f}, {0.f, 1.5f, 32.f}, 13);

      // Real mesh data from dump cluster $00C39700–$00C3A600
      static fa18::Mesh craft  = fa18::mesh_aircraft_39738();
      static fa18::Mesh craft2 = fa18::mesh_aircraft_39800();
      static fa18::Mesh craft3 = fa18::mesh_aircraft_398C6();
      static fa18::Mesh craft4 = fa18::mesh_aircraft_39980();
      static fa18::Mesh craftB = fa18::mesh_from_dump_B();
      static fa18::Mesh craftE = fa18::mesh_aircraft_39E00();
      static bool mesh_logged = false;
      if (!mesh_logged) {
        auto logm = [](const fa18::Mesh& m) {
          std::cout << "[mesh] " << m.name
                    << " verts=" << m.verts.size()
                    << " edges=" << m.edges.size()
                    << " dl=" << m.display_list.size() << "\n";
        };
        logm(craft); logm(craft2); logm(craft3); logm(craft4);
        logm(craftB); logm(craftE);
        mesh_logged = true;
      }
      fa18::Mat3Fixed orient =
          fa18::mat_from_angles_q14(0, att.angle[1] / 3, att.angle[2] / 2);
      // Player craft ahead; companions off to the sides / above
      fa18::draw_mesh(ren, gfx, craft,  orient, { 0.f,  0.f, 10.f}, 13);
      fa18::draw_mesh(ren, gfx, craft2, orient, { 5.f, -0.5f, 16.f},  9);
      fa18::draw_mesh(ren, gfx, craft3, orient, {-4.f,  0.f, 14.f}, 11);
      fa18::draw_mesh(ren, gfx, craft4, orient, { 3.f,  0.5f, 20.f}, 10);
      fa18::draw_mesh(ren, gfx, craftB, orient, { 2.f,  1.5f, 12.f},  7);
      fa18::draw_mesh(ren, gfx, craftE, orient, {-2.f, -1.0f, 18.f},  5);

      // 2-D hull radar silhouette (top-left, from mesh.hull)
      fa18::draw_hull_2d(ren, gfx, craft, 40, 40, 0.04f, 13);
      fa18::draw_hull_2d(ren, gfx, craft2, 90, 40, 0.04f, 9);

      // HUD crosshair — COLOR13
      fa18::draw_line_pal(ren, fa18::PLAYFIELD_CX-10, fa18::PLAYFIELD_CY,
                          fa18::PLAYFIELD_CX+10, fa18::PLAYFIELD_CY, 13);
      fa18::draw_line_pal(ren, fa18::PLAYFIELD_CX, fa18::PLAYFIELD_CY-10,
                          fa18::PLAYFIELD_CX, fa18::PLAYFIELD_CY+10, 13);

      // Instrument panel (inst5) — full width, native aspect ×2
      if (tex_inst) {
        int w = inst5.width * 2, h = inst5.height * 2;
        SDL_Rect dst = { (fa18::WINDOW_W - w) / 2, fa18::WINDOW_H - h, w, h };
        SDL_RenderCopy(ren, tex_inst, nullptr, &dst);
      }
    }

    SDL_RenderPresent(ren);
    SDL_Delay(16);
  }

  if (tex_frnt) SDL_DestroyTexture(tex_frnt);
  if (tex_inst) SDL_DestroyTexture(tex_inst);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
