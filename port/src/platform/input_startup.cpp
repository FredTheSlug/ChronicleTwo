#include "input_startup.hpp"

#include "platform/input.hpp"
#include "platform/settings.hpp"

void InputInitForRun(bool headless) {
    if (headless) {
        // Bindings and mouse settings still apply to scripted input; no gamepad subsystem is opened.
        InputApplyConfig(SettingsGet());
    } else {
        InputInit();
    }
}
