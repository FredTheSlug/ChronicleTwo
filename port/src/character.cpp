#include "common.h"

#include "character.hpp"
#include "mg_texture.hpp"
#include "mglib.hpp"
#include "platform/texture_catalog.hpp"
#include "texture_source.hpp"

namespace {

void DeleteCatalogTextures(mgIMG_FILE_HEADER *archive, int block) {
    auto source = TextureSourceLookup(archive);
    if (!source) {
        return;
    }
    const auto *catalog = texture_catalog::Find(*source);
    if (!catalog) {
        return;
    }
    for (const auto &entry : catalog->entries) {
        if (!entry.name.empty() && entry.name.front() != '#') {
            mgTexManager.DeleteTexture(const_cast<char *>(entry.name.c_str()), block);
        }
    }
}

} // namespace

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
    // The archive pointer identifies a source in the converted texture catalog.
    for (int slot = 1; slot < CHARA_IMAGE_MAX; ++slot) {
        auto *archive = images[slot];
        if (archive == nullptr) {
            continue;
        }
        DeleteCatalogTextures(archive, texture_block);
        images[slot] = nullptr;
    }
    for (int slot = 1; slot < CHARA_MOTION_SET_MAX; ++slot) {
        se_list[slot] = nullptr;
        se_num[slot] = 0;
    }
}

PORT_OVERRIDE void CCharacter2::DeleteImage() {
    int group_count = 0;
    mgTexManager.GetGroupNameList(texture_block, &group_count);
    if (tex_anime_group_start > 0) {
        for (int group = tex_anime_group_start; group < tex_anime_group_num; ++group) {
            mgTexManager.DeleteTexAnimeGroup(texture_block, group);
        }
    }
    if (images[0]) {
        DeleteCatalogTextures(images[0], texture_block);
        images[0] = nullptr;
    }
}
