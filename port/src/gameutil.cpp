#include "common.h"

#include "collision.hpp"
#include "gameutil.hpp"
#include "mg_math.hpp"

PORT_OVERRIDE int CheckHitVertical(CollisionInfo *collision, float *position, float dy, float *hit, int mask) {
    if (collision == nullptr || collision->polys == nullptr || collision->count == 0) {
        return -1;
    }
    // Translation changes only y, including for callers using a non-unit w.
    float end[4] = {position[0], position[1] + dy, position[2], position[3]};
    int   best = -1;
    float best_y = 0.0f;
    for (int i = 0; i < collision->count; ++i) {
        CCPoly &poly = collision->polys[i];
        if ((poly.ignore_mask & mask) ||
            !mgIntersectionPoint_line_poly3(position, end, poly.vertex[0], poly.vertex[1],
                                            poly.vertex[2], poly.normal, hit)) {
            continue;
        }
        const bool nearer = dy <= 0.0f
                                ? !(position[1] <= hit[1]) && (best < 0 || best_y <= hit[1])
                                : position[1] < hit[1] && (best < 0 || !(best_y < hit[1]));
        if (nearer) {
            best = i;
            best_y = hit[1];
        }
    }
    if (best >= 0) {
        hit[1] = best_y;
    }
    return best;
}
