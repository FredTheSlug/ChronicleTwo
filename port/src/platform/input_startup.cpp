#include "input_startup.hpp"

#include "platform/input.hpp"
#include "platform/settings.hpp"

void InputInitForRun(bool headless) {
    if (headless) {
        // InputPoll and script overrides work without physical devices or window event hooks.
        // Apply the same bindings and mouse settings without opening SDL's gamepad subsystem.
        InputApplyConfig(SettingsGet());
    } else {
        InputInit();
    }
}
