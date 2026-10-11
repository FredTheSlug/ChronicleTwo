#include "common.h"

#include "gamepad.hpp"

// The recording buffer sits at a dev-kit absolute address (PAD_CAPTURE_BUFFER), and the file I/O
// goes through the PS2 host file system.
PORT_OVERRIDE void CGamePad::Capture(PAD_STATUS *status) {
    PS2_UNIMPLEMENTED();
}

PORT_OVERRIDE void CGamePad::Play(PAD_STATUS *status) {
    PS2_UNIMPLEMENTED();
}

PORT_OVERRIDE void CGamePad::SaveCapture() {
    PS2_UNIMPLEMENTED();
}

PORT_OVERRIDE void CGamePad::LoadCapture() {
    PS2_UNIMPLEMENTED();
}
