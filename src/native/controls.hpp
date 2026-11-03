// Optional analog input interception for the original 68000 flight model.
#pragma once

namespace amiga { struct Machine; }

namespace native {
void install_control_hooks(amiga::Machine& machine);
}  // namespace native
