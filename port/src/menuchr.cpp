#include "menuchr.hpp"

#include "actionchara.hpp"
#include "dng_effect.hpp"
#include "dng_main.hpp"
#include "mainloop.hpp"
#include "mainloop_port.hpp"
#include "menudraw.hpp"
#include "menuchr_port.hpp"
#include "mglib.hpp"
#include "mg_texture.hpp"
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

// The character the pending quick change loads, for the banner that names it afterwards.
int quick_change_chara = -1;

// The banner naming the new character: retail's "change" logo row, shown over the game once the change is
// done, since the instant change no longer leaves time for retail's slide. Ticks of 1/60 s.
constexpr int kBannerFadeInTicks = 8;
constexpr int kBannerHoldTicks = 70;
constexpr int kBannerFadeOutTicks = 20;
constexpr int kBannerTicks = kBannerFadeInTicks + kBannerHoldTicks + kBannerFadeOutTicks;
constexpr int kBannerWidth = 0x100;
constexpr int kBannerHeight = 0x40;
constexpr int kBannerBottomMargin = 56;
int banner_chara = -1;
int banner_ticks = 0;
int banner_loop = -1;

// Dark Cloud's change effect, adapted: the new character starts lit white and fades back to its own colours,
// with sparkles rising around it (dungeons only: their sparkle sprites are the dungeon's). Game frames.
constexpr int kChangeFadeFrames = 24;
constexpr int kChangeSparkFrames = 18;
constexpr int kChangeSparksPerFrame = 3;
constexpr float kChangeSparkRadius = 6.0f;
bool change_fx_pending = false;
int change_fx_loop = -1;
int change_spark_frames = 0;

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
        quick_change_chara = chara_no;
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
            banner_chara = quick_change_chara;
            banner_ticks = 0;
            banner_loop = PortLoopNo;
            change_fx_pending = true;
            change_fx_loop = PortLoopNo;
            return 1;
        }
    }

    std::fprintf(stderr, "quick change: not finished after %d steps, continuing frame by frame\n", kQuickChangeMaxSteps);
    quick_change_pending = false;
    return 0;
}

extern CMenuPosDataManage *MenuPosData;

void PortCancelQuickChangeBanner() {
    banner_chara = -1;
    change_fx_pending = false;
    change_spark_frames = 0;
}

// Starts the change effect on the first game frame after the change, once the menu has closed, and keeps
// the sparkles coming for kChangeSparkFrames. Dark Cloud's version (gameloop.cpp, GAME_TASK_CHARA_CHANGED)
// pulses the ambient light toward white and plays its ceffe_s sparkle model; here the character's own
// palette animation fades from white and the dungeon's item sparkles stand in for the model.
void PortStepQuickChangeEffect() {
    if (PortLoopNo != change_fx_loop) {
        change_fx_pending = false;
        change_spark_frames = 0;
        return;
    }

    if (MenuPosData != NULL) {
        return;
    }

    CScene       *scene = GetMainScene();
    CActionChara *chara = scene != NULL ? (CActionChara *) scene->GetCharacter(0) : NULL;

    if (chara == NULL) {
        change_fx_pending = false;
        change_spark_frames = 0;
        return;
    }

    if (change_fx_pending) {
        change_fx_pending = false;
        // One white pulse that starts at its peak, so the character comes in white and fades to its colours.
        CPalletAnime &fade = chara->pallet[ACTION_PALLET_MAX - 1];
        fade.SetAnim(0xFF, 0xFF, 0xFF, 1, kChangeFadeFrames * 2, 0);
        fade.elapsed = kChangeFadeFrames;
        change_spark_frames = PortLoopNo == LOOP_DUNGEON ? kChangeSparkFrames : 0;
    }

    if (change_spark_frames <= 0) {
        return;
    }

    change_spark_frames--;
    sceVu0FVECTOR base;
    chara->GetPosition(base);

    for (int i = 0; i < kChangeSparksPerFrame; i++) {
        sceVu0FVECTOR spark;
        sceVu0CopyVector(spark, base);
        spark[0] += fRand(kChangeSparkRadius * 2.0f) - kChangeSparkRadius;
        spark[1] += fRand(1.1f) * chara->GetBodyHeight();
        spark[2] += fRand(kChangeSparkRadius * 2.0f) - kChangeSparkRadius;
        MiniEffPrimMan.CreatPrim(spark, MINI_EFF_PRIM_GOLD);
    }
}

// Draws the banner centred at the bottom of the screen, fading in and out, while the game runs. A menu
// opened meanwhile hides it and keeps running its time out, so it never comes back after a menu such as
// the dungeon's floor gate; a mode change drops it.
void PortDrawQuickChangeBanner() {
    if (banner_chara < 0) {
        return;
    }

    if (PortLoopNo != banner_loop || banner_ticks >= kBannerTicks) {
        banner_chara = -1;
        return;
    }

    if (MenuPosData != NULL) {
        banner_ticks += mgFrameRate > 0 ? mgFrameRate : 1;
        return;
    }

    mgCTexture *logo = mgTexManager.GetTexture("change_logo", -1);

    if (logo != NULL) {
        int alpha = 0x80;

        if (banner_ticks < kBannerFadeInTicks) {
            alpha = 0x80 * (banner_ticks + 1) / kBannerFadeInTicks;
        } else if (banner_ticks >= kBannerFadeInTicks + kBannerHoldTicks) {
            alpha = 0x80 * (kBannerTicks - banner_ticks) / kBannerFadeOutTicks;
        }

        mgRect<int> row;
        row.Set(0, banner_chara * kBannerHeight, kBannerWidth, kBannerHeight);
        mgTexManager.ReloadTexture(logo->block, (sceVif1Packet *) NULL);
        PrimQuad(logo, (float) ((mgScreenWidth - kBannerWidth) / 2),
                 (float) (mgScreenHeight - kBannerHeight - kBannerBottomMargin), row, alpha, 0x80, 0x80, 0x80);
    }

    banner_ticks += mgFrameRate > 0 ? mgFrameRate : 1;
}
