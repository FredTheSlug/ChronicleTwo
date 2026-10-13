#pragma once

// Projected anchors move smoothly during normal camera/character motion. A jump
// larger than 64 logical pixels in one tick is a presentation discontinuity.
struct ScreenOverlayAnchor {
    int  x = 0, y = 0;
    bool valid = false;

    bool Update(int next_x, int next_y) {
        const double dx = static_cast<double>(next_x) - x;
        const double dy = static_cast<double>(next_y) - y;
        const bool   cut = valid && dx * dx + dy * dy > 64.0 * 64.0;
        x = next_x;
        y = next_y;
        valid = true;
        return cut;
    }
};
