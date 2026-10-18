#include "common.h"
#include "mw_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <print>
#include <string>
#include <utility>
#include <vector>

#include "actionchara.hpp"
#include "asset_directory.hpp"
#include "dataread_port.hpp"
#include "mainloop.hpp"
#include "menu_mouse_item.hpp"
#include "menu_world.hpp"
#include "menuchr.hpp"
#include "menudraw.hpp"
#include "menudraw_port.hpp"
#include "menumain.hpp"
#include "mg_drawprim.hpp"
#include "mg_drawprim_port.hpp"
#include "mg_frame.hpp"
#include "mg_memory.hpp"
#include "mg_texture.hpp"
#include "mg_texture_port.hpp"
#include "mglib.hpp"
#include "mglib_port.hpp"
#include "platform/model_assets.hpp"
#include "platform/texture_bundle.hpp"
#include "stubs/menudraw.hpp"
#include "userdata.hpp"

PORT_OVERRIDE void MenuReloadCLUT(int index) {
    MgTextureSetDrawVariant(MenuCharaChangeBase_Tex, index == 0 ? MenuCharaChangeCLUT_Tex : nullptr);
}

PORT_OVERRIDE void DrawMenuFillBox(int alpha, int red, int green, int blue) {
    if (!menuworld::FillWithBackdrop()) {
        PortRetailDrawMenuFillBox(alpha, red, green, blue);
    }
}

PORT_OVERRIDE void DrawMenuWakuRect(mgCTexture *texture, mgRect<float> rect, mgRect<int> uv,
                                    int alpha, int red, int green, int blue) {
    if (texture == nullptr || uv.right <= 0) {
        return;
    }
    // The frame's many clipped pieces move together, or a frame between two spread panels comes apart.
    std::optional<gfx::UiAnchorScope> anchor;
    if (const auto selection = menuworld::SelectionAnchor()) {
        anchor.emplace(*selection);
    }
    const mgRect<int> outer(static_cast<int>(rect.left), static_cast<int>(rect.top),
                            static_cast<int>(rect.left + rect.right + uv.bottom),
                            static_cast<int>(rect.top + rect.bottom + uv.right));
    auto              scissor = outer;
    MenuClipRectCheck(scissor);
    if (scissor.right <= 1) {
        return;
    }
    auto *prim = GetMenuPrim();
    SetMenuScissor(scissor);
    SetSpriteEnv(prim, MENU_SPRITE_ENV_ADD);
    prim->Begin(MG_PRIM_SPRITE);
    prim->Texture(texture);
    prim->Color(red, green, blue, alpha);
    float     sign = 1.0f;
    const int horizontal_count = static_cast<int>(3.0f + rect.right / uv.right);
    for (int edge = 0; edge < 2; ++edge) {
        float       x = (edge == 0 ? outer.right - 3 : outer.left) + PortMenuWakuPosition[0] * sign;
        const float y = edge == 0 ? outer.top - 5 : outer.bottom - 8;
        for (int tile = 0; tile < horizontal_count; ++tile) {
            prim->TextureCrd(uv.left, uv.top);
            prim->Vertex(x, y, 0.0f);
            prim->TextureCrd(uv.left + uv.right, uv.top + uv.bottom);
            prim->Vertex(x + uv.right, y + uv.bottom, 0.0f);
            x -= uv.right * sign;
        }
        sign = -sign;
    }
    prim->End();

    SetMenuScissor({0, scissor.top + 1, mgScreenWidth - 1, scissor.bottom - 1});
    SetSpriteEnv(prim, MENU_SPRITE_ENV_ADD);
    prim->Begin(MG_PRIM_TRIANGLE_FAN);
    // Initialize clears the builder descriptor. Retail retains TEX0/RGBAQ in the GS;
    // explicitly bind both again for the rotated bands on the host.
    prim->Texture(texture);
    prim->Color(red, green, blue, alpha);
    const int vertical_count = static_cast<int>(3.0f + rect.bottom / uv.right);
    for (int edge = 0; edge < 2; ++edge) {
        const float x = edge == 0 ? outer.left - 5 : outer.right - 8;
        float       y = (edge == 0 ? outer.top - 13 : outer.bottom) + PortMenuWakuPosition[1] * sign;
        for (int tile = 0; tile < vertical_count; ++tile) {
            prim->TextureCrd(uv.left, uv.top + uv.bottom);
            prim->Vertex(x, y, 0.0f);
            prim->TextureCrd(uv.left, uv.top);
            prim->Vertex(x + uv.bottom, y, 0.0f);
            prim->TextureCrd(uv.left + uv.right, uv.top);
            prim->Vertex(x + uv.bottom, y + uv.right, 0.0f);
            prim->TextureCrd(uv.left + uv.right, uv.top + uv.bottom);
            prim->Vertex(x, y + uv.right, 0.0f);
            prim->Flush();
            y += uv.right * sign;
        }
        sign = -sign;
    }
    prim->End();
    ResetMenuScissor();
}

