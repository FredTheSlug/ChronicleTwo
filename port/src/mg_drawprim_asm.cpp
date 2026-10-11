#include "common.h"

#include <climits>
#include <cstring>

#include "mg_drawprim.hpp"

namespace {

// Matches the x86 conversion the other float-to-int paths use, since a C cast of these values is undefined.
int TruncateToInt(float value) {
    if (!(value >= -2147483648.0f && value < 2147483648.0f)) {
        return INT_MIN;
    }

    return static_cast<int>(value);
}

} // namespace

void mgCDrawPrim::Data0(float *data) {
    u_long128 *destination = write;
    write++;
    const int lanes[4] = {TruncateToInt(data[0]), TruncateToInt(data[1]), TruncateToInt(data[2]),
                          TruncateToInt(data[3])};
    memcpy(destination, lanes, sizeof(lanes));
}

void mgCDrawPrim::Data4(float *data) {
    u_long128 *destination = write;
    write++;
    const int lanes[4] = {TruncateToInt(data[0] * 16.0f), TruncateToInt(data[1] * 16.0f),
                          TruncateToInt(data[2] * 16.0f), TruncateToInt(data[3] * 16.0f)};
    memcpy(destination, lanes, sizeof(lanes));
}

void mgCDrawPrim::Vertex(float *pos) {
    Vertex4(TruncateToInt(pos[0] * 16.0f), TruncateToInt(pos[1] * 16.0f), TruncateToInt(pos[2]));
}

void mgCDrawPrim::Color(float *color) {
    Color(TruncateToInt(color[0]), TruncateToInt(color[1]), TruncateToInt(color[2]), TruncateToInt(color[3]));
}
