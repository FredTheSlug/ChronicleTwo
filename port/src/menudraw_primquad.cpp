#include "common.h"

#include <algorithm>
#include <vector>

#include "menudraw.hpp"
#include "mg_drawprim.hpp"
#include "mg_drawprim_port.hpp"
#include "mg_texture.hpp"
#include "mglib.hpp"
#include "mglib_port.hpp"

namespace {

bool IsBackdrop(mgCDrawPrim *prim, mgRect<int> uv, char *name, int width, int height, int top = 0) {
    if (!prim || !prim->prim.TME || uv.left != 0 || uv.top != top ||
        uv.right != width || uv.bottom != height) return false;
    const auto *texture = mgTexManager.GetTexture(name, -1);
    return texture && prim->texture.tex0_bits == texture->tex0_bits;
}

void DrawBackdrop(mgCDrawPrim &prim, mgRect<float> rect, mgRect<int> uv, float artwork_width) {
    const auto extent = mgPortVisibleExtent();
    const bool wide = extent.left < 0 || extent.right > mgScreenWidth;
    uint8_t color[4];
    mgPortPrimColor(prim, color);
    std::vector<gfx::Vertex2D> vertices;
    const int corners[6][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
    const auto append = [&](float left, float right, float u0, float u1) {
        // Linear filtering must stay inside the artwork at a reflection seam;
        // otherwise its transparent padding leaves a dark hairline.
        if (wide) {
            u0 = std::clamp(u0, 0.5f, artwork_width - 0.5f);
            u1 = std::clamp(u1, 0.5f, artwork_width - 0.5f);
        }
        for (const auto &corner : corners) {
            vertices.push_back(mgPortPrimVertex(prim,
                corner[0] ? right : left, corner[1] ? rect.top + rect.bottom : rect.top, 0,
                corner[0] ? u1 : u0, corner[1] ? uv.top + uv.bottom : uv.top, color));
        }
    };
    const float left = rect.left, right = left + rect.right;
    // bg16's last four columns and chr_bg's extra source column are transparent
    // padding. At wide aspects join the reflection to the artwork, not padding.
    const float u_end = wide ? artwork_width : uv.right;
    const float span = rect.right * u_end / uv.right;
    append(left, left + span, 0, u_end);

    if (wide && rect.right > 0 && rect.bottom > 0) {
        // Continue only the edge that has reached its side of the logical frame.
        // This keeps the extension moving with a page entering or leaving it.
        // The Make form starts its artwork at x=1 inside the logical frame.
        if (left <= 2 && right > 0) {
            bool reflected = true;
            for (float edge = left; extent.left < edge; edge -= span, reflected = !reflected) {
                const float end = std::max(extent.left, edge - span);
                const float start_u = reflected ? 0 : u_end;
                const float end_u = start_u + (reflected ? 1 : -1) * (edge - end) * uv.right / rect.right;
                append(end, edge, end_u, start_u);
            }
        }
        // Retail's eased Character form settles at x=-2 rather than zero.
        if (right >= mgScreenWidth - 2 && left < mgScreenWidth) {
            bool reflected = true;
            for (float edge = left + span; extent.right > edge; edge += span, reflected = !reflected) {
                const float end = std::min(extent.right, edge + span);
                const float start_u = reflected ? u_end : 0;
                const float end_u = start_u + (reflected ? -1 : 1) * (end - edge) * uv.right / rect.right;
                append(edge, end, start_u, end_u);
            }
        }
    }
    mgPortPrimSubmit(prim, gfx::Primitive::Triangles, vertices);
}

} // namespace

// Host specializations of the rectangle helper declared by menudraw.hpp.
template <>
void PrimQuad<float>(mgCDrawPrim *prim, mgRect<float> put_rect, mgRect<int> tex_rect) {
    static char character_name[] = "chr_bg";
    static char invent_name[] = "inv_bg";
    const auto extent = mgPortVisibleExtent();
    if ((extent.left < 0 || extent.right > mgScreenWidth) &&
        (IsBackdrop(prim, tex_rect, character_name, 513, 768) ||
         IsBackdrop(prim, tex_rect, invent_name, 512, 416, 48))) {
        DrawBackdrop(*prim, put_rect, tex_rect, 512);
        return;
    }
    PrimQuad_f_(prim, mgRect_f_{put_rect.left, put_rect.top, put_rect.right, put_rect.bottom},
                mgRect_i_{tex_rect.left, tex_rect.top, tex_rect.right, tex_rect.bottom});
}

template <>
void PrimQuad<int>(mgCDrawPrim *prim, mgRect<int> put_rect, mgRect<int> tex_rect) {
    static char frame_name[] = "bg16";
    if (IsBackdrop(prim, tex_rect, frame_name, 704, 416)) {
        // Preserve the authored lens position. Full-frame sprite cover scaling
        // moves its alpha mask away from the model camera, even at 4:3.
        DrawBackdrop(*prim, mgRect<float>(put_rect.left, put_rect.top, put_rect.right, put_rect.bottom), tex_rect, 700);
        return;
    }
    PrimQuad_i_(prim, mgRect_i_{put_rect.left, put_rect.top, put_rect.right, put_rect.bottom},
                mgRect_i_{tex_rect.left, tex_rect.top, tex_rect.right, tex_rect.bottom});
}