PORT_OVERRIDE int MenuCapture(int block, mgCMemory *stack, int draw) {
    stack->Align64();
    u8       *dst = stack->stack_bytes + (stack->stack_used << 4);
    const int half_w = mgScreenWidth >> 1;
    const int half_h = mgScreenHeight >> 1;
    MenuBGTextureBlock = block;

    static char frame_name[] = "menuwork2";
    mgTexManager.DeleteBlock(block);
    MenuFrameTex = mgTexManager.EnterTexture(block, frame_name, nullptr, (half_w + 63) / 64 * 64, (half_h + 63) / 64 * 64,
                                             32, nullptr, 0, 0);

    if (MenuFrameTex != nullptr) {
        MenuFrameTex->tex0.bits.tcc = 0;
        MenuFrameTex->image[0] = reinterpret_cast<u_long128 *>(dst);
        stack->Alloc(half_w * half_h * 4 / 16 + 1);
    }

    // The grab costs a frame on PS2 too: the tick that just ended is gfx::kPreviousFrame in the next one.
    mgEndFrame(nullptr);
    mgBeginFrame(nullptr);

    const gfx::TextureHandle source = menuworld::TakeMenuGrab();
    if (MenuFrameTex != nullptr) {
        // Frame target: the window's sides past the logical frame are copied in and carried by the full-frame draw.
        const gfx::TextureHandle target = mgPortRenderTarget(MenuFrameTex, true);
        // Registered before the copy: until a target exists under its TBP0 the texture binds its stack image.
        // Linear at 2:1 samples each 2x2 block's midpoint, which is the box filter retail ran on the CPU.
        gfx::BlitTexture(source, gfx::Rect{.x = 0, .y = 0, .w = mgScreenWidth, .h = mgScreenHeight}, target,
                         gfx::Rect{.x = 0, .y = 0, .w = half_w, .h = half_h}, gfx::Filter::Linear);
    }

    if (draw != 0 && MenuFrameTex != nullptr) {
        mgTexManager.ReloadTexture(block, static_cast<sceVif1Packet *>(nullptr));
        auto *prim = GetMenuPrim();
        prim->Initialize(nullptr, nullptr);
        prim->TextureMapEnable(1);
        prim->Bilinear(1);
        prim->AlphaTestEnable(0);
        prim->DepthTestEnable(0);
        prim->ZMask(MG_Z_MASK_MASKED);
        prim->Begin(MG_PRIM_SPRITE);
        prim->Texture(MenuFrameTex);
        prim->Color(0x80, 0x80, 0x80, 0x80);
        prim->TextureCrd(0, 0);
        prim->Vertex(0, 0, 0);
        prim->TextureCrd(half_w, half_h);
        prim->Vertex(mgScreenWidth, mgScreenHeight, 0);
        prim->End();
    }

    return true;
}

// Move by whole tiles so integer sprite coordinates cover the visible leading edge.
float MenuTilePatternFirst(float phase, int size, float minimum) {
    if (size <= 0) {
        return phase;
    }
    return phase - std::ceil((phase - minimum) / size) * size;
}

// Count of grid positions first, first + size, ... that do not pass limit.
int MenuTilePatternCount(float first, int size, float limit) {
    if (size <= 0) {
        return 0;
    }
    int count = 0;
    for (float pos = first; pos <= limit; pos += static_cast<float>(size)) {
        ++count;
    }
    return count;
}

