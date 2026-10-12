#include "debug_mode.hpp"

#include "gamepad.hpp"
#include "mainloop.hpp"
#include "title.hpp"

void DebugModeBoot(bool debug_mode) {
    if (!debug_mode) {
        return;
    }
    MasterDebugCode = MASTER_DEBUG_CODE;
    // title.cpp only reads the save's debug code while this is 0, so setting it here stands in for the developer save.
    MasterDebugModeOn = 1;
}

bool DebugModeToggleChord(int pad1_held, int pad1_pressed) {
    constexpr int kShoulders = PAD_L1 | PAD_R1 | PAD_L2 | PAD_R2;
    return MasterDebugCode == MASTER_DEBUG_CODE && (pad1_held & kShoulders) == kShoulders && (pad1_pressed & PAD_R3) != 0;
}
