#include "menuchr.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "dataread_port.hpp"
#include "mainloop_port.hpp"
#include "menu_character_assets.hpp"
#include "menuchr_port.hpp"
#include "menudraw.hpp"
#include "mg_texture.hpp"
#include "mglib.hpp"
#include "platform/arena.hpp"
#include "platform/model_assets.hpp"
#include "swordeffect.hpp"
#include "userdata.hpp"

namespace {

// Disc capacities in quadwords, before MenuMemoryDivide's 64-block rounding.
constexpr int kHumanCapacity[MENU_CHARA_LOAD_MAX] = {
    0x1B80, 0x9AC0, 0x1C84, 0x11C0, 0x0BC0, 0x26C0, 0x0708};
constexpr int kRidepodCapacity[MENU_CHARA_LOAD_MAX] = {
    0x5240, 0x4514, 0x6E80, 0x1240, 0x05DC, 0, 0};

// A quick change (L3, R3) whose target can be loaded, from InitMainCharaBG until KeyMainCharaBG finishes it.
bool quick_change_pending = false;

// The character the pending quick change loads, for the banner that names it afterwards.
int quick_change_chara = -1;

// Character-change banner timing in native gameplay ticks.
constexpr int kBannerFadeInTicks = 8;
constexpr int kBannerHoldTicks = 70;
constexpr int kBannerFadeOutTicks = 20;
constexpr int kBannerTicks = kBannerFadeInTicks + kBannerHoldTicks + kBannerFadeOutTicks;
constexpr int kBannerWidth = 0x100;
constexpr int kBannerHeight = 0x40;
constexpr int kBannerBottomMargin = 56;
int           banner_chara = -1;
int           banner_ticks = 0;
int           banner_loop = -1;

// Maximum synchronous loading steps before normal menu pacing resumes.
constexpr int kQuickChangeMaxSteps = 256;

} // namespace

bool menu_character::PrepareArena(mgCMemory &memory, const std::filesystem::path &model, std::string_view profile,
                                  std::string &error, std::size_t extra_quadwords) {
    if (memory.stack_used != 0) {
        error = "menu character arena must be reset before construction";
        return false;
    }
    const auto estimate = model_assets::EstimateCharacterMemory(model, profile, error);
    if (!estimate) {
        return false;
    }
    const auto quadwords = (*estimate + sizeof(u_long128) - 1) / sizeof(u_long128) + extra_quadwords + 1;
    if (quadwords > static_cast<std::size_t>(std::numeric_limits<int>::max() - 63)) {
        error = "menu character exceeds native arena capacity";
        return false;
    }
    const int capacity = static_cast<int>((quadwords + 63) / 64 * 64);
    if (memory.stack_size < capacity || !memory.stack) {
        auto *storage = reinterpret_cast<u_long128 *>(ArenaBlock(&memory, static_cast<std::size_t>(capacity) * sizeof(u_long128)));
        memory.stSetBuffer(storage, capacity);
    }
    error.clear();
    return true;
}

void PortPrepareMenuCharacterArena(mgCMemory &memory, const void *model, const char *profile, int part) {
    const auto  path = DataReadBufferPath(model);
    std::string error;
    // Equipped melee weapons reserve the native twelve-point, eight-subdivision sword trail.
    const std::size_t trail = part == 2 ? (sizeof(CSWordAfterEffect) + 15) / 16 + 2 + 2 * (12 + 1) + 2 * (12 * (8 + 2) + 1) : 0;
    if (!path || !menu_character::PrepareArena(memory, *path, profile ? profile : "", error, trail)) {
        std::fprintf(stderr, "menu character %s: %s\n", path ? path->c_str() : "unknown asset", path ? error.c_str() : "actual glTF filename required");
        std::exit(1);
    }
}

void PortRetailInitMainCharaBG(int, mgCMemory *, int) asm("MenuChr_InitMainCharaBG");
int  PortRetailKeyMainCharaBG() asm("MenuChr_KeyMainCharaBG");

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

/**
 * Starts a character change and records whether it can use synchronous loading.
 */
PORT_OVERRIDE void InitMainCharaBG(int chara_no, mgCMemory *stack, int mode) {
    quick_change_pending = false;

    if (stack != nullptr) {
        int reason;
        quick_change_pending = (GetUserDataMan()->CheckQuickChange(chara_no, &reason) & QUICK_CHANGE_STATE_ENABLED) != 0;
        quick_change_chara = chara_no;
    }

    PortRetailInitMainCharaBG(chara_no, stack, mode);
}

/**
 * Completes an eligible quick change within a bounded number of loading steps.
 */
PORT_OVERRIDE int KeyMainCharaBG() {
    if (!quick_change_pending) {
        return PortRetailKeyMainCharaBG();
    }

    for (int step = 0; step < kQuickChangeMaxSteps; ++step) {
        if (PortRetailKeyMainCharaBG() != 0) {
            quick_change_pending = false;
            banner_chara = quick_change_chara;
            banner_ticks = 0;
            banner_loop = PortLoopNo;
            return 1;
        }
    }

    std::fprintf(stderr, "quick change: not finished after %d steps, continuing frame by frame\n", kQuickChangeMaxSteps);
    quick_change_pending = false;
    return 0;
}

/**
 * Clears the character-change banner before a gameplay mode transition.
 */
void PortCancelQuickChangeBanner() {
    banner_chara = -1;
}

/**
 * Draws the active character-change banner while its gameplay mode remains active.
 */
void PortDrawQuickChangeBanner() {
    if (banner_chara < 0) {
        return;
    }

    if (PortLoopNo != banner_loop || banner_ticks >= kBannerTicks) {
        banner_chara = -1;
        return;
    }

    if (MenuPosData != nullptr) {
        banner_ticks += mgFrameRate > 0 ? mgFrameRate : 1;
        return;
    }

    static char logo_name[] = "change_logo";
    mgCTexture *logo = mgTexManager.GetTexture(logo_name, -1);

    if (logo != nullptr) {
        int alpha = 0x80;

        if (banner_ticks < kBannerFadeInTicks) {
            alpha = 0x80 * (banner_ticks + 1) / kBannerFadeInTicks;
        } else if (banner_ticks >= kBannerFadeInTicks + kBannerHoldTicks) {
            alpha = 0x80 * (kBannerTicks - banner_ticks) / kBannerFadeOutTicks;
        }

        mgRect<int> row;
        row.Set(0, banner_chara * kBannerHeight, kBannerWidth, kBannerHeight);
        mgTexManager.ReloadTexture(logo->block, static_cast<sceVif1Packet *>(nullptr));
        PrimQuad(logo, (float) ((mgScreenWidth - kBannerWidth) / 2),
                 (float) (mgScreenHeight - kBannerHeight - kBannerBottomMargin), row, alpha, 0x80, 0x80, 0x80);
    }

    banner_ticks += mgFrameRate > 0 ? mgFrameRate : 1;
}
