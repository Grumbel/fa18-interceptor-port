// Assembly math helpers of the flight/3D code (hunk 64 area, $308cc and
// $39b06-$39f74).  All are register-convention routines: callers see the
// registers they leave behind (d0-d4 scratch values, a0/a3 pointers), so
// every routine reproduces the exact final register file; --native verify
// compares all of d0-d7/a0-a6 and the memory image against the original.
//
//   $308cc  approximate hypot(d2, d3, d4) -> d1, $5ae38 (table at $30930)
//   $39b06  scripted view-angle sequences (tables at $39ade)
//   $39bba  integer square root of $5ae5c -> $5ae60 (Newton iteration)
//   $39c70  cockpit command word -> $5add8 / $5abc4 / $5abc6 flags
//   $39cb6/$39cc0/$39d58  scale a 3-vector to a fixed length -> $5ad44..48
//   $39dd0  reset the output list pointer ($5acc2)
//   $39de2  rotate a vector by the matrix at $5aef4, append to the list
//   $39e34  step the pan/tilt angle toward the commanded direction
//   $39eec  rounded quotient $5adc4 / $5adc8 -> $5adca
//   $39f2e / $39f74  8-digit BCD <-> binary conversion ($5ae1a <-> $5ae16)
#include "native/orig.hpp"
#include "native/vars.hpp"

