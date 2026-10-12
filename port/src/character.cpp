#include "common.h"

#include "character.hpp"
#include "mg_texture.hpp"
#include "mglib.hpp"

PORT_OVERRIDE void CCharacter2::DeleteExtMotion() {
    int                 i;
    CHRINFO_KEY_SET    *first;
    mgCTextureManager  *tex;
    int                 group;
    mgIMG_HEADER       *image;
    int                 j;
    u32                 k;
    mgIMG_FILE_HEADER  *archive;

    for (i = 1; i < CHARA_MOTION_SET_MAX; i++) {
        motion[i].frame_info = NULL;
        shadow_motion[i].frame_info = NULL;
        key_list[i] = NULL;
        key_num[i] = 0;
        seq_list[i] = NULL;
        first = GetKeyListIndexPtr(0, NULL);

        if (first != NULL) {
            SetMotion(first->name, CHARA_MOTION_RESTART);
        }

        if (next_key != NULL) {
            posed_key = next_key;
            frame = (float) next_key->start_frame;
        }
    }

    tex = &mgTexManager;
    group = tex_anime_group_start;

    if (group > 0) {
        for (; group < tex_anime_group_num; group++) {
            tex->DeleteTexAnimeGroup(texture_block, group);
        }
    } else {
        tex->DeleteTexAnime(texture_block);
    }

    // Retail walks the slots at a hand-counted PS2 offset and pointer size.
    for (j = 1; j < CHARA_IMAGE_MAX; j++) {
        archive = images[j];

        if (archive != NULL) {
            image = (mgIMG_HEADER *) (archive + 1);

            for (k = 0; k < archive->num3; k++) {
                if ((u8) image->name[0] != '#') {
                    tex->DeleteTexture(image->name, texture_block);
                }

                image++;
            }

            images[j] = NULL;
        }
    }

    for (i = 1; i < CHARA_MOTION_SET_MAX; i++) {
        se_list[i] = NULL;
        se_num[i] = 0;
    }
}
