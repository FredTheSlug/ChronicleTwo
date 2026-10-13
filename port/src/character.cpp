#include "common.h"

#include "character.hpp"
#include "mg_texture.hpp"
#include "mglib.hpp"

PORT_OVERRIDE void CCharacter2::DeleteExtMotion() {
    // Drop each extension before reselecting the base motion, preserving its pose transition.
    for (int slot = 1; slot < CHARA_MOTION_SET_MAX; ++slot) {
        motion[slot].frame_info = shadow_motion[slot].frame_info = nullptr;
        key_list[slot] = nullptr;
        seq_list[slot] = nullptr;
        key_num[slot] = 0;
        if (auto *base = GetKeyListIndexPtr(0, nullptr)) {
            SetMotion(base->name, CHARA_MOTION_RESTART);
        }
        if (next_key != nullptr) {
            posed_key = next_key;
            frame = static_cast<float>(next_key->start_frame);
        }
    }
    if (tex_anime_group_start > 0) {
        for (int group = tex_anime_group_start; group < tex_anime_group_num; ++group) {
            mgTexManager.DeleteTexAnimeGroup(texture_block, group);
        }
    } else {
        mgTexManager.DeleteTexAnime(texture_block);
    }
    // Native slots hold pointers; archive headers and names retain their disc layout.
    for (int slot = 1; slot < CHARA_IMAGE_MAX; ++slot) {
        auto *archive = images[slot];
        if (archive == nullptr) {
            continue;
        }
        auto *headers = reinterpret_cast<mgIMG_HEADER *>(archive + 1);
        for (u32 index = 0; index < archive->num3; ++index) {
            if (headers[index].name[0] != '#') {
                mgTexManager.DeleteTexture(headers[index].name, texture_block);
            }
        }
        images[slot] = nullptr;
    }
    for (int slot = 1; slot < CHARA_MOTION_SET_MAX; ++slot) {
        se_list[slot] = nullptr;
        se_num[slot] = 0;
    }
}
