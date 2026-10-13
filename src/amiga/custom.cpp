// Custom chip emulation: register file, copper + bitplane display, blitter,
// Paula audio DMA, joystick/mouse ports and the CIA-A port register.
#include "amiga/machine.hpp"
#include "amiga/hle.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace amiga {

namespace {
constexpr uint32_t CUSTOM = 0xDFF000;

inline uint32_t rgb12(uint16_t c) {
  uint32_t r = (c >> 8) & 15, g = (c >> 4) & 15, b = c & 15;
  return (r * 17) << 16 | (g * 17) << 8 | (b * 17);
}
}  // namespace

// ---- bus dispatch ------------------------------------------------------------

uint8_t Machine::io_read8(uint32_t addr) {
  if ((addr & 0xFFF000) == CUSTOM) {
    uint16_t v = custom_read(addr & 0x1FE);
    return (addr & 1) ? uint8_t(v) : uint8_t(v >> 8);
  }
  if ((addr & 0xFFF000) == 0xBFE000 && (addr & 1)) {  // CIA-A
    int reg = (addr >> 8) & 15;
    if (reg == 0) {  // PRA: /FIR1 bit7, /FIR0 bit6, /RDY /TK0 /WPRO /CHNG, LED, OVL
      uint8_t v = 0x3F;
      if (!lmb_) v |= 0x40;
      if (!joy_fire_) v |= 0x80;
      return v;
    }
    return 0xFF;
  }
  if ((addr & 0xFFF000) == 0xBFD000) return 0xFF;  // CIA-B
  return 0;
}

uint16_t Machine::io_read16(uint32_t addr) {
  if ((addr & 0xFFF000) == CUSTOM) return custom_read(addr & 0x1FE);
  return uint16_t(io_read8(addr) << 8 | io_read8(addr + 1));
}

void Machine::io_write8(uint32_t addr, uint8_t v) {
  if ((addr & 0xFFF000) == CUSTOM) {
    // Byte writes to custom registers put the byte on both halves.
    custom_write(addr & 0x1FE, uint16_t(v << 8 | v));
    return;
  }
  // CIA writes (LED/filter, timers) are ignored.
}

void Machine::io_write16(uint32_t addr, uint16_t v) {
  if ((addr & 0xFFF000) == CUSTOM) { custom_write(addr & 0x1FE, v); return; }
  io_write8(addr, uint8_t(v >> 8));
  io_write8(addr + 1, uint8_t(v));
}

// ---- registers ---------------------------------------------------------------

uint16_t Machine::custom_read(uint32_t reg) {
  switch (reg) {
    case 0x002:  // DMACONR (blitter is always done)
      return uint16_t((dmacon_ & 0x07FF) | (blt_zero_ ? 0x2000 : 0));
    case 0x004: {  // VPOSR: NTSC Agnus, V8
      return uint16_t(0x1000 | ((vpos_ >> 8) & 1));
    }
    case 0x006: {  // VHPOSR
      int h = int((cpu.cycles - line_start_) * CYCLES_PER_LINE / line_cycles_ / 2);
      if (h > 0xE2) h = 0xE2;
      if (h < 0) h = 0;
      return uint16_t((vpos_ & 0xFF) << 8 | h);
    }
    case 0x00A:  // JOY0DAT (mouse counters)
      return uint16_t((mouse_y_ & 0xFF) << 8 | (mouse_x_ & 0xFF));
    case 0x00C: {  // JOY1DAT (joystick)
      uint16_t v = 0;
      if (joy_right_) v |= 0x0002;
      if (joy_left_) v |= 0x0200;
      if (joy_right_ != joy_down_) v |= 0x0001;
      if (joy_left_ != joy_up_) v |= 0x0100;
      return v;
    }
    case 0x010: return adkcon_;
    case 0x012: case 0x014: return 0;
    case 0x016: {  // POTGOR: right mouse button = DATLY (bit 10), active low
      uint16_t v = 0xFF00;
      if (rmb_) v &= ~0x0400;
      return v;
    }
    case 0x018: return 0x3000;  // SERDATR: TBE, TSRE
    case 0x01C: return intena_;
    case 0x01E: return intreq_;
  }
  return 0;
}

