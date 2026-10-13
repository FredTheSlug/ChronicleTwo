#include "common.h"

#include <array>
#include <cstring>

#include "gamedata.hpp"
#include "gfx/tim2.hpp"
#include "menudraw.hpp"
#include "menumain.hpp"
#include "mg_texture_port.hpp"
#include "mglib.hpp"
#include "userdata.hpp"

extern short PortIconSheet asm("MenuDraw_IconSheet");
extern MENU_SHORT_RECT PortIconUV[2] asm("MenuDraw_IconUV");

PORT_OVERRIDE mgCTexture *GetMenuItemIconTexInfo(int item_no, int mode) {
    PortIconSheet = -1;
    const CDataCommon *item = GameItemDataManage.GetCommonData(item_no);
    if (item == nullptr || MenuPosData == nullptr || item->icon_texture_no >= 2 || mode < 0 || mode >= 4) {
        return nullptr;
    }
    PortIconSheet = item->icon_texture_no;
    mgCTexture *texture = MenuPosData->item_icon_tex[mode][PortIconSheet];
    if (texture == nullptr) {
        return nullptr;
    }

    // Retail stages every icon into one shared cell before drawing it. Display
    // replays keep only the last staged cell, so draw and raster effects must
    // sample each item's immutable atlas cell directly on the host.
    mgRect<int> source;
    GetMenuItemIconTexGetXY(item_no, source);
    PortIconUV[PortIconSheet] = {static_cast<short>(source.left), static_cast<short>(source.top),
                               static_cast<short>(source.right), static_cast<short>(source.bottom)};
    const MgTextureHandles handles = MgTextureLookupTex0(texture->tex0.value);
    // Effect variants are CPU copies of the atlas descriptor sharing its registry key;
    // their palette bytes belong to the variant, rather than to the registered base.
    if (mode != 0 && texture->clut != nullptr && handles.palette != gfx::kNullTexture) {
        gfx::tim2::Source palette;
        palette.psm = gfx::tim2::Psm::T8;
        palette.clut = reinterpret_cast<const uint8_t *>(texture->clut);
        palette.csm1 = texture->tex0.CSM == 0;
        palette.clut16 = texture->tex0.CPSM == SCE_GS_PSMCT16 || texture->tex0.CPSM == SCE_GS_PSMCT16S;
        gfx::UpdatePalette(handles.palette, gfx::tim2::DecodePalette(palette).data(), 0, 256);
    }
    return texture;
}

PORT_OVERRIDE int CopyActiveIconTexture(mgCTexture **textures, int chara_no, u_int *) {
    CUserDataManager *user = GetUserDataMan();
    if (user == nullptr || textures == nullptr || chara_no < USER_CHARA_MAX) {
        return 0;
    }
    std::array<int, 8> items{};
    if (chara_no >= USER_CHARA_MAX && chara_no < USER_CHARA_ROBO) {
        const CHARA_DATA *chara = user->GetCharaDataPtr(chara_no);
        if (chara == nullptr) {
            return 0;
        }
        for (int slot = 0; slot < 3; ++slot) {
            items[slot] = chara->active_item[slot].item_no;
        }
        for (int slot = 0; slot < 4; ++slot) {
            items[4 + slot] = chara->equip[slot].item_no;
        }
    } else if (chara_no == USER_CHARA_ROBO) {
        items[4] = user->robo_data.parts[ROBO_PART_PACK].item_no;
        items[5] = user->robo_data.parts[ROBO_PART_ARM].item_no;
    }
    char item_name[] = "itemicon", weapon_name[] = "wepicon";
    mgCTexture *sheets[] = {mgTexManager.GetTexture(item_name, -1), mgTexManager.GetTexture(weapon_name, -1)};
    constexpr int counts[] = {3, 2};
    for (int sheet = 0; sheet < 2; ++sheet) {
        mgCTexture *dest = textures[sheet];
        const mgCTexture *source = sheets[sheet];
        if (dest == nullptr || source == nullptr || dest->image[0] == nullptr || dest->clut == nullptr ||
            source->image[0] == nullptr || source->clut == nullptr || dest->bpp != 8 || source->bpp != 8) {
            continue;
        }
        std::memcpy(dest->clut, source->clut, 1024);
        const auto *palette = reinterpret_cast<const u8 *>(dest->clut);
        u8 transparent = 0;
        for (int index = 0; index < 256; ++index) {
            if (palette[index * 4 + 3] == 0) {
                transparent = static_cast<u8>(index);
                break;
            }
        }
        auto *pixels = reinterpret_cast<u8 *>(dest->image[0]);
        const auto *atlas = reinterpret_cast<const u8 *>(source->image[0]);
        for (int slot = 0; slot < counts[sheet]; ++slot) {
            const int x = (slot % 2) * 32, y = (slot / 2) * 32;
            if (x + 32 > dest->width || y + 32 > dest->height) {
                continue;
            }
            const int item = items[sheet * 4 + slot];
            const int icon = item > 0 ? GetItemIconNo(item) : -1;
            const int src_x = (icon % 8) * 32, src_y = (icon / 8) * 32;
            const bool valid = icon >= 0 && src_x + 32 <= source->width && src_y + 32 <= source->height;
            for (int row = 0; row < 32; ++row) {
                u8 *out = pixels + (y + row) * dest->width + x;
                if (valid) {
                    std::memcpy(out, atlas + (src_y + row) * source->width + src_x, 32);
                } else {
                    std::memset(out, transparent, 32);
                }
            }
        }
        // Both images and CLUTs changed in place: a pointer-only cache cannot see the copy.
        MgTexturePixelsChanged(dest);
    }
    return 1;
}
