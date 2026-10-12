#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <utility>

#include "platform/input.hpp"
#include "platform/input_script.hpp"
#include "platform/input_startup.hpp"

TEST(InputStartup, HeadlessLeavesControllerSubsystemsOff) {
    ASSERT_EQ(SDL_WasInit(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK), 0u);
    InputInitForRun(true);
    EXPECT_EQ(SDL_WasInit(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK), 0u);
    InputShutdown();
}

TEST(InputStartup, HeadlessKeepsScriptedFirstPad) {
    InputScript script;
    std::string error;
    ASSERT_TRUE(InputScriptParse("0 cross start 255 128 128 128\n", script, error)) << error;
    InputInitForRun(true);
    InputScriptInstall(std::move(script));
    InputPoll();
    InputScriptApply(0);
    const InputPadState &actual = InputGetPad(0);
    EXPECT_TRUE(actual.connected);
    EXPECT_EQ(actual.buttons, kInputCross | kInputStart);
    EXPECT_EQ(actual.left_x, 255);
    InputScriptInstall({});
    InputShutdown();
}

TEST(InputStartup, HeadlessKeepsScriptedKeyboard) {
    InputScript script;
    std::string error;
    ASSERT_TRUE(InputScriptParse("0 key:Space\n", script, error)) << error;
    InputInitForRun(true);
    InputScriptInstall(std::move(script));
    InputPoll();
    InputScriptApply(0);
    EXPECT_NE(InputGetPad(0).buttons & kInputCross, 0u);
    InputScriptInstall({});
    InputShutdown();
}

TEST(InputStartup, WindowedStillInitializesControllers) {
    InputInitForRun(false);
    EXPECT_EQ(SDL_WasInit(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK), SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK);
    InputShutdown();
}