PORT_OVERRIDE void DrawMenuTilePattern(mgCDrawPrim *prim, mgCTexture *tex, float x, float y, mgRect<int> tex_rect,
                                       int /*unused*/, u8 *rgba) {
    if (tex_rect.right <= 0 || tex_rect.bottom <= 0) {
        return;
    }

    const auto extent = mgPortVisibleExtent();
    // One logical pixel protects negative fractional origins from truncation toward zero.
    const float column_first = MenuTilePatternFirst(x, tex_rect.right, std::floor(extent.left) - 1.0f);
    const float row_first = MenuTilePatternFirst(y - tex_rect.bottom, tex_rect.bottom, std::floor(extent.top) - 1.0f);
    const int   column_count = MenuTilePatternCount(column_first, tex_rect.right, std::ceil(extent.right));
    const int   row_count = MenuTilePatternCount(row_first, tex_rect.bottom, std::ceil(extent.bottom));

    SetSpriteEnv(prim, MENU_SPRITE_ENV_TEXTURE);
    prim->Begin(MG_PRIM_SPRITE);
    prim->Texture(tex);

    if (rgba != nullptr) {
        prim->Color(rgba[0], rgba[1], rgba[2], rgba[3]);
    } else {
        prim->Color(0x80, 0x80, 0x80, 0x80);
    }

    mgRect<int> dest(0, 0, 0, 0);
    for (int column = 0; column < column_count; ++column) {
        dest.Set(static_cast<int>(column_first + static_cast<float>(column * tex_rect.right)), static_cast<int>(row_first),
                 tex_rect.right, tex_rect.bottom);

        for (int row = 0; row < row_count; ++row) {
            PrimQuad(prim, dest, tex_rect);
            dest.top += dest.bottom;
        }
    }

    prim->End();
}

namespace {

bool MenuPanelOutsideFrame(const CMenuPosDataForm &form) {
    if (form.dtype != MENUFORM_DTYPE_NORMAL && form.dtype != MENUFORM_DTYPE_LIST) {
        return false;
    }
    const auto extent = mgPortVisibleExtent();
    if (extent.left >= 0 && extent.right <= mgScreenWidth) {
        return false;
    }

    float left = std::numeric_limits<float>::infinity();
    float right = -left;
    for (int i = 0; i < form.parts_num; ++i) {
        const auto &part = form.parts[i];
        if (!part.active || !part.draw_flag || part.dtype == MENUFORMPARTS_DTYPE_FUNCINFO) {
            continue;
        }
        // Compositors and special drawing routines do not have panel bounds.
        // Preserve their drawing and side effects when an extent is unknown.
        if (part.w >= mgScreenWidth && part.h >= mgScreenHeight) {
            return false;
        }
        float x0 = part.x;
        float x1 = part.x + part.w;
        switch (part.dtype) {
            case MENUFORMPARTS_DTYPE_NORMAL:
            case MENUFORMPARTS_DTYPE_SQ_BETA:
            case MENUFORMPARTS_DTYPE_TRS:
                break;
            case MENUFORMPARTS_DTYPE_NORMAL2:
                x0 += std::min(0, part.etc_info[0]);
                x1 += std::max(0, part.etc_info[0]);
                break;
            case MENUFORMPARTS_DTYPE_NUMBER1:
            case MENUFORMPARTS_DTYPE_NUMBER2: {
                if (!MenuPosData) {
                    return false;
                }
                const auto *info = MenuPosData->GetTexGetInfo(part.tex_info_no);
                if (!info) {
                    return false;
                }
                int digits = 1;
                for (int value = part.etc_info[1]; value >= 10; value /= 10) {
                    ++digits;
                }
                if (part.dtype == MENUFORMPARTS_DTYPE_NUMBER2) {
                    digits = std::max(digits, part.etc_info[0]);
                }
                // Conservative for all three alignments, including negative spacing.
                const float span = digits * std::abs(info->rect.right + part.w) + info->rect.right;
                x0 -= span;
                x1 = part.x + span;
                break;
            }
            default:
                return false;
        }
        // Item-icon effects use their own storage layout in DrawOneItem.
        if (part.dtype != MENUFORMPARTS_DTYPE_SQ_BETA && part.dtype != MENUFORMPARTS_DTYPE_TRS) {
            for (int effect = 0; part.effect && std::cmp_less(effect, part.effect_num); ++effect) {
                const int type = part.effect[effect].type;
                if (type != 0 && type != MENU_PARTS_EFFECT_FILLBOX && type != MENU_PARTS_EFFECT_BLINK) {
                    return false;
                }
            }
        }
        const float sway = part.vibe_cnt[0] ? part.viber[0] + 1.0f : 0.0f;
        const float shadow = part.shadow ? std::abs(static_cast<int>(part.shadow_offset)) : 0;
        left = std::min(left, std::min(x0, x1) - sway - shadow);
        right = std::max(right, std::max(x0, x1) + sway + shadow);
    }
    const float sway = form.vibe_cnt[0] ? 10.0f : 0.0f;
    return std::isfinite(left) && (fptosi(form.x) + right + sway <= 0 ||
                                   fptosi(form.x) + left - sway >= mgScreenWidth);
}

} // namespace

