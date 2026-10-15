#include "menuchr.hpp"

#include "userdata.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// Disc capacities in quadwords, before MenuMemoryDivide's 64-block rounding.
constexpr int kHumanCapacity[MENU_CHARA_LOAD_MAX] = {
    0x1B80, 0x9AC0, 0x1C84, 0x11C0, 0x0BC0, 0x26C0, 0x0708};
constexpr int kRidepodCapacity[MENU_CHARA_LOAD_MAX] = {
    0x5240, 0x4514, 0x6E80, 0x1240, 0x05DC, 0, 0};

// A quick change (L3, R3) whose target can be loaded, from InitMainCharaBG until KeyMainCharaBG finishes it.
bool quick_change_pending = false;

// More steps than retail's slowest change needs: its loader phases plus the "change" logo crossing the screen.
constexpr int kQuickChangeMaxSteps = 256;

} // namespace

void PortRetailInitMainCharaBG(int, mgCMemory *, int) asm("MenuChr_InitMainCharaBG");
int PortRetailKeyMainCharaBG() asm("MenuChr_KeyMainCharaBG");

PORT_OVERRIDE void MenuMemoryAdjust(mgCMemory *pool, mgCMemory *rest, mgCMemory *buffers, int chara) {
    int capacities[MENU_CHARA_LOAD_MAX]{};
    if (chara == USER_CHARA_MAX || chara == USER_CHARA_MONICA || chara == USER_CHARA_ROBO) {
        const int *table = chara == USER_CHARA_ROBO ? kRidepodCapacity : kHumanCapacity;
        for (int i = 0; i < MENU_CHARA_LOAD_MAX; ++i) {
            capacities[i] = (table[i] + 63) / 64 * 64;
        }
    } else if (chara == USER_CHARA_MONSTER) {
        capacities[0] = 0x6400;
        capacities[5] = 0x3C0;
    }

    pool->Align64();
    int partitions = 0;
    for (int blocks : capacities) {
        partitions += blocks;
    }
    const int remaining = pool->stack_size - pool->stack_used;
    if (partitions > remaining) {
        std::fprintf(stderr, "menu character partitions exceed aligned pool: %d of %d blocks\n",
                     partitions, remaining);
        std::abort();
    }

    u_long128 *cursor = pool->stGetTop();
    for (int i = 0; i < MENU_CHARA_LOAD_MAX; ++i) {
        // Monster menus only replace these two views; the others retain their
        // owners just as in MenuMemoryDivide.
        if (chara == USER_CHARA_MONSTER && i != 0 && i != 5) {
            continue;
        }
        if (chara == USER_CHARA_MAX || chara == USER_CHARA_MONICA || chara == USER_CHARA_ROBO ||
            chara == USER_CHARA_MONSTER) {
            buffers[i].stSetBuffer(cursor, capacities[i]);
            if (chara != USER_CHARA_MONSTER) {
                std::snprintf(buffers[i].name, sizeof(buffers[i].name), "stack %d\n", i);
            }
            cursor += capacities[i];
        }
    }
    rest->stSetBuffer(cursor, remaining - partitions);
    std::strcpy(rest->name, "LOAD STACK");
    rest->stack_used = 0;
    rest->lock = 0;
}

// Retail paces a quick change (Max and Monica on L3, the Ridepod and the monster form on R3) one menu
// frame at a time: on the PS2 the frames hide the disc reads, and the "change" logo slides across the
// frozen screen until the reads are done. Natively the reads finish when they are issued, so the
// frozen screen is only the logo's slide. Note whether this change will load anything, as retail's
// NowMainCharaChngStatusBit does, so KeyMainCharaBG can tell a load from the "cannot change" message.
PORT_OVERRIDE void InitMainCharaBG(int chara_no, mgCMemory *stack, int mode) {
    quick_change_pending = false;

    if (stack != NULL) {
        int reason;
        quick_change_pending = (GetUserDataMan()->CheckQuickChange(chara_no, &reason) & QUICK_CHANGE_STATE_ENABLED) != 0;
    }

    PortRetailInitMainCharaBG(chara_no, stack, mode);
}

// Runs the whole quick change in one menu frame: retail's own steps, back to back, so the game resumes
// on the next frame with the new character. The "cannot change" message keeps retail's pacing, and so
// does a change that has not finished after kQuickChangeMaxSteps.
PORT_OVERRIDE int KeyMainCharaBG() {
    if (!quick_change_pending) {
        return PortRetailKeyMainCharaBG();
    }

    for (int step = 0; step < kQuickChangeMaxSteps; ++step) {
        if (PortRetailKeyMainCharaBG() != 0) {
            quick_change_pending = false;
            return 1;
        }
    }

    std::fprintf(stderr, "quick change: not finished after %d steps, continuing frame by frame\n", kQuickChangeMaxSteps);
    quick_change_pending = false;
    return 0;
}
