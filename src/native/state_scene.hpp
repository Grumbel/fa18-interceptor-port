// State-driven scene math: model-space vertices -> game view space, computed
// in floating point from the high-level game state (no rasteriser capture).
// Formula validated against captured prims, see docs/state_renderer.md.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "native/game_state.hpp"
#include "native/mesh_viewer.hpp"
#include "native/world.hpp"

namespace state_scene {

struct Vec3f {
    float x = 0, y = 0, z = 0;
};

// Model units per camera-relative "eye" unit (1 eye unit = 256 world units).
// Validated for the type 86 tower; other models may differ.
constexpr float kDefaultModelScale = 8.0f;

// View-space position of the model-space vertex `v` of an axis-aligned object.
//   obj_pos / cam_pos : world positions (long, world units)
//   view_m            : $5aed0 world->view matrix, row-major Q8
Vec3f model_to_view(const std::array<int16_t, 9>& view_m, const int32_t obj_pos[3],
                    const int32_t cam_pos[3], const int32_t v[3],
                    float model_scale = kDefaultModelScale);

// Frustum culling of generated primitives (on by default; off for diffing).
void set_frustum_cull(bool on);

// A view-space primitive generated from state (floats, game view units).
struct ViewPrim {
    mesh_viewer::Prim::Kind kind = mesh_viewer::Prim::Poly;
    uint8_t colour = 0, mask = 15, xr = 0;
    int type = 0;                 // object type index
    std::vector<Vec3f> v;
};

// Same, for an object with an attitude: `body_m` is the scenario record's
// +146 basis (Q14, row-major).  The view output is in world-unit based
// coordinates: only ratios matter for the projection, so it differs from the
// game's per-object vertex-stage unit by a constant factor.
Vec3f model_to_view(const std::array<int16_t, 9>& view_m, const std::array<int16_t, 9>& body_m,
                    const int32_t obj_pos[3], const int32_t cam_pos[3], const int32_t v[3],
                    int scale_shift);

// Scenario objects (aircraft, ships, missiles) of the pool, rotated by their
// body matrix.  The player's own aircraft is skipped unless the camera is external.
std::vector<ViewPrim> build_scenarios(const gamestate::GameState& gs,
                                      const std::vector<mesh_viewer::Model>& models);

// Static ground objects of the near map within `radius_cells` of the camera,
// transformed to view space (axis-aligned, nearest LOD per type).  Objects
// behind the camera (vz <= 0 for every vertex) are dropped.
std::vector<ViewPrim> build_static(const gamestate::GameState& gs, const world::World& w,
                                   const std::vector<mesh_viewer::Model>& models,
                                   int radius_cells = 1);

}  // namespace state_scene
