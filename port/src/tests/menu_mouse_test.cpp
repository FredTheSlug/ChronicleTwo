#include <gtest/gtest.h>

#include "gamepad.hpp"
#include "mainloop.hpp"
#include "menu_mouse.hpp"
#include "menusys.hpp"
#include "mglib.hpp"
#include "mglib_port.hpp"
#include "platform/clock.hpp"
#include "platform/input.hpp"

#include <limits>

namespace {
constexpr MenuMouseRect rows[] = {{20, 40, 100, 30}, {20, 80, 100, 30}, {20, 120, 100, 30}};

TEST(MenuMouse, HitBoxesExcludeGapsAndTheirFarEdges) {
    EXPECT_EQ(MenuMouseHit(rows, 20, 40), 0);
    EXPECT_EQ(MenuMouseHit(rows, 119, 109), 1);
    EXPECT_EQ(MenuMouseHit(rows, 120, 90), -1);
    EXPECT_EQ(MenuMouseHit(rows, 50, 75), -1);
    EXPECT_EQ(MenuMouseHit(rows, -100, 90), -1);
    EXPECT_EQ(MenuMouseHit(rows, std::numeric_limits<float>::quiet_NaN(), 90), -1);
}

TEST(MenuMouse, StationaryPointerDoesNotUndoControllerSelection) {
    MenuMouseSample mouse{.x = 40, .y = 90, .pointing = true};
    EXPECT_EQ(MenuMouseChoose(mouse, rows, 2), 2);
    mouse.moved = true;
    EXPECT_EQ(MenuMouseChoose(mouse, rows, 2), 1);
    mouse.pointing = false;
    EXPECT_EQ(MenuMouseChoose(mouse, rows, 2), 2);
}

TEST(MenuMouse, ClickSelectsTheHoveredRowAndWheelClamps) {
    MenuMouseSample mouse{.x = 40, .y = 90, .pointing = true, .clicked = 1};
    EXPECT_EQ(MenuMouseChoose(mouse, rows, 0), 1);
    mouse.clicked = 0;
    mouse.wheel = -1;
    EXPECT_EQ(MenuMouseChoose(mouse, rows, 1), 2);
    EXPECT_EQ(MenuMouseChoose(mouse, rows, 2), 2);
    mouse.wheel = 1;
    EXPECT_EQ(MenuMouseChoose(mouse, rows, 1), 0);
    EXPECT_EQ(MenuMouseChoose(mouse, rows, 0), 0);
    mouse.wheel = std::numeric_limits<float>::infinity();
    EXPECT_EQ(MenuMouseChoose(mouse, rows, 1), 1);
}

TEST(MenuMouse, WindowPixelsFollowUiScalingAndHighDpiMapping) {
    EXPECT_FLOAT_EQ(MenuMouseUnits(3, 1536, 1536), 1.0f / 3);
    EXPECT_FLOAT_EQ(MenuMouseUnits(6, 3072, 1536), 1.0f / 3);
    EXPECT_FLOAT_EQ(MenuMouseUnits(0, 0, 0), 1);
}

TEST(MenuMouse, HoverUsesTheRetailMoveResultAndScrollUpdate) {
    int cursor = 0, scroll = 0;
    MenuMouseMove(&cursor, 8);
    EXPECT_EQ(cursor, 0);
    EXPECT_EQ(MenuKeySelectCheck(0, &cursor, &scroll, 0, 13, 3, MENU_CURSOR_LIMIT_CLAMP), MENU_CURSOR_MOVED);
    EXPECT_EQ(cursor, 8);
    EXPECT_EQ(scroll, 6);
    EXPECT_EQ(MenuKeySelectCheck(0, &cursor, &scroll, 0, 13, 3, MENU_CURSOR_LIMIT_CLAMP), MENU_CURSOR_UNCHANGED);
}

TEST(MenuMouse, PadCursorLimitsRemainRetailWithoutMouseMotion) {
    int cursor = 0;
    EXPECT_EQ(MenuKeySelectCheck(-1, &cursor, nullptr, 0, 4, 4, MENU_CURSOR_LIMIT_WRAP), MENU_CURSOR_MOVED);
    EXPECT_EQ(cursor, 3);
    EXPECT_EQ(MenuKeySelectCheck(1, &cursor, nullptr, 0, 4, 4, MENU_CURSOR_LIMIT_REJECT), MENU_CURSOR_BLOCKED);
    EXPECT_EQ(cursor, 3);
    EXPECT_EQ(MenuKeySelectCheck(1, &cursor, nullptr, 0, 4, 4, MENU_CURSOR_LIMIT_EXIT), MENU_CURSOR_EXIT);
    EXPECT_EQ(cursor, 3);
}

TEST(MenuMouse, PopupWheelStepsAnEntireGridRow) {
    MenuMouseRect cells[15]{};
    MenuMouseSample mouse{.wheel = -1, .pointing = true};
    EXPECT_EQ(MenuMouseChoose(mouse, cells, 2, 5), 7);
    EXPECT_EQ(MenuMouseChoose(mouse, cells, 12, 5), 14);
}

class MenuMouseInput : public testing::Test {
protected:
    int width = mgScreenWidth, height = mgScreenHeight;
    void SetUp() override {
        mgScreenWidth = 512;
        mgScreenHeight = 480;
        InputResetBindings();
        InputSetScriptedDevices({});
        MenuMouseEnable(false);
        MenuMouseTake();
        GamePad__2.key_lock = 0;
        GamePad__2.pad[0].button = GamePad__2.previous_pad[0].button = 0;
        ClockSetUnbounded(true);
    }
    void TearDown() override {
        InputSetScriptedDevices({});
        MenuMouseEnable(false);
        MenuMouseTake();
        InputSetMenuMouse(false);
        mgScreenWidth = width;
        mgScreenHeight = height;
        InputSetOverride(0, nullptr);
        InputResetBindings();
    }
    static void Tick() { mgEndFrame(nullptr); }
};

TEST_F(MenuMouseInput, ClicksUseRetailDownWithoutChangingStoredPadButtons) {
    MenuMouseEnable(true);
    InputSetScriptedDevices({.mouse_buttons = 1});
    EXPECT_TRUE(GamePad__2.Down(PAD_CROSS));
    EXPECT_FALSE(GamePad__2.Down(PAD_CIRCLE));
    EXPECT_EQ(GamePad__2.pad[0].button, 0);
    GamePad__2.key_lock = 1;
    EXPECT_FALSE(GamePad__2.Down(PAD_CROSS));
    GamePad__2.key_lock = 0;
}

TEST_F(MenuMouseInput, HeldClickCannotSelectWhenMenuOpens) {
    InputSetScriptedDevices({.mouse_buttons = 1});
    MenuMouseEnable(true);
    EXPECT_EQ(MenuMouseButtons(), 0);
}

TEST_F(MenuMouseInput, OutsideClickIsRejectedButRightClickCancels) {
    MenuMouseEnable(true);
    InputSetScriptedDevices({.mouse_buttons = 3});
    MenuMouseRows(rows, 0);
    EXPECT_FALSE(GamePad__2.Down(PAD_CROSS));
    EXPECT_TRUE(GamePad__2.Down(PAD_CIRCLE));
}

TEST_F(MenuMouseInput, MessageClickMovesTheRetailDirectionBeforeItConfirms) {
    const MenuMouseRect choices[] = {{20, 200, 70, 50}, {200, 200, 100, 60}};
    MenuMouseEnable(true);
    MenuMouseNoteChoices(this, 42, choices, 0, true);
    InputSetScriptedDevices({.mouse_buttons = 1});
    EXPECT_EQ(GamePad__2.GetPadDown(), PAD_RIGHT);
    InputSetScriptedDevices({});
    for (int i = 0; i < 7; ++i) {
        Tick();
        MenuMouseNoteChoices(this, 42, choices, 1, true);
        if (i < 6) EXPECT_EQ(MenuMouseButtons(), 0);
        else EXPECT_EQ(MenuMouseButtons(), PAD_CROSS);
    }
    Tick();
    MenuMouseNoteChoices(this, 42, choices, 1, true);
    EXPECT_EQ(MenuMouseButtons(), 0);
}

TEST_F(MenuMouseInput, NewMessageDiscardsThePreviousPendingClick) {
    const MenuMouseRect choices[] = {{20, 200, 70, 50}, {200, 200, 100, 60}};
    MenuMouseEnable(true);
    MenuMouseNoteChoices(this, 42, choices, 0, true);
    InputSetScriptedDevices({.mouse_buttons = 1});
    EXPECT_EQ(MenuMouseButtons(), PAD_RIGHT);
    InputSetScriptedDevices({});
    for (int i = 0; i < 9; ++i) {
        Tick();
        MenuMouseNoteChoices(this, 43, choices, 1, true);
        EXPECT_EQ(MenuMouseButtons(), 0);
    }
}

TEST_F(MenuMouseInput, MenuCloseKeepsHeldClickAwayFromGameplayUntilRelease) {
    MenuMouseEnable(true);
    InputSetScriptedDevices({.mouse_buttons = 1});
    EXPECT_EQ(MenuMouseButtons(), PAD_CROSS);
    MenuMouseEnable(false);
    EXPECT_EQ(MenuMouseButtons(), 0);
    EXPECT_EQ(InputApplyKeyboardMouse({}, {.mouse_buttons = 1}).buttons, 0);
    InputSetScriptedDevices({});
    Tick();
    MenuMouseTake();
    EXPECT_EQ(InputApplyKeyboardMouse({}, {.mouse_buttons = 1}).buttons, kInputCross);
}
}