void Machine::custom_write(uint32_t reg, uint16_t v) {
  regs_[reg >> 1] = v;
  auto setclr = [](uint16_t& r, uint16_t v) {
    if (v & 0x8000) r |= v & 0x7FFF; else r &= ~v;
  };
  switch (reg) {
    case 0x034: potgo_ = v; return;
    case 0x040: bltcon0_ = v; return;
    case 0x042: bltcon1_ = v; return;
    case 0x044: bltafwm_ = v; return;
    case 0x046: bltalwm_ = v; return;
    case 0x048: bltpt_[2] = (bltpt_[2] & 0xFFFF) | uint32_t(v & 0x1F) << 16; return;
    case 0x04A: bltpt_[2] = (bltpt_[2] & 0xFFFF0000) | (v & 0xFFFE); return;
    case 0x04C: bltpt_[1] = (bltpt_[1] & 0xFFFF) | uint32_t(v & 0x1F) << 16; return;
    case 0x04E: bltpt_[1] = (bltpt_[1] & 0xFFFF0000) | (v & 0xFFFE); return;
    case 0x050: bltpt_[0] = (bltpt_[0] & 0xFFFF) | uint32_t(v & 0x1F) << 16; return;
    case 0x052: bltpt_[0] = (bltpt_[0] & 0xFFFF0000) | (v & 0xFFFE); return;
    case 0x054: bltpt_[3] = (bltpt_[3] & 0xFFFF) | uint32_t(v & 0x1F) << 16; return;
    case 0x056: bltpt_[3] = (bltpt_[3] & 0xFFFF0000) | (v & 0xFFFE); return;
    case 0x058: blit(); return;
    case 0x060: bltmod_[2] = int16_t(v & 0xFFFE); return;
    case 0x062: bltmod_[1] = int16_t(v & 0xFFFE); return;
    case 0x064: bltmod_[0] = int16_t(v & 0xFFFE); return;
    case 0x066: bltmod_[3] = int16_t(v & 0xFFFE); return;
    case 0x070: bltdat_[2] = v; return;
    case 0x072: bltdat_[1] = v; return;
    case 0x074: bltdat_[0] = v; return;
    case 0x080: cop1lc_ = (cop1lc_ & 0xFFFF) | uint32_t(v & 0x1F) << 16; return;
    case 0x082: cop1lc_ = (cop1lc_ & 0xFFFF0000) | (v & 0xFFFE); return;
    case 0x084: cop2lc_ = (cop2lc_ & 0xFFFF) | uint32_t(v & 0x1F) << 16; return;
    case 0x086: cop2lc_ = (cop2lc_ & 0xFFFF0000) | (v & 0xFFFE); return;
    case 0x088: coppc_ = cop1lc_; cop_waiting_ = false; cop_done_ = false; return;
    case 0x08A: coppc_ = cop2lc_; cop_waiting_ = false; cop_done_ = false; return;
    case 0x08E: diwstrt_ = v; return;
    case 0x090: diwstop_ = v; return;
    case 0x092: ddfstrt_ = v & 0xFC; return;
    case 0x094: ddfstop_ = v & 0xFC; return;
    case 0x096: {
      uint16_t old = dmacon_;
      setclr(dmacon_, v);
      for (int ch = 0; ch < 4; ++ch) {
        bool was = (old & 0x200) && (old & (1 << ch));
        bool now = (dmacon_ & 0x200) && (dmacon_ & (1 << ch));
        if (now && !was) audio_dma_start(ch);
        if (!now && was) aud_[ch].active = false;
      }
      return;
    }
    case 0x09A: setclr(intena_, v); check_interrupts(); return;
    case 0x09C: setclr(intreq_, v); check_interrupts(); return;
    case 0x09E: setclr(adkcon_, v); return;
    case 0x100: bplcon0_ = v; return;
    case 0x102: bplcon1_ = v; return;
    case 0x104: bplcon2_ = v; return;
    case 0x108: bpl1mod_ = int16_t(v & 0xFFFE); return;
    case 0x10A: bpl2mod_ = int16_t(v & 0xFFFE); return;
  }
  if (reg >= 0x0A0 && reg < 0x0E0) {
    int ch = (reg - 0x0A0) >> 4;
    AudioChannel& a = aud_[ch];
    switch (reg & 0xF) {
      case 0x0: a.lc = (a.lc & 0xFFFF) | uint32_t(v & 0x1F) << 16; break;
      case 0x2: a.lc = (a.lc & 0xFFFF0000) | (v & 0xFFFE); break;
      case 0x4: a.len = v; break;
      case 0x6: a.per = v; break;
      case 0x8: a.vol = v & 0x7F; break;
      case 0xA: a.dat = v; break;
    }
    return;
  }
  if (reg >= 0x0E0 && reg < 0x0F8) {
    int p = (reg - 0x0E0) >> 2;
    if (reg & 2) bplpt_[p] = (bplpt_[p] & 0xFFFF0000) | (v & 0xFFFE);
    else bplpt_[p] = (bplpt_[p] & 0xFFFF) | uint32_t(v & 0x1F) << 16;
    return;
  }
  if (reg >= 0x180 && reg < 0x1C0) {
    color_[(reg - 0x180) >> 1] = v & 0xFFF;
    return;
  }
}

