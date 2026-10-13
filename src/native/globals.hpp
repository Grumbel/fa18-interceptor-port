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

// ---- strings in the data hunk -------------------------------------------
constexpr uint32_t str_config_w = 0x2a436;   // "df0:config" (write)
constexpr uint32_t str_config_r = 0x2a441;   // "df0:config" (read)
constexpr uint32_t str_config_u = 0x2a44c;   // "df0:config" (update)

}  // namespace native::g
