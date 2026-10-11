#include "common.h"

#include "dng_event.hpp"

// Retail's definition returns void against the fixed-up declaration (scripts/port/header_fixups.py).
PORT_OVERRIDE int CRedMarkModel::Draw() {
    PS2_UNIMPLEMENTED();
}