// ---- input from host --------------------------------------------------------

void Machine::mouse_move(int dx, int dy) {
  mouse_x_ = (mouse_x_ + dx) & 0xFF;
  mouse_y_ = (mouse_y_ + dy) & 0xFF;
}

void Machine::mouse_button(int button, bool down) {
  if (button == 0) lmb_ = down; else rmb_ = down;
  hle_->mouse_button(button, down);
}

void Machine::joy_state(bool up, bool down, bool left, bool right, bool fire) {
  bool fire_changed = fire != joy_fire_;
  joy_up_ = up; joy_down_ = down; joy_left_ = left; joy_right_ = right;
  joy_fire_ = fire;
  if (fire_changed) hle_->joy_fire(fire);
}

void Machine::key_event(uint8_t rawkey, bool down) { hle_->key_event(rawkey, down); }

// ---- copper + display -------------------------------------------------------

void Machine::copper_reset() {
  coppc_ = cop1lc_;
  cop_waiting_ = false;
  cop_done_ = false;
}

void Machine::copper_run_until(int vpos, int hpos) {
  if (!(dmacon_ & 0x200) || !(dmacon_ & 0x80)) return;
  for (int guard = 0; guard < 4096 && !cop_done_; ++guard) {
    if (cop_waiting_) {
      int vmask = (cop_wait_mv_ | 0x80);
      int cv = vpos & 0xFF;
      // Beam position comparison (V7 always compared, V8 never).
      int beam = ((cv & vmask) << 8) | (hpos & cop_wait_mh_);
      int want = ((cop_wait_v_ & vmask) << 8) | (cop_wait_h_ & cop_wait_mh_);
      // Lines >= 256 compare as their low 8 bits; treat them as past any
      // wait for an earlier (low) line to avoid re-triggering.
      if (vpos > 255 && cop_wait_v_ < 0x80) beam = 0xFFFF;
      if (beam < want) return;
      cop_waiting_ = false;
    }
    uint16_t ir1 = read16(coppc_);
    uint16_t ir2 = read16(coppc_ + 2);
    coppc_ += 4;
    if (!(ir1 & 1)) {  // MOVE
      uint32_t reg = ir1 & 0x1FE;
      if (reg < 0x40) { cop_done_ = true; return; }  // illegal: copper stops
      custom_write(reg, ir2);
      continue;
    }
    if (ir1 == 0xFFFF && ir2 == 0xFFFE) { cop_done_ = true; return; }
    cop_wait_v_ = ir1 >> 8;
    cop_wait_h_ = ir1 & 0xFE;
    cop_wait_mv_ = (ir2 >> 8) & 0x7F;
    cop_wait_mh_ = ir2 & 0xFE;
    if (ir2 & 1) {  // SKIP
      int vmask = (cop_wait_mv_ | 0x80);
      int beam = (((vpos & 0xFF) & vmask) << 8) | (hpos & cop_wait_mh_);
      int want = ((cop_wait_v_ & vmask) << 8) | (cop_wait_h_ & cop_wait_mh_);
      if (beam >= want) coppc_ += 4;
      continue;
    }
    cop_waiting_ = true;
  }
}

