#include "common.h"
#include "mw_runtime.h"

#include "menudraw.hpp"
#include "menumain.hpp"
#include "mg_drawprim.hpp"
#include "mg_memory.hpp"
#include "mg_texture.hpp"
#include "mglib.hpp"
#include "mglib_port.hpp"
#include "mw_runtime.h"
#include "menu_mouse_item.hpp"

extern float PortMenuWakuPosition[2] asm("MenuDraw_WakuPosition");

PORT_OVERRIDE void CMenuPosDataForm::MenuFormDraw(int &state) {
    int draw_x = fptosi(x), draw_y = fptosi(y);
    MenuMouseDrawPosition(this, draw_x, draw_y);
    MenuFormDraw(draw_x, draw_y, state);
}

PORT_OVERRIDE void DrawMenuWakuRect(mgCTexture *texture, mgRect<float> rect, mgRect<int> uv,
                                    int alpha, int red, int green, int blue) {
    if (texture == nullptr || uv.right <= 0) {
        return;
    }
    const mgRect<int> outer(static_cast<int>(rect.left), static_cast<int>(rect.top),
                            static_cast<int>(rect.left + rect.right + uv.bottom),
                            static_cast<int>(rect.top + rect.bottom + uv.right));
    auto scissor = outer;
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
    float sign = 1.0f;
    const int horizontal_count = static_cast<int>(3.0f + rect.right / uv.right);
    for (int edge = 0; edge < 2; ++edge) {
        float x = (edge == 0 ? outer.right - 3 : outer.left) + PortMenuWakuPosition[0] * sign;
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
        float y = (edge == 0 ? outer.top - 13 : outer.bottom) + PortMenuWakuPosition[1] * sign;
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
    u8                *dst;
    mgCTextureManager *tex_manager;
    int                half_w;
    int                half_h;
    int                tex_w;
    int                tex_h;
    int                pad_w;
    int                pad_h;
    mgCDrawPrim       *prim;

    stack->Align64();
    tex_manager = &mgTexManager;
    dst = stack->stack_bytes + (stack->stack_used << 4);
    half_w = mgScreenWidth >> 1;
    half_h = mgScreenHeight >> 1;
    tex_w = half_w;
    MenuBGTextureBlock = block;
    tex_h = half_h;
    pad_w = half_w % 64;
    pad_h = half_h % 64;

    if (pad_w != 0) {
        tex_w += 64 - pad_w;
    }

    if (pad_h != 0) {
        tex_h += 64 - pad_h;
    }

    static char frame_name[] = "menuwork2";
    tex_manager->DeleteBlock(block);
    MenuFrameTex = tex_manager->EnterTexture(block, frame_name, NULL, tex_w, tex_h, 32, NULL, 0, 0);

    if (MenuFrameTex != NULL) {
        MenuFrameTex->tex0.bits.tcc = 0;
        MenuFrameTex->image[0] = (u_long128 *) dst;
        stack->Alloc(half_w * half_h * 4 / 16 + 1);
    }

    // The grab costs a frame on PS2 too: the tick that just ended is gfx::kPreviousFrame in the next one.
    mgEndFrame(NULL);
    mgBeginFrame(NULL);

    if (MenuFrameTex != NULL) {
        // Frame target: the window's sides past the logical frame are copied in and carried by the full-frame draw.
        const gfx::TextureHandle target = mgPortRenderTarget(MenuFrameTex, true);
        // Registered before the copy: until a target exists under its TBP0 the texture binds its stack image.
        // Linear at 2:1 samples each 2x2 block's midpoint, which is the box filter retail ran on the CPU.
        gfx::BlitTexture(gfx::kPreviousFrame, gfx::Rect{0, 0, mgScreenWidth, mgScreenHeight}, target,
                         gfx::Rect{0, 0, half_w, half_h}, gfx::Filter::Linear);
    }

    if (draw != 0 && MenuFrameTex != NULL) {
        tex_manager->ReloadTexture(block, (sceVif1Packet *) NULL);
        prim = GetMenuPrim();
        prim->Initialize(NULL, NULL);
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

// Largest grid position at or left of -256 that keeps the grid phase. Wide windows show ~171 logical pixels left of the frame.
float MenuTilePatternFirst(float v, int size) {
    float k;

    if (size <= 0 || v + 256.0f <= 0.0f) {
        return v;
    }

    k = (float) (int) ((v + 256.0f) / (float) size);

    if (k * (float) size < v + 256.0f) {
        k += 1.0f;
    }

    return v - k * (float) size;
}

// Count of grid positions first, first + size, ... that do not pass limit.
int MenuTilePatternCount(float first, int size, float limit) {
    int   count = 0;
    float pos = first;

    if (size <= 0) {
        return 0;
    }

    while (pos <= limit) {
        count++;
        pos += (float) size;
    }

    return count;
}

PORT_OVERRIDE void DrawMenuTilePattern(mgCDrawPrim *prim, mgCTexture *tex, float x, float y, mgRect<int> tex_rect,
                                       int unused, u8 *rgba) {
    mgRect<int> dest(0, 0, 0, 0);
    float       column_first;
    float       row_first;
    int         column_count;
    int         row_count;
    int         column;
    int         row;

    if (tex_rect.right <= 0 || tex_rect.bottom <= 0) {
        return;
    }

    column_first = MenuTilePatternFirst(x, tex_rect.right);
    row_first = MenuTilePatternFirst(y - tex_rect.bottom, tex_rect.bottom);
    column_count = MenuTilePatternCount(column_first, tex_rect.right, (float) mgScreenWidth + 256.0f);
    // Tall windows show logical y past 480, up to about 695 at 9:16, so rows must run to 480 + 256.
    row_count = MenuTilePatternCount(row_first, tex_rect.bottom, (float) mgScreenHeight + 256.0f);

    SetSpriteEnv(prim, MENU_SPRITE_ENV_TEXTURE);
    prim->Begin(MG_PRIM_SPRITE);
    prim->Texture(tex);

    if (rgba != NULL) {
        prim->Color(rgba[0], rgba[1], rgba[2], rgba[3]);
    } else {
        prim->Color(0x80, 0x80, 0x80, 0x80);
    }

    for (column = 0; column < column_count; column++) {
        dest.Set((int) (column_first + (float) (column * tex_rect.right)), (int) row_first, tex_rect.right,
                 tex_rect.bottom);

        for (row = 0; row < row_count; row++) {
            PrimQuad(prim, dest, tex_rect);
            dest.top += dest.bottom;
        }
    }

    prim->End();
}

PORT_OVERRIDE void CMenuPosDataForm::MenuFormDraw(int &state) {
    bool clip_form = dtype == MENUFORM_DTYPE_NORMAL || dtype == MENUFORM_DTYPE_LIST;
    // Full-screen fills and frame grabs are composition layers, not sliding panels.
    for (int i = 0; clip_form && i < parts_num; ++i) {
        const auto &part = parts[i];
        if (part.w >= mgScreenWidth && part.h >= mgScreenHeight) clip_form = false;
    }
    mgPortScreenClipScope clip(clip_form);
    MenuFormDraw(fptosi(x), fptosi(y), state);
}
