#include "common.h"

#include <vector>

#include "menudraw.hpp"
#include "mg_drawprim.hpp"
#include "mg_drawprim_port.hpp"
#include "mg_texture.hpp"
#include "mglib.hpp"
#include "mglib_port.hpp"

// Host specializations of the rectangle helper declared by menudraw.hpp.
template <>
void PrimQuad<float>(mgCDrawPrim *prim, mgRect<float> put_rect, mgRect<int> tex_rect) {
    PrimQuad_f_(prim, mgRect_f_{put_rect.left, put_rect.top, put_rect.right, put_rect.bottom},
                mgRect_i_{tex_rect.left, tex_rect.top, tex_rect.right, tex_rect.bottom});
}

template <>
void PrimQuad<int>(mgCDrawPrim *prim, mgRect<int> put_rect, mgRect<int> tex_rect) {
    static char frame_name[] = "bg16";
    auto *frame = prim != nullptr && prim->prim.TME != 0 &&
                  tex_rect.left == 0 && tex_rect.top == 0 && tex_rect.right == 704 && tex_rect.bottom == 416
                      ? mgTexManager.GetTexture(frame_name, -1) : nullptr;
    if (prim != nullptr && frame != nullptr && prim->texture.tex0_bits == frame->tex0_bits) {
        // The decorative menu frame has authored coordinates (704 pixels wide,
        // sliding left by 192 on the item page). Its circle must stay with the
        // model camera and scissors. Submit triangles so the renderer's full-
        // frame sprite cover rule does not stretch it to the window boundaries.
        uint8_t color[4];
        mgPortPrimColor(*prim, color);
        std::vector<gfx::Vertex2D> vertices;
        const int corners[6][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
        const auto append = [&](float left, float top, float right, float bottom,
                                float u0, float v0, float u1, float v1) {
            for (const auto &corner : corners) {
                vertices.push_back(mgPortPrimVertex(*prim,
                    corner[0] ? right : left, corner[1] ? bottom : top, 0,
                    corner[0] ? u1 : u0, corner[1] ? v1 : v0, color));
            }
        };
        const float left = put_rect.left, top = put_rect.top;
        const float right = left + put_rect.right, bottom = top + put_rect.bottom;
        const float u0 = tex_rect.left, v0 = tex_rect.top;
        const float u1 = u0 + tex_rect.right, v1 = v0 + tex_rect.bottom;
        append(left, top, right, bottom, u0, v0, u1, v1);
        // Fill wide flanks with the edge texel, without moving the lens or
        // extending a frame that is still entering/leaving the logical screen.
        if (left <= 0 && right >= mgScreenWidth && top <= 0 && bottom >= mgScreenHeight) {
            const auto extent = mgPortVisibleExtent();
            if (extent.left < left) append(extent.left, top, left, bottom, u0 + 0.5f, v0, u0 + 0.5f, v1);
            if (extent.right > right) append(right, top, extent.right, bottom, u1 - 0.5f, v0, u1 - 0.5f, v1);
        }
        mgPortPrimSubmit(*prim, gfx::Primitive::Triangles, vertices);
        return;
    }
    PrimQuad_i_(prim, mgRect_i_{put_rect.left, put_rect.top, put_rect.right, put_rect.bottom},
                mgRect_i_{tex_rect.left, tex_rect.top, tex_rect.right, tex_rect.bottom});
}