void Machine::render_line(int line) {
  uint32_t* out = nullptr;
  int fy = line - FB_VSTART;
  if (fy >= 0 && fy < FB_H) out = &fb_[size_t(fy) * FB_W];

  int vstart = diwstrt_ >> 8;
  int vstop = (diwstop_ >> 8) | ((diwstop_ & 0x8000) ? 0 : 0x100);
  int hstart = diwstrt_ & 0xFF;
  int hstop = (diwstop_ & 0xFF) | 0x100;
  int nplanes = (bplcon0_ >> 12) & 7;
  bool hires = bplcon0_ & 0x8000;
  bool ham = bplcon0_ & 0x0800;
  bool dpf = bplcon0_ & 0x0400;
  if (hires) nplanes = std::min(nplanes, 4);
  bool dma = (dmacon_ & 0x200) && (dmacon_ & 0x100);
  bool active = dma && nplanes > 0 && line >= vstart && line < vstop;

  uint32_t bg = rgb12(color_[0]);
  if (!out) {
    // Still consume bitplane data so pointers advance correctly.
    if (active) {
      int nwords = hires ? (ddfstop_ - ddfstrt_) / 4 + 2 : (ddfstop_ - ddfstrt_) / 8 + 1;
      for (int p = 0; p < nplanes; ++p)
        bplpt_[p] += uint32_t(nwords * 2 + ((p & 1) ? bpl2mod_ : bpl1mod_));
    }
    return;
  }
  for (int x = 0; x < FB_W; ++x) out[x] = bg;
  if (!active) return;

  int nwords = hires ? (ddfstop_ - ddfstrt_) / 4 + 2 : (ddfstop_ - ddfstrt_) / 8 + 1;
  if (nwords <= 0 || nwords > 64) nwords = 20;
  static uint8_t pix[64 * 16 + 32];
  int npix = nwords * 16;
  std::memset(pix, 0, sizeof pix);
  int delay1 = bplcon1_ & 15, delay2 = (bplcon1_ >> 4) & 15;
  for (int p = 0; p < nplanes; ++p) {
    uint32_t pt = bplpt_[p];
    int delay = (p & 1) ? delay2 : delay1;
    for (int w = 0; w < nwords; ++w) {
      uint16_t data = read16(pt + uint32_t(w * 2));
      for (int b = 0; b < 16; ++b)
        if (data & (0x8000 >> b)) pix[w * 16 + b + delay] |= uint8_t(1 << p);
    }
    bplpt_[p] = pt + uint32_t(nwords * 2 + ((p & 1) ? bpl2mod_ : bpl1mod_));
  }
  // first displayed pixel position (lores pixels, beam coordinates)
  int x0 = hires ? ddfstrt_ * 2 + 9 : ddfstrt_ * 2 + 17;
  uint32_t ham_col = color_[0];
  int step = hires ? 2 : 1;
  for (int i = 0; i < npix; i += step) {
    int hx = x0 + (hires ? i / 2 : i);
    uint8_t c = pix[i];
    uint32_t rgb;
    if (ham && !hires && nplanes >= 6) {
      int ctl = (c >> 4) & 3, val = c & 15;
      switch (ctl) {
        case 0: ham_col = color_[val]; break;
        case 1: ham_col = (ham_col & 0xFF0) | uint32_t(val); break;
        case 2: ham_col = (ham_col & 0x0FF) | uint32_t(val) << 8; break;
        case 3: ham_col = (ham_col & 0xF0F) | uint32_t(val) << 4; break;
      }
      rgb = rgb12(uint16_t(ham_col));
    } else if (dpf) {
      int pf1 = (c & 1) | ((c >> 1) & 2) | ((c >> 2) & 4);
      int pf2 = ((c >> 1) & 1) | ((c >> 2) & 2) | ((c >> 3) & 4);
      bool pf2pri = bplcon2_ & 0x40;
      int idx;
      if (pf1 && pf2) idx = pf2pri ? pf2 + 8 : pf1;
      else if (pf1) idx = pf1;
      else if (pf2) idx = pf2 + 8;
      else idx = 0;
      rgb = rgb12(color_[idx]);
    } else if (nplanes == 6 && !hires) {  // EHB
      uint16_t col = color_[c & 31];
      if (c & 32) col = (col >> 1) & 0x777;
      rgb = rgb12(col);
    } else {
      rgb = rgb12(color_[c & 31]);
    }
    if (hx < hstart || hx >= hstop) continue;
    int fx = hx - FB_HSTART;
    if (fx >= 0 && fx < FB_W) out[fx] = rgb;
  }
}

void Machine::render_frame() {
  copper_reset();
  for (int line = 0; line < LINES_PER_FRAME; ++line) {
    copper_run_until(line, 0x10);
    render_line(line);
    copper_run_until(line, 0xE2);
  }
}

// ---- blitter ------------------------------------------------------------------

namespace {
inline uint16_t minterm(uint8_t lf, uint16_t a, uint16_t b, uint16_t c) {
  uint16_t r = 0;
  if (lf & 0x01) r |= ~a & ~b & ~c;
  if (lf & 0x02) r |= ~a & ~b & c;
  if (lf & 0x04) r |= ~a & b & ~c;
  if (lf & 0x08) r |= ~a & b & c;
  if (lf & 0x10) r |= a & ~b & ~c;
  if (lf & 0x20) r |= a & ~b & c;
  if (lf & 0x40) r |= a & b & ~c;
  if (lf & 0x80) r |= a & b & c;
  return r;
}
}  // namespace

