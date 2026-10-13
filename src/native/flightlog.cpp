// Flight log (df0:config) handling and the codewheel check code.
// Original: hunk 0, $2843c-$28a38 (compiled C).
#include "native/globals.hpp"
#include "native/os.hpp"

namespace native {
using namespace g;

// $21060: inspect the disk in DF0: (Lock/Info on "df0:"): 0 = writable DOS
// disk, 1 = no disk, 2 = not a DOS disk ("BAD"), 3 = write protected.
// (Like the original it frees the 4-byte aligned pointer, not the one
// AllocMem returned; both are equal with our 8-byte aligned heap.)
int32_t check_log_disk() {
  uint32_t raw = os::AllocMem(40, 0x10001);
  uint32_t info = (raw + 3) & ~3u;
  wr32(0x650b4, info);
  int32_t lock = os::Lock(0x2a408, -2);
  os::Info(lock, info);
  os::UnLock(lock);
  int32_t result;
  int32_t disk_type = rds32(info + 24);
  if (disk_type < 0) result = 1;
  else if (disk_type == 0x42414400) result = 2;
  else if (rd32(info + 8) == 0x50) result = 3;
  else result = 0;
  if (info != 0) os::FreeMem(info, 40);
  return result;
}
NATIVE_C(0x21060, check_log_disk, false);

// $28474: create df0:config from the in-memory log.
static int32_t write_new_config() {
  int32_t fh = os::Open(str_config_w, os::MODE_NEWFILE);
  if (fh < 1) return 0;
  int32_t n = os::Write(fh, config_buf, 78);
  if (n == -1) return 0;
  log_disk_ok = 1;
  return os::Close(fh);
}
NATIVE_C(0x28474, write_new_config, false);

// $284de: read df0:config into the log buffer; 1 on success.
static int32_t read_config() {
  int32_t fh = os::Open(str_config_r, os::MODE_OLDFILE);
  if (fh < 1) {
    log_disk_ok = 0;
    return 0;
  }
  log_disk_ok = 1;
  int32_t n = os::Read(fh, config_buf, 78);
  os::Close(fh);
  if (n == -1) return 0;
  log_disk_ok = 1;
  return 1;
}
NATIVE_C(0x284de, read_config, false);

// $2843c: at startup, load the log or create it.
static void load_flight_log() {
  os::DisownBlitter();
  disk_state = int16_t(check_log_disk());
  if (disk_state == 0) {
    config_loaded = int16_t(read_config());
    if (config_loaded == 1) log_disk_ok = 1;
    else write_new_config();
  }
  os::OwnBlitter();
}
NATIVE_C(0x2843c, load_flight_log, false);

// $2855e: reset the log (SHIFT-2 in the flight log screen).
static void reset_flight_log() {
  uint32_t p = config_buf;
  for (int i = 0; i < 39; ++i) wr16(p + uint32_t(i) * 2, 0);
  log_dirty = 1;
}
NATIVE_C(0x2855e, reset_flight_log, true);

// $28592: write the log back to df0:config ("1 ... update log onto disk").
static int16_t save_flight_log() {
  int16_t r = disk_state;
  if (disk_state != 0) return r;
  os::DisownBlitter();
  log_dirty = 0;
  disk_state = int16_t(check_log_disk());
  if (disk_state == 0) {
    os::FreeSprite(0);
    if (log_disk_ok == 0) return 0;
    int32_t fh = os::Open(str_config_u, os::MODE_OLDFILE);
    os::FreeSprite(0);
    if (fh < 1) {
      log_disk_ok = 0;
      return 0;
    }
    int32_t n = os::Write(fh, config_buf, 78);
    os::FreeSprite(0);
    os::Close(fh);
    os::FreeSprite(0);
    if (n == -1) return 0;
  }
  os::OwnBlitter();
  return r;
}
NATIVE_C(0x28592, save_flight_log, false);

// $2885c: codewheel window hash (15 rounds of a shift/xor-ish mix).
static uint16_t codewheel_hash(int16_t a, int16_t b, int16_t idx, int16_t d) {
  uint16_t v = uint16_t(uint16_t((a - d) << 4) + uint16_t(wheel_table.at(idx)) + uint16_t(b)) & 0xFF;
  uint16_t x = uint16_t((v << 8) + v);
  for (int16_t n = 15; n != 0; --n) {
    bool low = x & 1;
    x = uint16_t((int16_t(x) >> 1) & 0x7FFF);
    if (low) x = uint16_t(x + 0xB400);
  }
  wheel_sel = int16_t((int16_t(v) >> 1) & 3);
  return x;
}
NATIVE_C(0x2885c, codewheel_hash, true);

// $288e8: codewheel answer for the current wheel position/selection.
static uint32_t codewheel_answer() {
  int16_t d = wheel_d;
  int16_t entry = wheel_table.at(d);
  int16_t t1 = int16_t(entry & 7);
  int16_t t2 = int16_t(entry >> 4);
  int16_t a = wheel_a;
  if ((d ^ a) & 1) {
    int16_t idx = int16_t((t1 << 1) + (wheel_b & 1));
    wheel_b = int16_t((wheel_table2.at(idx) + a - t2) & 15);
  }
  int16_t i = int16_t(t1 << 1);
  int16_t u = int16_t((int16_t(wheel_b - a) + t2) & 15);
  uint16_t r;
  if (int16_t(wheel_table2.at(i)) == u || u == int16_t(wheel_table2.at(i + 1)))
    r = codewheel_hash(wheel_c, 8, d, a);
  else
    r = codewheel_hash(wheel_b, 0, d, a);
  return uint32_t(uint16_t(wheel_mask.at(wheel_sel)) & r);
}
NATIVE_C(0x288e8, codewheel_answer, true);

}  // namespace native
