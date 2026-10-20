// The polygon-op handlers of ops_body.inc compiled against the mesh viewer's
// offline register file (mesh_emu.hpp) instead of the live machine.
#include "native/mesh_emu.hpp"

#include <cstdlib>

namespace mesh_viewer::emu {

namespace {
Env* g_env = nullptr;
}  // namespace

Env& env() {
  if (!g_env) throw DecodeError("no model environment");
  return *g_env;
}
void set_env(Env* e) { g_env = e; }

using namespace asmr;

namespace {
#define abort() throw DecodeError("abort")
#include "native/ops_body.inc"
#undef abort

struct OpEntry {
  uint32_t addr;
  OpFn fn;
};
// Same addresses as the NATIVE_ASM registrations in ops.cpp.
const OpEntry kOps[] = {
    {0x32ffa, op_32ffa},   {0x33052, op_33052},   {0x3309e, op_3309e},
    {0x335b0, op_335b0},   {0x335b4, op_335b4},   {0x335d6, op_335d6},   {0x335f8, op_335f8},
    {0x33664, op_33664},   {0x33674, op_33674},   {0x33682, op_33682},   {0x3369a, op_3369a},
    {0x336d6, op_336d6},   {0x3372c, op_3372c},   {0x33734, op_33734},   {0x33744, op_33744},
    {0x33792, op_33792},   {0x337ec, op_337ec},   {0x33890, op_33890},   {0x33936, op_33936},
    {0x33b5c, op_33b5c},   {0x33b60, op_33b60},   {0x33d22, op_33d22},   {0x33de6, op_33de6},
    {0x33e74, op_33e74},   {0x33f8e, op_33f8e},   {0x33fb6, op_33fb6},   {0x33fba, op_33fba},
    {0x34094, op_34094},   {0x341d0, op_341d0},   {0x341e2, op_341e2},   {0x343b2, op_343b2},
    {0x343c8, op_343c8},   {0x344f8, op_344f8},   {0x345d0, op_345d0},   {0x345de, op_345de},
    {0x34654, op_34654},   {0x346a0, op_346a0},   {0x34708, op_34708},   {0x34754, op_34754},
    {0x347ec, op_347ec},   {0x34872, op_34872},   {0x34968, op_34968},   {0x349b6, op_349b6},
    {0x34a28, op_34a28},   {0x34a3c, op_34a3c},   {0x34aa8, op_34aa8},   {0x34b2a, op_34b2a},
    {0x34b9e, op_34b9e},   {0x34c1c, op_34c1c},   {0x34c8c, op_34c8c},   {0x34d2a, op_34d2a},
    {0x34e16, op_34e16},   {0x34f76, op_34f76},   {0x35054, op_35054},   {0x350a2, op_350a2},
    {0x350cc, op_350cc},   {0x350ec, op_350ec},   {0x350f2, op_350f2},   {0x35118, op_35118},
    {0x3513a, op_3513a},   {0x351ac, op_351ac},   {0x352c4, op_352c4},   {0x353ba, op_353ba},
    {0x35412, op_35412},   {0x35594, op_35594},   {0x35684, op_35684},   {0x35730, op_35730},
    {0x357d0, op_357d0},   {0x357ee, op_357ee},   {0x35b6c, op_35b6c},   {0x35d5c, op_35d5c},
    {0x35e54, op_35e54},   {0x35efc, op_35efc},   {0x35f44, op_35f44},   {0x35f48, op_35f48},
    {0x35884, op_35884},   {0x35832, sub_35832},  {0x352e2, sub_352e2},  {0x353d8, sub_353d8},
    {0x35462, sub_35462},  {0x3574c, sub_3574c},  {0x35bbc, sub_35bbc},  {0x35d78, sub_35d78},
    {0x35e70, sub_35e70},  {0x35f18, sub_35f18},
};
}  // namespace

OpFn op_at(uint32_t addr) {
  for (const auto& e : kOps)
    if (e.addr == addr) return e.fn;
  return nullptr;
}

}  // namespace mesh_viewer::emu
