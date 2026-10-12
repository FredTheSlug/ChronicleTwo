
#include "common.h"

#include "mg_drawprim.hpp"
#include "vu0_lanes.hpp"

void mgCDrawPrim::Data0(float *data) {
    int lanes[4] = {vu0::Ftoi(data[0]), vu0::Ftoi(data[1]), vu0::Ftoi(data[2]), vu0::Ftoi(data[3])};
    Data(lanes);
}

void mgCDrawPrim::Data4(float *data) {
    int lanes[4] = {vu0::Ftoi4(data[0]), vu0::Ftoi4(data[1]), vu0::Ftoi4(data[2]), vu0::Ftoi4(data[3])};
    Data(lanes);
}

void mgCDrawPrim::Vertex(float *pos) {
    Vertex4(vu0::Ftoi4(pos[0]), vu0::Ftoi4(pos[1]), vu0::Ftoi(pos[2]));
}

void mgCDrawPrim::Color(float *color) {
    Color(vu0::Ftoi(color[0]), vu0::Ftoi(color[1]), vu0::Ftoi(color[2]), vu0::Ftoi(color[3]));
}
