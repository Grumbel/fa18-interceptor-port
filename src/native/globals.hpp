// Named game variables in the memory image (addresses are runtime addresses
// of the loader layout, see AGENTS.md). Names are added as they are
// understood; DAT_xxxxx style names mark variables not yet understood.
#pragma once

#include "native/runtime.hpp"

namespace native::g {

// ---- flight log / config file (78 bytes, df0:config) ----------------------
inline constexpr Var<uint32_t> config_buf{0x2d03c};     // -> 78-byte log record
inline constexpr Var<int16_t> log_disk_ok{0x2a434};     // config readable/writable
inline constexpr Var<int16_t> disk_state{0x5ac20};      // 0 ok, else see check_log_disk()
inline constexpr Var<int16_t> config_loaded{0x2d040};   // result of read_config()
inline constexpr Var<uint8_t> log_dirty{0x5aabd};

// ---- codewheel ---------------------------------------------------------
inline constexpr Var<int16_t> wheel_table{0x2a4e0};     // [16]
inline constexpr Var<int16_t> wheel_table2{0x2a500};    // [16]
inline constexpr Var<uint16_t> wheel_mask{0x2a520};     // [4]
inline constexpr Var<int16_t> wheel_a{0x2d042};
inline constexpr Var<int16_t> wheel_b{0x2d044};
inline constexpr Var<int16_t> wheel_c{0x2d046};
inline constexpr Var<int16_t> wheel_d{0x2d048};
inline constexpr Var<int16_t> wheel_sel{0x2d04a};

// ---- libraries ----------------------------------------------------------
inline constexpr Var<uint32_t> gfx_base{0x2a792};
inline constexpr Var<uint32_t> intuition_base{0x2a6d0};

// ---- display ---------------------------------------------------------------
constexpr uint32_t view = 0x2a6e0;                      // struct View
constexpr uint32_t viewport = 0x2a6f2;                  // struct ViewPort
inline constexpr Var<uint32_t> colormap{0x2a71a};       // ViewPort ColorMap*
inline constexpr Var<uint32_t> bitmap_b{0x2a72e};       // 4-plane BitMap*
inline constexpr Var<uint32_t> game_planes{0x5a9ca};    // [4] plane pointers
inline constexpr Var<uint32_t> work_buffer{0x5a9da};    // 8000 bytes chip
inline constexpr Var<uint32_t> misc_buffer_6510c{0x6510c};
inline constexpr Var<int16_t> fade_steps{0x5aa3a};

// ---- pictures (pix/splsh, inst5, frnt5) -----------------------------------
inline constexpr Var<uint16_t> palette_buf{0x2cf64};    // [32] loaded CMAP
inline constexpr Var<uint32_t> splash_bitmap{0x2cfa4};
constexpr uint32_t splash_bmhd = 0x2cfa8;
inline constexpr Var<uint16_t> splash_bmhd_w{0x2cfa8};
inline constexpr Var<uint16_t> splash_bmhd_h{0x2cfaa};
inline constexpr Var<uint32_t> cockpit_bitmap{0x2cfd0};
constexpr uint32_t cockpit_bmhd = 0x2cfd4;
inline constexpr Var<uint16_t> cockpit_bmhd_w{0x2cfd4};
inline constexpr Var<uint16_t> cockpit_bmhd_h{0x2cfd6};
inline constexpr Var<uint32_t> front_bitmap{0x2d000};
constexpr uint32_t front_bmhd = 0x2d004;
inline constexpr Var<uint16_t> front_bmhd_w{0x2d004};
inline constexpr Var<uint16_t> front_bmhd_h{0x2d006};
// Plane checksums ($3931c); the title sequence only runs if the splash
// picture is intact (-0x3aa0).
inline constexpr Var<int16_t> splash_checksum{0x5a944};
inline constexpr Var<int16_t> cockpit_checksum{0x5a948};
inline constexpr Var<int16_t> front_checksum{0x5a94c};

// ---- sound -------------------------------------------------------------------
inline constexpr Var<uint32_t> sound_flags{0x5ae50};    // bit 7: music off?
inline constexpr Var<uint32_t> music_volume{0x65216};
inline constexpr Var<uint32_t> music_volume2{0x5a960};

// ---- misc flags (meaning not yet known) -----------------------------------
inline constexpr Var<uint8_t> title_flag_5ab47{0x5aacf};
inline constexpr Var<uint8_t> title_flag_5ab46{0x5aace};

// ---- strings in the data hunk -------------------------------------------
constexpr uint32_t str_graphics = 0x2a3b8;
constexpr uint32_t str_splsh = 0x2a3c9;
constexpr uint32_t str_inst5 = 0x2a3d7;
constexpr uint32_t str_frnt5 = 0x2a3e5;
constexpr uint32_t str_intuition = 0x2a3f3;
constexpr uint32_t str_config_w = 0x2a436;   // "df0:config" (write)
constexpr uint32_t str_config_r = 0x2a441;   // "df0:config" (read)
constexpr uint32_t str_config_u = 0x2a44c;   // "df0:config" (update)

}  // namespace native::g
