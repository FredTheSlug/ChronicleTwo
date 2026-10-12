#include "common.h"

#include "mg_math.hpp"
#include "mg_visual_port.hpp"
#include "mglib.hpp"
#include "visualmotion.hpp"

PORT_OVERRIDE int mgCVisualMotionMDT::DataAssignMotionMDT(MDT_HEADER *header, mgCVMotionData *motion,
                                                        mgCMemory *memory, mgCMemory *work,
                                                        mgCTextureManager *textures) {
    if (work == nullptr) {
        return 0;
    }

    work->stReset();
    texture_manager = textures != nullptr ? textures : &mgTexManager;
    CopyMDTDataPointer(header, memory);
    frame = motion->frame;
    base_matrix = motion->base_matrix;
    frame_id = motion->frame_id;

    // Copies share these weights, so they must live in the visual's arena rather than the reused work stack.
    CreateVertexWeight(motion->weight_data, frame_id, memory);

    int slots = 0;
    while (slots < 32 && bone[slots] >= 0) {
        ++slots;
    }
    vu1_base = slots * 4 + 0x3C;
    vu1_offset = 0xB4 - (slots * 4) / 2;

    mgCMemory indices;
    indices.stSetBuffer(work->stGetTop(), work->stGetRest());
    face_group = nullptr;
    auto *section = reinterpret_cast<MDT_FACES *>(reinterpret_cast<u_char *>(header) + header->faces_ofs);
    auto *source = reinterpret_cast<FACES_ID *>(section + 1);
    for (int primitive = 0; primitive < section->prim_num; ++primitive) {
        indices.stReset();
        mgCFace *face = nullptr;
        source = CreateFace(source, memory, &indices, &face);
        CreateFaceMotionPacket(nullptr, face, motion);
        face->packet_tag = 0;
    }
    return 1;
}

PORT_OVERRIDE int mgCVisualMotionMDT::CreateFaceMotionPacket(u_int *packet, mgCFace *face, mgCVMotionData *motion) {
    if (face != NULL) {
        mgVisualPortRefreshFace(this, face);
    }
    return 0;
}

PORT_OVERRIDE int mgCVisualMotionMDT::CreateExtRenderInfoPacket(u_int *packet, float (*matrix)[4],
                                                                mgRENDER_INFO *info) {
    if (frame == NULL || base_matrix == NULL) {
        return 0;
    }

    int slot_count = 0;
    while (slot_count < 0x20 && bone[slot_count] >= 0) {
        slot_count += 1;
    }

    if (slot_count == 0) {
        return 0;
    }

    float inverse_matrix[4][4];
    float root_matrix[4][4];
    float inverse_root[4][4];
    float root_local[4][4];
    float slot_matrix[4][4];
    float slot_inverse[4][4];
    float frame_matrix[4][4];
    float bone_matrix[4][4];

    mgInversMatrix(inverse_matrix, matrix);
    frame[0]->GetLWMatrix(root_matrix);
    mgMulMatrix(inverse_root, root_matrix, base_matrix[frame_id]);
    mgInversMatrix(root_local, inverse_root);

    gfx::Prog0Constants &prog0 = mgVisualPortCurrentProg0();

    for (int i = 0; i < slot_count; i++) {
        const int slot = bone[i];
        if (frame[slot] == NULL) {
            continue;
        }

        frame[slot]->GetLWMatrix(frame_matrix);
        mgMulMatrix(slot_matrix, root_matrix, base_matrix[slot]);
        mgMulMatrix(slot_matrix, root_local, slot_matrix);
        mgInversMatrix(slot_inverse, slot_matrix);
        mgMulMatrix(frame_matrix, inverse_matrix, frame_matrix);
        mgMulMatrix(bone_matrix, frame_matrix, slot_inverse);

        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 4; col++) {
                prog0.bones[i][row][col] = bone_matrix[col][row];
            }
        }
    }

    return 0;
}