PORT_OVERRIDE void CMenuPosDataForm::MenuFormDraw(int &state) {
    // Retail parks unused panels wholly outside its logical frame (SPECHECK,
    // for example, at x=-260). Hide that panel, not the entire page's flanks.
    // As soon as it overlaps the frame, draw its complete entering geometry.
    if (MenuPanelOutsideFrame(*this) || menuworld::HidesForm(*this)) {
        return;
    }
    // An item the mouse carries is drawn at the pointer, unmoved by the spread panels.
    float held_x = 0.0f;
    float held_y = 0.0f;
    if (menuworld::ItemBackdrop() && this == MenuCommonInfo->cursor_form && MenuMouseHeldItemAt(held_x, held_y)) {
        float tip_x = 0.0f;
        float tip_y = 0.0f;
        GetPutPosXY(const_cast<char *>("cursor0"), tip_x, tip_y);
        const gfx::UiAnchorScope unanchored(gfx::UiAnchor{});
        int                      draw_x = fptosi(x + held_x - 26.0f - tip_x);
        int                      draw_y = fptosi(y + held_y - 14.0f - tip_y);
        MenuFormDraw(draw_x, draw_y, state);
        return;
    }
    std::optional<gfx::UiAnchorScope> anchor;
    if (const auto spread = menuworld::FormAnchor(*this)) {
        anchor.emplace(*spread);
    }
    int draw_x = fptosi(x);
    int draw_y = fptosi(y);
    MenuFormDraw(draw_x, draw_y, state);
}

PORT_OVERRIDE void MenuCursorDraw(mgCTexture *texture, float *position, float rotation,
                                  int reverse, int alpha, float scale) {
    if (!texture) {
        return;
    }
    const auto hand_key = MenuMouseNoteHand(mgPortBinding(texture),
                                            {.x = static_cast<float>(menu_long_hand.left), .y = static_cast<float>(menu_long_hand.top), .w = static_cast<float>(menu_long_hand.right), .h = static_cast<float>(menu_long_hand.bottom)},
                                            rotation, alpha, scale);
    // The canonical image retains the selection hand. A displayed mouse pointer
    // replaces this tagged group without changing screenshots or grab textures.
    gfx::Interp2DScope tag({.key = hand_key, .generation = static_cast<uint64_t>(mgPortFrameCount())});
    auto              *prim = GetMenuPrim();
    SetSpriteEnv(prim, MENU_SPRITE_ENV_TEXTURE);
    prim->Bilinear(1);
    prim->Begin(MG_PRIM_TRIANGLE_STRIP);
    prim->Texture(texture);
    prim->Color(128, 128, 128, alpha);
    const float c = std::cos(rotation);
    const float s = std::sin(rotation);
    for (int i = 0; i < 4; ++i) {
        const float x = (i & 1 ? 40 : 0) * scale * (reverse ? -1 : 1);
        const float y = (i & 2 ? 24 : 0) * scale;
        prim->TextureCrd(menu_long_hand.left + (i & 1 ? menu_long_hand.right : 0),
                         menu_long_hand.top + (i & 2 ? menu_long_hand.bottom : 0));
        const float px = i == 0 ? 3.0f + position[0] : 3.0f + (position[0] + (x * c - y * s));
        const float py = i == 0 ? 3.0f + position[1] : i & 1 ? position[1] + (x * s + y * c)
                                                             : 3.0f + (position[1] + (x * s + y * c));
        prim->Vertex(px, py, 0.0f);
    }
    prim->End();
}