void Machine::blit() {
  uint16_t size = regs_[0x058 >> 1];
  int h = size >> 6;
  int w = size & 0x3F;
  if (h == 0) h = 1024;
  if (w == 0) w = 64;
  if (bltcon1_ & 1) {
    blit_line();
  } else {
    bool useA = bltcon0_ & 0x800, useB = bltcon0_ & 0x400;
    bool useC = bltcon0_ & 0x200, useD = bltcon0_ & 0x100;
    uint8_t lf = uint8_t(bltcon0_);
    int ash = bltcon0_ >> 12, bsh = bltcon1_ >> 12;
    bool desc = bltcon1_ & 2;
    bool fill = bltcon1_ & 0x18;
    bool ife = bltcon1_ & 0x08, efe = bltcon1_ & 0x10;
    int inc = desc ? -2 : 2;
    uint16_t aold = 0, bold = 0;
    bool zero = true;
    uint32_t& apt = bltpt_[0];
    uint32_t& bpt = bltpt_[1];
    uint32_t& cpt = bltpt_[2];
    uint32_t& dpt = bltpt_[3];
    for (int y = 0; y < h; ++y) {
      bool fc = bltcon1_ & 4;  // FCI
      for (int x = 0; x < w; ++x) {
        if (useA) { bltdat_[0] = read16(apt); apt += uint32_t(inc); }
        if (useB) { bltdat_[1] = read16(bpt); bpt += uint32_t(inc); }
        if (useC) { bltdat_[2] = read16(cpt); cpt += uint32_t(inc); }
        uint16_t a = bltdat_[0];
        if (x == 0) a &= bltafwm_;
        if (x == w - 1) a &= bltalwm_;
        uint16_t as, bs;
        if (!desc) {
          as = uint16_t(((uint32_t(aold) << 16) | a) >> ash);
          bs = uint16_t(((uint32_t(bold) << 16) | bltdat_[1]) >> bsh);
        } else {
          as = uint16_t(((uint32_t(a) << 16) | aold) >> (16 - ash));
          bs = uint16_t(((uint32_t(bltdat_[1]) << 16) | bold) >> (16 - bsh));
        }
        aold = a;
        bold = bltdat_[1];
        uint16_t d = minterm(lf, as, bs, bltdat_[2]);
        if (fill) {
          uint16_t r = 0;
          for (int bit = 0; bit < 16; ++bit) {
            uint16_t m = uint16_t(1 << bit);
            bool v = d & m;
            if (efe) { fc ^= v; if (fc) r |= m; }
            else { if (fc || v) r |= m; fc ^= v; }
          }
          d = r;
        }
        if (d) zero = false;
        if (useD) { write16(dpt, d); dpt += uint32_t(inc); }
      }
      if (useA) apt += uint32_t(desc ? -bltmod_[0] : bltmod_[0]);
      if (useB) bpt += uint32_t(desc ? -bltmod_[1] : bltmod_[1]);
      if (useC) cpt += uint32_t(desc ? -bltmod_[2] : bltmod_[2]);
      if (useD) dpt += uint32_t(desc ? -bltmod_[3] : bltmod_[3]);
    }
    blt_zero_ = zero;
  }
  raise(INT_BLIT);
  // The blit happens instantly; charge a rough DMA cost to the CPU.
  cpu.cycles += int64_t(w) * h * 2;
}

