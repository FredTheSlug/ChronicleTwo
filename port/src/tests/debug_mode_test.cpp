#include <gtest/gtest.h>

#include "debug_mode.hpp"
#include "gamepad.hpp"
#include "mainloop.hpp"
#include "title.hpp"

namespace {

constexpr int kAllShoulders = PAD_L1 | PAD_R1 | PAD_L2 | PAD_R2;

} // namespace

TEST(DebugMode, BootOnSetsTheDebugCodeAndTheModeFlag) {
    MasterDebugCode = 0;
    MasterDebugModeOn = 0;
    DebugModeBoot(true);
    EXPECT_EQ(MasterDebugCode, MASTER_DEBUG_CODE);
    EXPECT_EQ(MasterDebugModeOn, 1);
}

TEST(DebugMode, BootOffLeavesTheGlobalsAlone) {
    MasterDebugCode = 0;
    MasterDebugModeOn = 0;
    DebugModeBoot(false);
    EXPECT_EQ(MasterDebugCode, 0);
    EXPECT_EQ(MasterDebugModeOn, 0);
}

TEST(DebugMode, ChordNeedsAllFourShouldersAndANewR3Press) {
    MasterDebugCode = MASTER_DEBUG_CODE;
    EXPECT_TRUE(DebugModeToggleChord(kAllShoulders, PAD_R3));
    EXPECT_TRUE(DebugModeToggleChord(kAllShoulders | PAD_CROSS, PAD_R3));
    EXPECT_FALSE(DebugModeToggleChord(kAllShoulders | PAD_R3, 0));
    EXPECT_FALSE(DebugModeToggleChord(PAD_R3, PAD_R3));
    for (int shoulder : {PAD_L1, PAD_R1, PAD_L2, PAD_R2}) {
        EXPECT_FALSE(DebugModeToggleChord(kAllShoulders & ~shoulder, PAD_R3)) << shoulder;
    }
}

TEST(DebugMode, ChordIsOffWithoutTheDebugCode) {
    MasterDebugCode = 0;
    EXPECT_FALSE(DebugModeToggleChord(kAllShoulders, PAD_R3));
}

// Drives the real pad-1 accessors the MainLoop call site uses, not masks built by hand.
TEST(GamePadDebugChord, FiresOnANewR3PressWithAllShoulders) {
    MasterDebugCode = MASTER_DEBUG_CODE;
    GamePad__2.key_lock = 0;
    GamePad__2.pad[0].button = kAllShoulders | PAD_R3;
    GamePad__2.previous_pad[0].button = kAllShoulders;
    EXPECT_TRUE(DebugModeToggleChord(GamePad__2.GetPadOn(), GamePad__2.GetPadDown()));
}

TEST(GamePadDebugChord, DoesNotFireWhileR3IsHeld) {
    MasterDebugCode = MASTER_DEBUG_CODE;
    GamePad__2.key_lock = 0;
    GamePad__2.pad[0].button = kAllShoulders | PAD_R3;
    GamePad__2.previous_pad[0].button = kAllShoulders | PAD_R3;
    EXPECT_FALSE(DebugModeToggleChord(GamePad__2.GetPadOn(), GamePad__2.GetPadDown()));
}

TEST(GamePadDebugChord, FiresWithPad2Locked) {
    MasterDebugCode = MASTER_DEBUG_CODE;
    GamePad__2.DebugKeyLock(1);
    GamePad__2.key_lock = 0;
    GamePad__2.pad[0].button = kAllShoulders | PAD_R3;
    GamePad__2.previous_pad[0].button = kAllShoulders;
    EXPECT_TRUE(DebugModeToggleChord(GamePad__2.GetPadOn(), GamePad__2.GetPadDown()));
}

TEST(GamePadDebugChord, PadLockSilencesTheChord) {
    MasterDebugCode = MASTER_DEBUG_CODE;
    GamePad__2.key_lock = 1;
    GamePad__2.pad[0].button = kAllShoulders | PAD_R3;
    GamePad__2.previous_pad[0].button = kAllShoulders;
    EXPECT_FALSE(DebugModeToggleChord(GamePad__2.GetPadOn(), GamePad__2.GetPadDown()));
    GamePad__2.key_lock = 0;
}
