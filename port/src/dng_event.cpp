#include "common.h"

#include <cmath>

#include "dng_event.hpp"
#include "dng_main.hpp"
#include "mg_memory.hpp"
#include "monster.hpp"
#include "scene.hpp"
#include "snd_mngr.hpp"

PORT_OVERRIDE void LoadMonsterFile(int monster_id, int initialize) {
    CScene *scene = DngMainScene;
    CMonsterMan *manager = ActiveMonster;

    if (manager == nullptr) {
        return;
    }

    if (initialize != 0) {
        manager->Initialize(scene);
        scene->AssignStack(3);
        scene->ClearStack(3);
    }

    mgCMemory *memory = scene->GetStack(3);
    if (memory == nullptr) {
        return;
    }

    if (initialize != 0) {
        // Retail places these at +4 with a 0x30 stride; host pointers enlarge both fields.
        for (mgCMemory &slot : manager->memory) {
            slot.stSetBuffer(memory->stAlloc64(0xFA0), 0xFA0);
            slot.stReset();
        }
        sndInitPort(5);
    }

    if (manager->SearchBaseIndex(monster_id) < 0) {
        manager->EntryRefer(monster_id, memory);
    }
}

// Retail's definition returns void against the fixed-up declaration (scripts/port/header_fixups.py).
PORT_OVERRIDE int CRedMarkModel::Draw() {
    float position[4];
    float saved_position[4];

    if (draw_request != 0) {
        GetPosition(position);
        GetPosition(saved_position);
        position[1] += 2.0f * sinf(angle);
        SetPosition(position);
        CObjectFrame::DrawDirect();
        SetPosition(saved_position);
        draw_request = false;
    }

    return 0;
}
