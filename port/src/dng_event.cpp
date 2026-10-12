#include "common.h"

#include <cmath>

#include "dng_event.hpp"

// Retail's definition returns void against the fixed-up declaration (scripts/port/header_fixups.py).
PORT_OVERRIDE int CRedMarkModel::Draw() {
    float position[4];
    float saved_position[4];

    if (draw_request != 0) {
        GetPosition(position);
        GetPosition(saved_position);
        position[1] += 2.0f * sinf(angle);
        SetPosition(position);
        CObjectFrame::DrawDirect();
        SetPosition(saved_position);
        draw_request = false;
    }

    return 0;
}
