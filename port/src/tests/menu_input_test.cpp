#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <utility>

#include "gamepad.hpp"
#include "mainloop.hpp"
#include "menumain.hpp"
#include "menusys.hpp"
#include "platform/clock.hpp"
#include "platform/input.hpp"
#include "platform/input_script.hpp"

namespace {

class MenuInput : public testing::Test {
protected:
    void SetUp() override {
        InputResetBindings();
        GamePad__2.MenuModeOff();
    }

    void TearDown() override {
        GamePad__2.MenuModeOff();
        GamePad__2.AutoRepeatOff();
        InputScriptInstall({});
        InputResetBindings();
    }

    static unsigned Buttons(int scancode) {
        InputKeyboardMouse held;
        held.keys.push_back(scancode);
        return InputApplyKeyboardMouse({}, held).buttons;
    }
};

TEST_F(MenuInput, EscapeTracksRetailMenuMode) {
    EXPECT_EQ(Buttons(SDL_SCANCODE_ESCAPE), kInputTriangle);
    GamePad__2.MenuModeOn(120);
    EXPECT_EQ(GamePad__2.axis_threshold[0], 120);
    EXPECT_EQ(Buttons(SDL_SCANCODE_ESCAPE), kInputCircle);
    GamePad__2.MenuModeOff();
    EXPECT_EQ(GamePad__2.axis_threshold[0], 0);
    EXPECT_EQ(Buttons(SDL_SCANCODE_ESCAPE), kInputTriangle);
}

TEST_F(MenuInput, MenuKeyEnvironmentPropagatesEveryLayout) {
    SetMenuKeyCtrlEnv(0);
    EXPECT_EQ(GamePad__2.axis_threshold[0], 120);
    EXPECT_EQ(Buttons(SDL_SCANCODE_ESCAPE), kInputCircle);
    SetMenuKeyCtrlEnv(1);
    EXPECT_EQ(GamePad__2.axis_threshold[0], 0);
    EXPECT_EQ(Buttons(SDL_SCANCODE_ESCAPE), kInputTriangle);
    SetMenuKeyCtrlEnv(2);
    EXPECT_EQ(GamePad__2.axis_threshold[0], 120);
    EXPECT_EQ(Buttons(SDL_SCANCODE_ESCAPE), kInputCircle);
    SetMenuKeyCtrlEnv(3);
    EXPECT_EQ(GamePad__2.axis_threshold[0], 0);
    EXPECT_EQ(Buttons(SDL_SCANCODE_ESCAPE), kInputTriangle);
}

TEST_F(MenuInput, MenuModePreservesThresholdAndSecondPort) {
    CGamePad pad{};
    pad.axis_threshold[1] = 37;
    for (int threshold : {0, -1, 64, 120}) {
        pad.MenuModeOn(threshold);
        EXPECT_EQ(pad.axis_threshold[0], threshold);
        EXPECT_EQ(pad.axis_threshold[1], 37);
        EXPECT_EQ(Buttons(SDL_SCANCODE_ESCAPE), kInputCircle);
        pad.MenuModeOff();
        EXPECT_EQ(pad.axis_threshold[0], 0);
        EXPECT_EQ(pad.axis_threshold[1], 37);
        EXPECT_EQ(Buttons(SDL_SCANCODE_ESCAPE), kInputTriangle);
    }
}

TEST_F(MenuInput, OtherDefaultKeysRetainTheirPadButtons) {
    for (bool menu : {false, true}) {
        if (menu) GamePad__2.MenuModeOn(120);
        EXPECT_EQ(Buttons(SDL_SCANCODE_TAB), kInputTriangle);
        EXPECT_EQ(Buttons(SDL_SCANCODE_RETURN), kInputStart);
        EXPECT_EQ(Buttons(SDL_SCANCODE_E), kInputSquare);
        EXPECT_EQ(Buttons(SDL_SCANCODE_SPACE), kInputCross);
    }
}

TEST_F(MenuInput, EnglishMenuReadsEscapeAsCancelAndSpaceAsConfirm) {
    const int language = LanguageCode;
    LanguageCode = LANG_ENGLISH;
    GamePad__2.key_lock = 0;
    GamePad__2.previous_pad[0].button = 0;
    GamePad__2.MenuModeOn(120);
    GamePad__2.pad[0].button = Buttons(SDL_SCANCODE_ESCAPE);
    EXPECT_EQ(MenuCheckPushButton(), MENU_PUSH_BUTTON_CANCEL);
    GamePad__2.pad[0].button = Buttons(SDL_SCANCODE_SPACE);
    EXPECT_EQ(MenuCheckPushButton(), MENU_PUSH_BUTTON_DECIDE);
    GamePad__2.pad[0].button = Buttons(SDL_SCANCODE_TAB);
    EXPECT_EQ(MenuCheckPushButton(), MENU_PUSH_BUTTON_TRIANGLE);
    EXPECT_EQ(ConvertCheckPushButton(MenuCheckPushButton()), MENU_PUSH_BUTTON_CANCEL);
    GamePad__2.pad[0].button = 0;
    LanguageCode = language;
}

TEST_F(MenuInput, RetailUpdateConvertsLeftStickOnlyPastMenuThreshold) {
    InputScript script;
    std::string error;
    ASSERT_TRUE(InputScriptParse("0 pad1\n0 pad2\n4 pad1 255 128 128 128\n5 pad1 0 128 128 128\n6 pad1 128 255 128 128\n7 pad1 128 0 128 128\n8 pad1 255 128 128 128\n", script, error)) << error;
    InputScriptInstall(std::move(script));
    ClockSetUnbounded(true);
    ClockReset();
    GamePad__2.Init();
    for (int frame = 0; frame < 4; ++frame) {
        InputScriptApply(frame);
        GamePad__2.UpDate();
    }
    constexpr int directions = PAD_RIGHT | PAD_LEFT | PAD_DOWN | PAD_UP;
    GamePad__2.MenuModeOn(120);
    int frame = 4;
    for (int direction : {PAD_RIGHT, PAD_LEFT, PAD_DOWN, PAD_UP}) {
        InputScriptApply(frame++);
        GamePad__2.UpDate();
        EXPECT_EQ(GamePad__2.pad[0].button & directions, direction);
    }
    GamePad__2.MenuModeOff();
    InputScriptApply(8);
    GamePad__2.UpDate();
    EXPECT_EQ(GamePad__2.GetLX(), 128);
    EXPECT_EQ(GamePad__2.pad[0].button & directions, 0);
    GamePad__2.MenuModeOn(128);
    GamePad__2.UpDate();
    EXPECT_EQ(GamePad__2.pad[0].button & directions, 0);
    GamePad__2.MenuModeOn(127);
    GamePad__2.UpDate();
    EXPECT_EQ(GamePad__2.pad[0].button & directions, PAD_RIGHT);
}

} // namespace
