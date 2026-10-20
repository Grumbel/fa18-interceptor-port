// Polygon-op handlers of the 3D renderer: the routines behind the jump
// table at $33478 ($32ffa-$35f48).  They run inside the `link a6,#-152`
// frame of the object draw routines (render.cpp): a2 is the op stream, a6
// the frame, and results come back in d0 (0 = continue, 1/-1 = stop).
// Register-convention assembly on the live register file.
#include "native/asmregs.hpp"
#include "native/orig.hpp"
#include "native/vars.hpp"

namespace native {
using namespace asmr;

namespace {

#include "native/ops_body.inc"

}  // namespace

NATIVE_ASM(0x32ffa, op_32ffa, true);
NATIVE_ASM(0x33052, op_33052, true);
NATIVE_ASM(0x3309e, op_3309e, true);
NATIVE_ASM(0x335b0, op_335b0, true);
NATIVE_ASM(0x335b4, op_335b4, true);
NATIVE_ASM(0x335d6, op_335d6, true);
NATIVE_ASM(0x335f8, op_335f8, true);
NATIVE_ASM(0x33664, op_33664, true);
NATIVE_ASM(0x33674, op_33674, true);
NATIVE_ASM(0x33682, op_33682, true);
NATIVE_ASM(0x3369a, op_3369a, true);
NATIVE_ASM(0x336d6, op_336d6, true);
NATIVE_ASM(0x3372c, op_3372c, true);
NATIVE_ASM(0x33734, op_33734, true);
NATIVE_ASM(0x33744, op_33744, true);
NATIVE_ASM(0x33792, op_33792, true);
NATIVE_ASM(0x337ec, op_337ec, true);
NATIVE_ASM(0x33890, op_33890, true);
NATIVE_ASM(0x33936, op_33936, true);
NATIVE_ASM(0x33b5c, op_33b5c, true);
NATIVE_ASM(0x33b60, op_33b60, true);
NATIVE_ASM(0x33d22, op_33d22, true);
NATIVE_ASM(0x33de6, op_33de6, true);
NATIVE_ASM(0x33e74, op_33e74, true);
NATIVE_ASM(0x33f8e, op_33f8e, true);
NATIVE_ASM(0x33fb6, op_33fb6, true);
NATIVE_ASM(0x33fba, op_33fba, true);
NATIVE_ASM(0x34094, op_34094, true);
NATIVE_ASM(0x341d0, op_341d0, true);
NATIVE_ASM(0x341e2, op_341e2, true);
NATIVE_ASM(0x343b2, op_343b2, true);
NATIVE_ASM(0x343c8, op_343c8, true);
NATIVE_ASM(0x344f8, op_344f8, true);
NATIVE_ASM(0x345d0, op_345d0, true);
NATIVE_ASM(0x345de, op_345de, true);
NATIVE_ASM(0x34654, op_34654, true);
NATIVE_ASM(0x346a0, op_346a0, true);
NATIVE_ASM(0x34708, op_34708, true);
NATIVE_ASM(0x34754, op_34754, true);
NATIVE_ASM(0x347ec, op_347ec, true);
NATIVE_ASM(0x34872, op_34872, true);
NATIVE_ASM(0x34968, op_34968, true);
NATIVE_ASM(0x349b6, op_349b6, true);
NATIVE_ASM(0x34a28, op_34a28, true);
NATIVE_ASM(0x34a3c, op_34a3c, true);
NATIVE_ASM(0x34aa8, op_34aa8, true);
NATIVE_ASM(0x34b2a, op_34b2a, true);
NATIVE_ASM(0x34b9e, op_34b9e, true);
NATIVE_ASM(0x34c1c, op_34c1c, true);
NATIVE_ASM(0x34c8c, op_34c8c, true);
NATIVE_ASM(0x34d2a, op_34d2a, true);
NATIVE_ASM(0x34e16, op_34e16, true);
NATIVE_ASM(0x34f76, op_34f76, true);
NATIVE_ASM(0x35054, op_35054, true);
NATIVE_ASM(0x350a2, op_350a2, true);
NATIVE_ASM(0x350cc, op_350cc, true);
NATIVE_ASM(0x350ec, op_350ec, true);
NATIVE_ASM(0x350f2, op_350f2, true);
NATIVE_ASM(0x35118, op_35118, true);
NATIVE_ASM(0x3513a, op_3513a, true);
NATIVE_ASM(0x351ac, op_351ac, true);
NATIVE_ASM(0x352c4, op_352c4, true);
NATIVE_ASM(0x353ba, op_353ba, true);
NATIVE_ASM(0x35412, op_35412, true);
NATIVE_ASM(0x35594, op_35594, true);
NATIVE_ASM(0x35684, op_35684, true);
NATIVE_ASM(0x35730, op_35730, true);
NATIVE_ASM(0x357d0, op_357d0, true);
NATIVE_ASM(0x357ee, op_357ee, true);
NATIVE_ASM(0x35b6c, op_35b6c, true);
NATIVE_ASM(0x35d5c, op_35d5c, true);
NATIVE_ASM(0x35e54, op_35e54, true);
NATIVE_ASM(0x35efc, op_35efc, true);
NATIVE_ASM(0x35f44, op_35f44, true);
NATIVE_ASM(0x35f48, op_35f48, true);
NATIVE_ASM(0x35884, op_35884, true);
NATIVE_ASM(0x35832, sub_35832, true);

// Internal lattice helpers used by the ops above — register so original
// callers that jump here directly take the native path.
NATIVE_ASM(0x352e2, sub_352e2, true);
NATIVE_ASM(0x353d8, sub_353d8, true);
NATIVE_ASM(0x35462, sub_35462, true);
NATIVE_ASM(0x3574c, sub_3574c, true);
NATIVE_ASM(0x35bbc, sub_35bbc, true);
NATIVE_ASM(0x35d78, sub_35d78, true);
NATIVE_ASM(0x35e70, sub_35e70, true);
NATIVE_ASM(0x35f18, sub_35f18, true);

}  // namespace native