namespace native {
using namespace v;

namespace {

uint32_t& D(int n) { return cpu().d[n]; }
uint16_t lo(int n) { return uint16_t(cpu().d[n]); }
void setw(int n, uint32_t v) { cpu().d[n] = (cpu().d[n] & 0xFFFF0000u) | (v & 0xFFFF); }
void setb(int n, uint32_t v) { cpu().d[n] = (cpu().d[n] & 0xFFFFFF00u) | (v & 0xFF); }
int32_t ext(uint16_t v) { return int16_t(v); }

// N and Z from the last data move; V and C cleared (move/ext/tst/clr).
void flags(uint32_t v, int bits) {
  m68k::Cpu& c = cpu();
  uint32_t mask = bits == 32 ? 0xFFFFFFFFu : (1u << bits) - 1;
  c.fn = (v >> (bits - 1)) & 1;
  c.fz = (v & mask) == 0;
  c.fv = false;
  c.fc = false;
}

// 68000 shifts by a register count use count mod 64.
int32_t asr32(int32_t v, uint32_t n) {
  n &= 63;
  return n > 31 ? (v < 0 ? -1 : 0) : v >> n;
}
int32_t asl32(int32_t v, uint32_t n) {
  n &= 63;
  return n > 31 ? 0 : int32_t(uint32_t(v) << n);
}

// divu.w: overflow (quotient > $ffff) leaves the destination unchanged.
void divuw(uint32_t& dn, uint16_t divisor) {
  if (divisor == 0) {
    mach().log("native math: division by zero at %06x\n", cpu().op_pc);
    abort();
  }
  uint32_t q = dn / divisor;
  if (q > 0xFFFF) return;
  dn = ((dn % divisor) << 16) | q;
}

// ---- $308cc ---------------------------------------------------------------
// In: d2, d3, d4 (non-negative words).  Out: d1 = ~sqrt-like combination,
// $5ae38 = min(d1, 32767).  Uses the 16-bit table at $30930 twice.
void hypot_308cc() {
  constexpr uint32_t table = 0x30930;
  if (ext(lo(3)) > ext(lo(2))) std::swap(D(2), D(3));  // cmpw d2,d3; ble
  D(3) = uint32_t(ext(lo(3)));
  if (D(3) != 0) {
    D(3) <<= 8;
    if (lo(2) == 0) {
      D(3) = 0;
    } else {
      divuw(D(3), lo(2));
      setw(3, lo(3) + lo(3));
    }
  }
  setw(3, rd16(table + uint32_t(ext(lo(3)))));
  D(2) = uint32_t(lo(3)) * lo(2);                         // muluw
  D(4) = uint32_t(ext(lo(4)));
  D(4) = uint32_t(asl32(int32_t(D(4)), 14));
  if (int32_t(D(4)) > int32_t(D(2))) std::swap(D(2), D(4));
  D(2) = uint32_t(asr32(int32_t(D(2)), 14));
  if (lo(2) == 0) {
    D(4) = 0;
  } else {
    divuw(D(4), lo(2));
    setw(4, uint32_t(int16_t(lo(4)) >> 6));
    setw(4, lo(4) + lo(4));
  }
  setw(1, rd16(table + uint32_t(ext(lo(4)))));
  D(1) = uint32_t(lo(1)) * lo(2);
  D(1) = uint32_t(asr32(int32_t(D(1)), 14));
  if (int32_t(D(1)) > 32767) setw(1, 32767);
  wr16(0x5ae38, lo(1));
  flags(lo(1), 16);
}
NATIVE_ASM(0x308cc, hypot_308cc, true);

// ---- $39bba ---------------------------------------------------------------
// Newton iteration for sqrt(d0), start value d0/200+2, stops when the
// quotient is within 1 of the divisor.  Large inputs are pre-shifted by 4 or
// 8 and the root scaled back.
uint16_t newton(uint32_t d0) {
  uint32_t d1, d2 = d0, d3;
  divuw(d2, 200);
  d2 = (d2 & 0xFFFF0000u) | uint16_t(uint16_t(d2) + 2);
  for (;;) {
    d1 = d0;
    divuw(d1, uint16_t(d2));
    d3 = uint16_t(d1) - uint16_t(d2);
    uint16_t t = uint16_t(d3);
    if (t == 0 || t == 1 || t == 0xFFFF) break;
    uint16_t sum = uint16_t(uint16_t(d1) + uint16_t(d2));
    d2 = (d2 & 0xFFFF0000u) | (sum >> 1);
  }
  return uint16_t(d1);
}
void isqrt_39bba() {
  uint32_t d0 = rd32(0x5ae5c);
  uint16_t r;
  if (int32_t(d0) <= 6549504) r = newton(d0);
  else if (int32_t(d0) <= 104792064) r = uint16_t(newton(d0 >> 4) << 2);
  else r = uint16_t(newton(d0 >> 8) << 4);
  wr16(0x5ae60, r);
  flags(r, 16);
}
NATIVE_ASM(0x39bba, isqrt_39bba, true);

// ---- $39eec ---------------------------------------------------------------
// $5adca = round-to-nearest($5adc4 / $5adc8) (signed 32/16 division).
void rounded_div_39eec() {
  uint32_t d0 = rd32(0x5adc4);
  int16_t d1 = int16_t(rd16(0x5adc8));
  int16_t d2 = d1 < 0 ? int16_t(-d1) : d1;
  d2 = int16_t(d2 >> 1);
  if (d1 == 0) {
    mach().log("native math: divs by zero at %06x\n", cpu().op_pc);
    abort();
  }
  int64_t q = int64_t(int32_t(d0)) / d1, r = int64_t(int32_t(d0)) % d1;
  if (q >= -32768 && q <= 32767) d0 = (uint32_t(uint16_t(r)) << 16) | uint16_t(q);
  d0 = (d0 << 16) | (d0 >> 16);                       // swap
  int16_t rem = int16_t(d0);
  if (rem < 0) rem = int16_t(-rem);
  d0 = (d0 & 0xFFFF0000u) | uint16_t(rem);
  uint16_t res;
  d0 = (d0 << 16) | (d0 >> 16);                       // swap
  if (d2 > rem) {
    res = uint16_t(d0);
  } else {
    int16_t q16 = int16_t(d0);
    res = uint16_t(q16 >= 0 ? q16 + 1 : q16 - 1);
  }
  wr16(0x5adca, res);
  flags(res, 16);
}
NATIVE_ASM(0x39eec, rounded_div_39eec, true);

// ---- $39f2e / $39f74 --------------------------------------------------------
void bcd_to_bin_39f2e() {
  constexpr uint32_t pow10 = 0x39faa;                 // 10^7 .. 10^0 (longs)
  uint32_t sum = 0;
  for (int i = 0; i < 8; ++i) {
    uint8_t b = rd8(0x5ae1a + uint32_t(i / 2));
    uint32_t digit = (i & 1) ? (b & 15) : ((b >> 4) & 15);
    sum += digit * rd32(pow10 + 4u * uint32_t(i));
  }
  wr32(0x5ae16, sum);
  flags(sum, 32);
}
NATIVE_ASM(0x39f2e, bcd_to_bin_39f2e, true);

void bin_to_bcd_39f74() {
  constexpr uint32_t pow10 = 0x39faa;
  uint32_t d5 = rd32(0x5ae16), d7 = 0, d6 = 0x10000000u, p = pow10;
  for (;;) {
    uint32_t t = rd32(p);
    if (d5 >= t) {                                    // cmpl; bcs
      d5 -= t;
      d7 += d6;
      continue;
    }
    p += 4;
    d6 >>= 4;
    if (d6 == 0) break;
  }
  wr32(0x5ae1a, d7);
  flags(d7, 32);
}
NATIVE_ASM(0x39f74, bin_to_bcd_39f74, true);

// ---- $39c70 ---------------------------------------------------------------
void command_flags_39c70() {
  uint32_t d1 = D(1);                                 // saved/restored
  wr16(0x5add8, lo(0));
  wr16(0x5abc4, rd16(0x5abc4) | 1);
  setw(0, lo(0) & 0xFF00);
  if (lo(0) & 0x2000) {
    wr16(0x5abc6, (rd16(0x5abc6) & 0x7FFF) | 0x2000);
  } else if (lo(0) == 0x4000 || lo(0) == 0x4800) {
    wr16(0x5abc6, rd16(0x5abc6) & 0xDFFF);
  }
  D(1) = d1;
  flags(lo(1), 16);
}
NATIVE_ASM(0x39c70, command_flags_39c70, true);

// ---- $39dd0 ---------------------------------------------------------------
void reset_list_39dd0() {
  wr16(0x5b47a, 0);
  wr32(0x5acc2, 406960);                              // $63430
  flags(406960, 32);
}
NATIVE_ASM(0x39dd0, reset_list_39dd0, true);

// ---- $39cb6 / $39cc0 / $39d58 -----------------------------------------------
// Scale the vector (d5, d6, d7) to a length given by d0 (abs): the three
// routines share the tail; they differ in the shift base (8 / 3), the entry
// (registers vs. stack arguments) and in how d0 == 0 / d1 == 0 is handled.
void zero_vector_tail() {
  setw(5, 0);
  setw(6, 0);
  setw(7, 0);
}
void store_vector_tail() {
  wr16(0x5ad44, lo(5));
  wr16(0x5ad46, lo(6));
  wr16(0x5ad48, lo(7));
  D(5) = uint32_t(ext(lo(5)));
  D(6) = uint32_t(ext(lo(6)));
  D(7) = uint32_t(ext(lo(7)));
  flags(D(7), 32);
}
void scale_vector(uint16_t sign, uint32_t shift_base, bool check_zero_length) {
  if (lo(0) == 0) {
    if (!check_zero_length) {                         // $39d48: error trap, loops forever
      wr16(0x5ac96, 21);
      for (;;) call_c(0x2f216);
    }
    zero_vector_tail();
    store_vector_tail();
    return;
  }
  if (int16_t(lo(0)) < 0) setw(0, uint16_t(-int16_t(lo(0))));
  setw(2, int16_t(lo(5)) < 0 ? uint16_t(-int16_t(lo(5))) : lo(5));
  setw(3, int16_t(lo(6)) < 0 ? uint16_t(-int16_t(lo(6))) : lo(6));
  setw(4, int16_t(lo(7)) < 0 ? uint16_t(-int16_t(lo(7))) : lo(7));
  hypot_308cc();
  if (check_zero_length && lo(1) == 0) {
    zero_vector_tail();
    store_vector_tail();
    return;
  }
  D(2) = shift_base;
  D(0) = uint32_t(ext(lo(0)));
  while (int32_t(D(0)) <= int32_t(D(1))) {            // scale up until > length
    D(0) = uint32_t(asl32(int32_t(D(0)), 2));
    setw(2, lo(2) + 2);
  }
  do {                                                // scale back down
    D(0) = uint32_t(asr32(int32_t(D(0)), 2));
    setw(2, lo(2) - 2);
  } while (int16_t(lo(2)) > 1 && int32_t(D(0)) > int32_t(D(1)));
  D(0) = uint32_t(asl32(int32_t(D(0)), 2));
  setw(2, lo(2) + 2);
  D(0) = uint32_t(asl32(int32_t(D(0)), 8));
  divuw(D(0), lo(1));
  for (int r = 5; r <= 7; ++r) D(r) = uint32_t(int32_t(int16_t(lo(r))) * int32_t(int16_t(lo(0))));
  for (int r = 5; r <= 7; ++r) D(r) = uint32_t(asr32(int32_t(D(r)), D(2)));
  if (int16_t(sign) < 0)
    for (int r = 5; r <= 7; ++r) setw(r, uint16_t(-int16_t(lo(r))));
  store_vector_tail();
}
uint32_t stack_arg(int i) { return rd32(cpu().a[7] + 4 + 4u * uint32_t(i)); }

void scale_vector_regs_39cb6() { scale_vector(lo(0), 8, true); }
NATIVE_ASM(0x39cb6, scale_vector_regs_39cb6, true);

void scale_vector_39cc0() {
  uint32_t a0 = stack_arg(0);
  D(0) = a0;
  D(5) = stack_arg(1);
  D(6) = stack_arg(2);
  D(7) = stack_arg(3);
  scale_vector(uint16_t(a0 >> 16), 8, true);
}
NATIVE_ASM(0x39cc0, scale_vector_39cc0, true);

void scale_vector_39d58() {
  uint32_t a0 = stack_arg(0);
  D(0) = a0;
  D(5) = stack_arg(1);
  D(6) = stack_arg(2);
  D(7) = stack_arg(3);
  scale_vector(uint16_t(a0 >> 16), 3, false);
}
NATIVE_ASM(0x39d58, scale_vector_39d58, true);

// ---- $39de2 ---------------------------------------------------------------
// Rotate (d2, d3, d4) by the rows of the matrix at $5aef4 and append two
// result records (x', y' scaled by 1 << d1) to the list at $5acc2.
void rotate_append_39de2() {
  uint32_t& a0 = cpu().a[0];
  uint32_t& a3 = cpu().a[3];
  uint16_t d7w = lo(7);
  setw(5, lo(7));
  a0 = 0x5aef4;
  a3 = rd32(0x5acc2);
  auto mulsw = [&](int r) {
    D(r) = uint32_t(int32_t(int16_t(lo(r))) * int32_t(int16_t(rd16(a0))));
    a0 += 2;
  };
  setw(5, lo(2));
  setw(6, lo(3));
  setw(7, lo(4));
  mulsw(5);
  mulsw(6);
  mulsw(7);
  D(7) += D(6);
  D(7) += D(5);
  D(7) = uint32_t(asr32(int32_t(D(7)), 8));
  D(7) = uint32_t(asl32(int32_t(D(7)), D(1)));
  wr32(a3, D(7));
  a3 += 4;
  wr32(a3, 0);
  a3 += 4;
  a0 += 6;
  mulsw(2);
  mulsw(3);
  mulsw(4);
  D(4) += D(3);
  D(4) += D(2);
  D(4) = uint32_t(asr32(int32_t(D(4)), 8));
  D(4) = uint32_t(asl32(int32_t(D(4)), D(1)));
  wr32(a3, D(4));
  a3 += 4;
  setw(7, d7w);
  wr16(a3, lo(7));
  a3 += 2;
  a3 += 2;
  wr32(a3, 0);
  wr32(a3 + 4, 0);
  wr32(a3 + 8, 0);
  wr32(0x5acc2, a3);
  flags(a3, 32);
}
NATIVE_ASM(0x39de2, rotate_append_39de2, true);

// ---- $39e34 ---------------------------------------------------------------
// Pan the external-view angles ($5ad8c tilt, $5ad8e pan; 0..28799 = 360
// degrees) by 480 per frame toward the stick direction in $5ab26/$5ab28.
// Leaves d1 = pan, d2 = stick bits, d4 = tilt like the original.
void view_pan_39e34() {
  if (rd8(0x5aa7d) == 0 || rd8(0x5aaa5) != 0 || rd8(0x5aaac) != 0 || rd8(0x5aba6) != 0) return;
  setw(4, rd16(0x5ad8c));
  setw(1, rd16(0x5ad8e));
  setb(2, rd8(0x5ab26));
  if (uint8_t(D(2)) != 0) {
    setb(2, uint8_t(D(2)) & 0x20);
    int32_t t = int16_t(lo(4));
    if (!(uint8_t(D(2)) & 0x20)) {
      if (t < 14400) {
        setw(4, lo(4) + 480);
        if (int16_t(lo(4)) >= 7200) return;
      } else {
        setw(4, lo(4) + 480);
        if (int16_t(lo(4)) >= 28800) setw(4, lo(4) - 28800);
      }
    } else {
      if (t < 14400) {
        int32_t r = t - 480;
        setw(4, lo(4) - 480);
        if (r < 0) setw(4, lo(4) + 28800);
      } else {
        int32_t r = t - 480;
        setw(4, lo(4) - 480);
        if (!(r > 21600 && int16_t(lo(4)) > 21600)) return;
      }
    }
    wr16(0x5ad8c, lo(4));
    return;
  }
  setb(2, rd8(0x5ab28));
  if (uint8_t(D(2)) == 0) return;
  setb(2, uint8_t(D(2)) & 8);
  if (uint8_t(D(2)) == 0) {
    setw(1, lo(1) + 480);
    if (int16_t(lo(1)) >= 28800) setw(1, lo(1) - 28800);
  } else {
    int32_t r = int32_t(int16_t(lo(1))) - 480;
    setw(1, lo(1) - 480);
    if (r < 0) setw(1, lo(1) + 28800);
  }
  wr16(0x5ad8e, lo(1));
}
NATIVE_ASM(0x39e34, view_pan_39e34, true);

// ---- $39b06 ---------------------------------------------------------------
// Scripted view-angle sequence: mode $5aab8 (1..4) picks a pair of byte
// strings from the table at $39ade (header byte = length, then offsets that
// are fed to the view angle $5ac7c and the forward offset $5ac10).
void view_sequence_39b06() {
  constexpr uint32_t table = 0x39ade;
  setb(0, rd8(0x5aab8));
  if (uint8_t(D(0)) == 0) return;
  setw(1, rd16(0x5ac7e));
  if (int16_t(lo(1)) < 0) setw(1, uint16_t(-int16_t(lo(1))));
  if (int16_t(lo(1)) > 14) {
    wr8(0x5aab8, 0);
    return;
  }
  cpu().a[0] = table;
  setw(0, uint16_t(int16_t(int8_t(uint8_t(D(0))))));
  setw(0, lo(0) - 1);
  setw(0, uint16_t(lo(0) << 3));
  cpu().a[1] = rd32(table + uint32_t(ext(lo(0))));
  setb(1, rd8(cpu().a[1]));
  if (int8_t(uint8_t(D(1))) > int8_t(rd8(0x5ab87))) {
    setw(2, rd16(0x5abc4) & 0x400);
    if (lo(2) == 0) {
      wr8(0x5ab87, 0);
      wr16(0x5abc4, rd16(0x5abc4) | 0x400);
    }
    setw(0, lo(0) + 4);
    cpu().a[1] = rd32(table + uint32_t(ext(lo(0))));
    setb(2, rd8(0x5ab87));
    setw(2, uint16_t(int16_t(int8_t(uint8_t(D(2))))));
    setb(0, rd8(cpu().a[1] + uint32_t(ext(lo(2)))));
    wr8(0x5ab87, rd8(0x5ab87) + 1);
    setw(0, uint16_t(int16_t(int8_t(uint8_t(D(0))))));
    setw(1, lo(0) + 144);
    wr16(0x5ac7c, lo(1));
    wr16(0x5abd0, lo(0));
    setw(0, uint16_t(lo(0) << 3));
    setw(1, lo(0));
    setw(0, lo(0) + lo(0));
    setw(0, lo(0) + lo(0));
    setw(0, lo(0) + lo(1));
    D(0) = uint32_t(ext(lo(0)));
    wr32(0x5ac10, D(0));
  } else {
    wr8(0x5aab8, 0);
    wr8(0x5ab87, 0);
    wr16(0x5abc4, rd16(0x5abc4) & 0xFBFF);
    wr32(0x5ac10, 0);
    wr16(0x5ac7c, 144);
  }
  call_c(0x2f02c);
}
NATIVE_ASM(0x39b06, view_sequence_39b06, true);

}  // namespace
}  // namespace native