void Machine::blit_line() {
  uint16_t size = regs_[0x058 >> 1];
  int len = size >> 6;
  if (len == 0) len = 1024;
  uint8_t lf = uint8_t(bltcon0_);
  int ashift = bltcon0_ >> 12;
  int bshift = bltcon1_ >> 12;
  bool sing = bltcon1_ & 2;
  bool sud = bltcon1_ & 0x10, sul = bltcon1_ & 0x08, aul = bltcon1_ & 0x04;
  bool sign = bltcon1_ & 0x40;
  int16_t err = int16_t(bltpt_[0] & 0xFFFF);
  uint32_t cpt = bltpt_[2];
  uint32_t dpt = bltpt_[3];
  uint16_t texture = bltdat_[1];
  bool onedot = false;
  bool zero = true;
  uint16_t ahold = bltdat_[0] & bltafwm_;

  auto incx = [&]() { if (++ashift == 16) { ashift = 0; cpt += 2; } };
  auto decx = [&]() { if (ashift-- == 0) { ashift = 15; cpt -= 2; } };
  auto incy = [&]() { cpt += uint32_t(bltmod_[2]); onedot = false; };
  auto decy = [&]() { cpt -= uint32_t(bltmod_[2]); onedot = false; };

  for (int i = 0; i < len; ++i) {
    uint16_t c = (bltcon0_ & 0x200) ? read16(cpt) : bltdat_[2];
    uint16_t a = uint16_t(ahold >> ashift);
    bool draw = !(sing && onedot);
    uint16_t b = ((texture >> bshift) & 1) ? 0xFFFF : 0;
    if (!draw) a = 0;
    uint16_t d = minterm(lf, a, b, c);
    onedot = true;
    if (d) zero = false;
    uint32_t write_at = dpt;
    // step
    if (!sign) err = int16_t(err + bltmod_[0]);
    else err = int16_t(err + bltmod_[1]);
    if (!sign) {
      if (sud) { if (sul) decy(); else incy(); }
      else { if (sul) decx(); else incx(); }
    }
    if (sud) { if (aul) decx(); else incx(); }
    else { if (aul) decy(); else incy(); }
    sign = err < 0;
    bshift = (bshift - 1) & 15;
    if ((bltcon0_ & 0x100) && draw) write16(write_at, d);
    dpt = cpt;
  }
  bltpt_[0] = (bltpt_[0] & 0xFFFF0000) | uint16_t(err);
  bltpt_[2] = cpt;
  bltpt_[3] = dpt;
  bltcon0_ = uint16_t((bltcon0_ & 0x0FFF) | (ashift << 12));
  bltcon1_ = uint16_t((bltcon1_ & 0x0FBF) | (bshift << 12) | (sign ? 0x40 : 0));
  blt_zero_ = zero;
}

// ---- Paula --------------------------------------------------------------------

void Machine::audio_dma_start(int ch) {
  AudioChannel& a = aud_[ch];
  a.active = true;
  a.pt = a.lc;
  a.remaining = a.len ? a.len : 0x10000;
  a.byte_index = 0;
  a.phase = 0;
  raise(uint16_t(INT_AUD0 << ch));
}

void Machine::audio_advance(double ticks, int16_t* out, int frames) {
  for (int f = 0; f < frames; ++f) {
    int l = 0, r = 0;
    for (int ch = 0; ch < 4; ++ch) {
      AudioChannel& a = aud_[ch];
      if (a.active) {
        int per = a.per < 124 ? 124 : a.per;
        a.phase += ticks;
        while (a.phase >= per) {
          a.phase -= per;
          uint16_t w = read16(a.pt);
          a.sample = int8_t(a.byte_index ? (w & 0xFF) : (w >> 8));
          if (++a.byte_index == 2) {
            a.byte_index = 0;
            a.pt += 2;
            if (--a.remaining == 0) {
              a.pt = a.lc;
              a.remaining = a.len ? a.len : 0x10000;
              raise(uint16_t(INT_AUD0 << ch));
            }
          }
        }
      }
      int vol = a.vol > 64 ? 64 : a.vol;
      int s = a.active ? a.sample * vol : 0;
      if (ch == 0 || ch == 3) l += s; else r += s;
    }
    // A500 output stage: fixed one-pole RC low-pass at ~4.4 kHz.
    lp_l_ += lp_k_ * (double(l) - lp_l_);
    lp_r_ += lp_k_ * (double(r) - lp_r_);
    // 2 channels × 127 × 64 → scale to 16 bit
    out[f * 2] = int16_t(std::clamp(int(lp_l_ * 2), -32768, 32767));
    out[f * 2 + 1] = int16_t(std::clamp(int(lp_r_ * 2), -32768, 32767));
  }
}

void Machine::audio_line() {
  int rate = host_ ? host_->audio_rate() : 48000;
  lp_k_ = 1.0 - std::exp(-2.0 * 3.14159265358979 * 4400.0 / rate);
  audio_frac_ += double(rate) / (LINES_PER_FRAME * 60.0);
  int n = int(audio_frac_);
  if (n <= 0) return;
  audio_frac_ -= n;
  size_t off = audio_buf_.size();
  audio_buf_.resize(off + size_t(n) * 2);
  audio_advance(PAULA_CLOCK / rate, &audio_buf_[off], n);
}

}  // namespace amiga
