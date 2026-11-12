// The scene source of `--renderer new`: the original scene code, run on a
// copy of the game for every presented frame.
//
// The game draws at most 15 frames per second and its simulation advances a
// fixed step per frame, so a higher frame rate cannot come from running the
// game faster.  Instead a second ("shadow") machine takes over the game's
// memory as it was just before the view was set up ($4217c), gets the camera
// and the moving objects of a moment between the last two game frames poked
// in, and runs the game's own scene pipeline: view matrix, camera, object
// list and sort ($2f7b8), sun ($43306), ground dots ($3c1bc), effects
// ($272e4) and the object draw ($2fc14) with every model op handler.  Only
// the leaf rasterisers are replaced: they record the primitive instead of
// drawing it.  What the game would draw for that instant, in the order it
// would draw it, with its own level of detail, culling, damage states and
// effects, comes out as a display list (display_list.hpp).
//
// The game itself is not affected: the shadow never writes to it.
#pragma once

#include "amiga/machine.hpp"
#include "render/display_list.hpp"

#include <memory>
#include <string>

namespace render {

class Shadow {
 public:
  Shadow();
  ~Shadow();
  // Boot the shadow from the same disk as `game` and hook the game's frame.
  bool init(amiga::Machine& game, const std::string& disk_path, std::string& err);

  // Number of in-flight game frames seen so far (0: nothing to draw).
  uint64_t serial() const;
  // Machine frame (vertical blank count) of the newest snapshot and of the
  // one before it; the caller derives the interpolation phase from them.
  uint64_t newest_frame() const;
  uint64_t previous_frame() const;

  // Display list for a moment between the previous (alpha 0) and the newest
  // (alpha 1) game frame.  `widen` >= 1 extends the horizontal field of view
  // the original code culls against (widescreen); the 4:3 centre keeps its
  // geometry.  False if no frame is available or the shadow failed.
  bool build(float alpha, float widen, Frame& out);

  // Debug: compare the shadow's primitives at alpha 1 with what the game
  // itself drew in that frame (FA18_SHADOW_VERIFY=1).
  void set_verify(bool on);

 private:
  struct Impl;
  std::unique_ptr<Impl> p_;
};

}  // namespace render
