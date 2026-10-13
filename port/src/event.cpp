#include "common.h"

#include <array>

#include "dng_main.hpp"
#include "event.hpp"
#include "event_func.hpp"
#include "event_vector_callbacks.hpp"
#include "mainloop.hpp"
#include "runscript.hpp"
#include "stubs/event.hpp"

extern int PortLoopNo asm("MainLoop_LoopNo");

namespace {
int ClearDungeonEffects(RS_STACKDATA *, int) {
    RocketLauncher.Clear();
    for (int i = 0; i < 16; ++i) {
        MachineGun.active[i] = 0;
        MachineGun.col_prim_id[i] = -1;
    }
    MachineGun.index = 0;
    LaserGun.Clear();
    // Town events share this external, but only the dungeon owns FxScriptMan.
    // It can be null on first entry or point into a retired dungeon arena.
    if (PortLoopNo == LOOP_DUNGEON && FxScriptMan != nullptr) {
        FxScriptMan->AllClearEffSpt();
    }
    return true;
}
} // namespace

PORT_OVERRIDE void SetEventScript(char *program, char *, mgCMemory *memory) {
    // The one scene interpreter owns its host runtime arrays. The file arena keeps
    // the unchanged disc bytes, whose retail budget does not include wider pointers.
    static std::array<RS_STACKDATA, 128> values;
    static std::array<RS_CALLDATA, 512> calls;
    if (program == nullptr || memory == nullptr) {
        EventScript.DeleteProgram();
        return;
    }
    EventScript.load(reinterpret_cast<RS_PROG_HEADER *>(program), values.data(), values.size(),
                              calls.data(), calls.size());
    SetEventFunc(&EventScript);
    // The scene owns a private table for native slot arithmetic and the
    // dungeon external whose ownership does not hold in town mode.
    static std::array<int (*)(RS_STACKDATA *, int), EVENT_EXT_FUNC_NUM> functions;
    for (int i = 0; i < EVENT_EXT_FUNC_NUM; ++i) {
        functions[i] = EventScript.ext_func_table[i];
    }
    functions[EVENT_EXT_DNG_EFFECT_ALL_CLEAR] = ClearDungeonEffects;
    ReplaceEventVectorCallbacks(functions.data());
    EventScript.ext_func(functions.data(), functions.size());
}