namespace {

// Lattice spacing in board units, and how much a patch magnifies the slot interior.
// The patch reach (0.8 of the spacing, see Ramp) stays inside the slot's frame lines.
constexpr float kCellX = 24.0f;
constexpr float kCellY = 30.0f;
constexpr float kMagnify = 1.3f;
// Overall tone, so the flattened surface keeps the slot's average brightness.
constexpr float kTone = 1.05f;
// Fraction of each cell edge held by a single patch before the blend starts.
constexpr float kRamp = 0.2f;
// Inset of the sampled patch from the slot's frame lines, in texels.
constexpr int kKomaInset = 3;
constexpr int kBoardColumns = 6;
constexpr int kBoardRows = 26;
constexpr int kSlotW = 40;
constexpr int kSlotH = 50;

/**
 * Hashes a lattice vertex into a well-mixed 32-bit value.
 */
constexpr uint32_t Hash(int x, int y) {
    uint32_t h = static_cast<uint32_t>(x) * 0x8DA6B343u ^ static_cast<uint32_t>(y) * 0xD8163841u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

/**
 * Returns a pseudo-random value in [-1, 1] for a lattice vertex.
 */
constexpr float Lattice(int x, int y) {
    return static_cast<float>(Hash(x, y + 0x1000) & 0xFFFF) / 32767.5f - 1.0f;
}

/**
 * Returns smooth value noise in [-1, 1] at a fractional lattice position.
 */
float ValueNoise(float x, float y) {
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const int   ix = static_cast<int>(fx);
    const int   iy = static_cast<int>(fy);
    const float tx = (x - fx) * (x - fx) * (3.0f - 2.0f * (x - fx));
    const float ty = (y - fy) * (y - fy) * (3.0f - 2.0f * (y - fy));
    const float top = Lattice(ix, iy) + (Lattice(ix + 1, iy) - Lattice(ix, iy)) * tx;
    const float bottom = Lattice(ix, iy + 1) + (Lattice(ix + 1, iy + 1) - Lattice(ix, iy + 1)) * tx;
    return top + (bottom - top) * ty;
}

/**
 * Returns the low-frequency brightness factor at a board position.
 */
float Macro(float x, float y) {
    // About +-4%, varying over a few slots.
    const float col = x / kSlotW;
    const float row = y / kSlotH;
    const float n = 0.7f * ValueNoise(col * 0.38f + 11.3f, row * 0.42f + 3.7f) +
                    0.3f * ValueNoise(col * 0.9f + 47.1f, row * 0.9f + 29.5f);
    return 1.0f + 0.04f * n;
}

/**
 * Returns the slot art's brightness at a texel, relative to its mean.
 */
constexpr float SlotShading(float u, float v) {
    // Least-squares cubic of the slot's brightness; (u, v) are texels from its top-left corner.
    const float x = (u - 20.0f) / 20.0f;
    const float y = (v - 25.0f) / 25.0f;
    return 0.9156f + y * (0.2082f + y * (0.2006f - 0.2238f * y)) + x * (0.1266f + 0.0763f * y - 0.0289f * y * y) +
           x * x * (0.1747f - 0.0056f * y) - 0.1412f * x * x * x;
}

/**
 * Maps a fraction across a lattice cell to the blend weight of its far corner.
 */
constexpr float Ramp(float t) {
    // Flat near each corner, so most of the cell keeps one patch's contrast.
    return std::clamp((t - kRamp) / (1.0f - 2.0f * kRamp), 0.0f, 1.0f);
}

/**
 * Scales one colour channel by a factor and clamps the result to a byte.
 */
uint8_t Channel(int channel, float factor) {
    const int value = static_cast<int>(std::lround(static_cast<float>(channel) * factor));
    return static_cast<uint8_t>(std::clamp(value, 0, 0xFF));
}

/**
 * Placement, visible rows and tint of the item board being drawn.
 */
struct Board {
    float       left = 0;        /**< Left edge of the board, in screen units. */
    float       top = 0;         /**< Top edge of the board, in screen units. */
    float       right = 0;       /**< Right edge of the board, in screen units. */
    float       bottom = 0;      /**< Bottom edge of the board, in screen units. */
    float       clip_top = 0;    /**< Top of the visible rows, in screen units. */
    float       clip_bottom = 0; /**< Bottom of the visible rows, in screen units. */
    mgRect<int> koma;            /**< Slot interior that the patches sample, in texels. */
    int         r = 0;           /**< Red tint channel. */
    int         g = 0;           /**< Green tint channel. */
    int         b = 0;           /**< Blue tint channel. */
    int         a = 0;           /**< Opacity. */
};

/**
 * Appends the triangles that carry one corner's patch over every lattice cell in view.
 */
void AppendPass(const mgCDrawPrim &prim, const Board &board, int dx, int dy, std::vector<gfx::Vertex2D> &out) {
    const float centre_u = board.koma.left + board.koma.right * 0.5f;
    const float centre_v = board.koma.top + board.koma.bottom * 0.5f;
    const int   first_row = static_cast<int>(std::floor((board.clip_top - board.top) / kCellY));
    const int   last_row = static_cast<int>(std::ceil((board.clip_bottom - board.top) / kCellY));
    const int   columns = static_cast<int>(std::ceil((board.right - board.left) / kCellX));
    // The ramp's corners, plus enough steps for the shading correction to follow its curve.
    const std::array<float, 6> knots{0.0f, kRamp, 0.4f, 0.6f, 1.0f - kRamp, 1.0f};

    for (int cy = std::max(first_row, 0); cy < last_row; cy++) {
        for (int cx = 0; cx < columns; cx++) {
            const uint32_t variant = Hash(cx + dx, cy + dy) >> 9;
            const float    su = (variant & 1) ? -1.0f : 1.0f;
            const float    sv = (variant & 2) ? -1.0f : 1.0f;
            const float    origin_x = board.left + cx * kCellX;
            const float    origin_y = board.top + cy * kCellY;
            const float    corner_x = origin_x + dx * kCellX;
            const float    corner_y = origin_y + dy * kCellY;

            auto vertex = [&](float x, float y) {
                const float                  tx = (x - origin_x) / kCellX;
                const float                  ty = (y - origin_y) / kCellY;
                const float                  wx = dx ? Ramp(tx) : 1.0f - Ramp(tx);
                const float                  wy = dy ? Ramp(ty) : 1.0f - Ramp(ty);
                const float                  du = su * (x - corner_x) / kMagnify;
                const float                  dv = sv * (y - corner_y) / kMagnify;
                const float                  f = wx * wy * kTone * Macro(x - board.left, y - board.top) / SlotShading(20.0f + du, 25.0f + dv);
                const std::array<uint8_t, 4> rgba{Channel(board.r, f), Channel(board.g, f), Channel(board.b, f),
                                                  static_cast<uint8_t>(board.a)};
                return mgPortPrimVertex(prim, x, y, 0.0f, centre_u + du, centre_v + dv, rgba.data());
            };

            for (std::size_t j = 0; j + 1 < knots.size(); j++) {
                const float y0 = std::max({origin_y + knots[j] * kCellY, board.top, board.clip_top});
                const float y1 = std::min({origin_y + knots[j + 1] * kCellY, board.bottom, board.clip_bottom});
                if (y1 <= y0) {
                    continue;
                }
                for (std::size_t i = 0; i + 1 < knots.size(); i++) {
                    const float x0 = std::max(origin_x + knots[i] * kCellX, board.left);
                    const float x1 = std::min(origin_x + knots[i + 1] * kCellX, board.right);
                    if (x1 <= x0) {
                        continue;
                    }
                    const gfx::Vertex2D top_left = vertex(x0, y0);
                    const gfx::Vertex2D top_right = vertex(x1, y0);
                    const gfx::Vertex2D bottom_right = vertex(x1, y1);
                    const gfx::Vertex2D bottom_left = vertex(x0, y1);
                    out.insert(out.end(), {top_left, top_right, bottom_right, top_left, bottom_right, bottom_left});
                }
            }
        }
    }
}

} // namespace

/**
 * Draws the item board as one continuous surface rather than a grid of identical slots.
 * The surface is built by blending mirrored patches of the slot interior, placed on a
 * lattice that ignores the slot grid. The lattice is keyed to board coordinates, so the
 * surface scrolls with the board. The capacity line under the last usable row is kept.
 */
PORT_OVERRIDE void MenuItemBrdDraw(float *pos, mgRect<int> clip_rect, int &tex_block, int a, int r, int g, int b) {
    mgCDrawPrim *prim = GetMenuPrim();
    mgCTexture  *tex = mgTexManager.GetTexture(const_cast<char *>("mt0"), -1);

    if (tex == nullptr || mgScreenWidth < pos[0]) {
        return;
    }

    mgRect<int> scissor = clip_rect;
    MenuClipRectCheck(scissor);
    MenuReloadTexture(tex_block, tex->block);

    // The slot's interior, inside its frame lines.
    const mgRect<int> koma = MenuItemBrdKomaRect;
    Board             board{};
    board.left = static_cast<float>(static_cast<int>(pos[0]));
    board.top = static_cast<float>(static_cast<int>(pos[1]));
    board.right = std::min(board.left + kBoardColumns * kSlotW, static_cast<float>(mgScreenWidth) + kSlotW);
    board.bottom = board.top + kBoardRows * kSlotH;
    board.clip_top = static_cast<float>(scissor.top);
    board.clip_bottom = static_cast<float>(scissor.bottom) + 1.0f;
    board.koma = mgRect<int>(koma.left + kKomaInset, koma.top + kKomaInset, koma.right - 2 * kKomaInset,
                             koma.bottom - 2 * kKomaInset);
    board.r = r;
    board.g = g;
    board.b = b;
    board.a = a;

    SetSpriteEnv(prim, MENU_SPRITE_ENV_TEXTURE);
    prim->Shading(1);
    prim->Direct(SCE_GS_SCISSOR_1, scissor.left | (static_cast<s64>(scissor.right) << 16) |
                                       (static_cast<s64>(scissor.top) << 32) | (static_cast<s64>(scissor.bottom) << 48));
    std::vector<gfx::Vertex2D> triangles;

    // The first corner's patch replaces what is underneath; the other three add on top.
    for (int corner = 0; corner < 4; corner++) {
        prim->AlphaBlend(corner == 0 ? MG_ALPHA_BLEND_NONE : MG_ALPHA_BLEND_ADD_FULL);
        prim->Begin(MG_PRIM_SPRITE);
        prim->Texture(tex);
        prim->Color(r, g, b, a);
        triangles.clear();
        AppendPass(*prim, board, corner & 1, corner >> 1, triangles);
        mgPortPrimSubmit(*prim, gfx::Primitive::Triangles, triangles);
        prim->End();
    }

    prim->AlphaBlend(MG_ALPHA_BLEND_NORMAL);

    // Capacity line under the last usable row.
    const int last_row = GetNowBagMax(ITEM_BOARD_MAIN) / kBoardColumns;
    const int line_top = static_cast<int>(pos[1]) + last_row * kSlotH;

    if (last_row < kBoardRows && OmakeFlag == 0 && line_top <= clip_rect.bottom) {
        const int line_x0 = static_cast<int>(pos[0] - 2.0f);
        const int line_x1 = static_cast<int>(pos[0] + static_cast<float>(kSlotW * kBoardColumns));
        const int line_y = line_top - 1;
        prim->TextureMapEnable(0);
        prim->Bilinear(1);
        prim->Begin(MG_PRIM_SPRITE);
        prim->Color(0xDE, 0xDE, 0xDE, a);
        prim->Vertex(line_x0, line_y, 0);
        prim->Vertex(line_x1, line_y + 1, 0);
        prim->End();
        prim->Begin(MG_PRIM_SPRITE);
        prim->Color(0xAC, 0xAC, 0xAC, a * 3 / 5);
        prim->Vertex(line_x0, line_y + 2, 0);
        prim->Vertex(line_x1, line_y + 3, 0);
        prim->End();
        prim->Begin(MG_PRIM_SPRITE);
        prim->Color(0x80, 0x80, 0x80, a / 5);
        prim->Vertex(line_x0, line_y + 4, 0);
        prim->Vertex(line_x1, line_y + 5, 0);
        prim->End();
        prim->TextureMapEnable(1);
        prim->Bilinear(0);
    }

    prim->Shading(0);
    prim->Direct(SCE_GS_SCISSOR_1, (static_cast<s64>(mgScreenWidth - 1) << 16) | (static_cast<s64>(mgScreenHeight - 1) << 48));
}

namespace {
/**
 * Selects the repair model by its actual bundle or ordinary asset-directory filename.
 */
std::optional<std::filesystem::path> RepairModelPath(const void *data) {
    const auto path = DataReadBufferPath(data);
    if (path && std::filesystem::path(*path).extension() == ".gltf") {
        return std::filesystem::path(*path).replace_extension(".assets") / "repair_powder.gltf";
    }
    if (const auto directory = AssetDirectoryPath(data)) {
        return std::filesystem::path(*directory) / "repair_powder.gltf";
    }
    return std::nullopt;
}

/**
 * Enters the repair sprites from an embedded native set or an ordinary texture document.
 */
bool EnterRepairTexture(const void *data, int block) {
    const auto path = DataReadBufferPath(data);
    if (!path) {
        return false;
    }
    std::strcpy(mgTexManager.name_suffix, "_2");
    bool loaded = false;
    if (std::filesystem::path(*path).extension() == ".gltf") {
        std::string error;
        loaded = texture_bundle::Enter(*path, "menueff0", mgTexManager, block, nullptr, error);
        if (!loaded) {
            std::println(stderr, "repair texture: {}", error);
        }
    } else if (const auto directory = AssetDirectoryPath(data)) {
        MgTextureEnterDocument(&mgTexManager, *directory + "/repair_powder2d.json", block, nullptr, nullptr);
        char texture_name[] = "menueff0";
        loaded = mgTexManager.GetTexture(texture_name, block) != nullptr;
    }
    mgTexManager.name_suffix[0] = 0;
    return loaded;
}
} // namespace

PORT_OVERRIDE void CRepairManager::CheckDataBG(int block) {
    if (data_ready) {
        return;
    }
    mgTexManager.DeleteBlock(block);
    tex_block = static_cast<s16>(block);
    tex = nullptr;
    if (!EnterRepairTexture(data, block)) {
        bg_load = false;
        return;
    }
    char texture_name[] = "menueff0_2";
    tex = mgTexManager.GetTexture(texture_name, -1);
    data_ready = tex != nullptr;
    bg_load = false;
}

PORT_OVERRIDE void CRepairManager::SetRepairData(mgCMemory *memory, int block, unsigned int *pack) {
    if (!memory) {
        return;
    }
    tex_block = static_cast<s16>(block);
    data = pack;
    tex = nullptr;
    data_ready = false;
    bg_load = false;
    keep = false;
    if (!EnterRepairTexture(data, block)) {
        return;
    }
    char texture_name[] = "menueff0_2";
    tex = mgTexManager.GetTexture(texture_name, -1);
    data_ready = tex != nullptr;
    if (data_ready) {
        SetStack(memory, 1);
        keep = true;
    }
}

PORT_OVERRIDE void CRepairManager::GeneratePoly(float *position, int block) {
    const auto path = RepairModelPath(data);
    if (!path || !position || !model_stack.stack) {
        return;
    }
    if (model) {
        model_assets::DeleteCharacterTextures(*model, 0, CHARA_IMAGE_MAX - 1);
    }
    model = nullptr;
    model_stack.stack_used = 0;
    model_stack.lock = 0;
    auto *character = new (model_stack.Alloc(static_cast<int>((sizeof(CActionChara) + 15) / 16 + 2))) CActionChara;
    model_assets::DeleteCharacterTextures(*character, 0, CHARA_IMAGE_MAX - 1);
    character->Initialize(nullptr);
    model_assets::CharacterLoadOptions options{.model_memory = &model_stack, .motion_memory = &model_stack, .image_memory = &model_stack, .textures = &mgTexManager, .parent = nullptr, .texture_block = block};
    std::string                        error;
    if (!model_assets::LoadCharacter(*character, *path, "info", options, error)) {
        model_assets::DeleteCharacterTextures(*character, 0, CHARA_IMAGE_MAX - 1);
        model_stack.stack_used = 0;
        model_stack.lock = 0;
        std::println(stderr, "repair model: {}", error);
        return;
    }
    model = character;
    model->SetScale(0.4f, 0.4f, 0.4f);
    model->SetPosition(position);
    model->SetMotion(const_cast<char *>("\x94\xad\x93\xae"), 0, 1);
    model->SetFadeFlag(1);
    model->Show(0, 1);
    auto *frame = model->CObjectFrame::frame;
    if (frame && frame->attr) {
        frame->attr->z_test = MG_DEPTH_TEST_ALWAYS;
        frame->SetAttrParam(*frame->attr, 1, MG_FRAME_ATTR_Z_TEST);
    }
    model->Show(1, 1);
    model_counter = 0;
}
