#include "common.h"

#define CAMERA_CONTROL_USE_RETAIL_ASSIGNMENT
#include "cameracontrol.hpp"
#undef CAMERA_CONTROL_USE_RETAIL_ASSIGNMENT

#include "camera_mouse.hpp"
#include "editctrl.hpp"
#include "editloop.hpp"
#include "gamepad.hpp"
#include "mainloop.hpp"
#include "scenesnd.hpp"
#include "subgame.hpp"
#include "platform/input.hpp"

#include <algorithm>
#include <cmath>

CameraCtrlParam &CameraCtrlParam::operator=(const CameraCtrlParam &source) {
    min_dist = source.min_dist;
    max_dist = source.max_dist;
    near_height = source.near_height;
    far_height = source.far_height;
    height = source.height;
    max_height = source.max_height;
    min_height = source.min_height;
    rest_max_height = source.rest_max_height;
    rest_min_height = source.rest_min_height;
    ground_space = source.ground_space;
    no_check = source.no_check;
    return *this;
}

PORT_OVERRIDE void CCameraControl::MoveCamera(CPadControl *pad, float *player_rot, CCPoly *polys, int poly_count) {
    Control control{};
    if (pad) {
        if (!(rot_cancel & CAMERA_ROT_CANCEL_ANALOG)) control.rot = -0.05f * pad->Analog(PAD_ANALOG_RIGHT_X_2);
        if (!(rot_cancel & CAMERA_ROT_CANCEL_BUTTON)) {
            if (pad->Btn(PAD_BTN_L1_HELD)) control.rot = 0.05f;
            if (pad->Btn(PAD_BTN_R1_HELD)) control.rot = -0.05f;
        }
        if (rot_reverse) control.rot = -control.rot;
        control.height = -2.0f * pad->Analog(PAD_ANALOG_RIGHT_Y_2);
        control.rot_back = (pad->Btn(PAD_BTN_L2) || pad->Btn(PAD_BTN_CANCEL)) ? 1 : 0;
    }
    // Controller movement, wall correction, floor clearance and swing-back keep
    // their retail implementation. Mouse yaw is a separate, immediate pose turn.
    MoveCamera(&control, player_rot, polys, poly_count);

    const InputMouseLook &look = InputGetMouseLook();
    static std::uint64_t consumed_read = 0;
    if (look.read == consumed_read) return;
    consumed_read = look.read;
    if (control.rot_back) return;
    CScene *scene = GetMainScene();
    const bool walking = GetNowLoopNo() == LOOP_EDIT && !IsEditMode() && IsWalkMode();
    if (!TownMouseCameraOwns(this, scene, pad, walking, SubGameRunning(), GamePad__2.key_lock) ||
        (look.yaw == 0 && look.pitch == 0) || !std::isfinite(look.yaw) || !std::isfinite(look.pitch)) return;

    // The retail collection encloses the old eye and cannot cover a mouse flick.
    // Collect the full current/pending orbit before validating a continuous turn.
    float radius = 5;
    for (auto pair : {std::pair{pos, ref}, std::pair{next_pos, next_ref}}) {
        float squared = 0;
        for (int i = 0; i < 3; ++i) squared += (pair.first[i] - pair.second[i]) * (pair.first[i] - pair.second[i]);
        radius = std::max(radius, std::sqrt(squared) + 5.0f);
    }
    const CameraCtrlParam *active = GetActiveParam();
    radius = std::max(radius, std::max(std::fabs(active->min_height), std::fabs(active->max_height)) +
                              std::max(std::fabs(active->near_height), std::fabs(active->far_height)) +
                              active->ground_space + 20.0f);
    if (!std::isfinite(radius)) return;
    mgVu0FBOX bounds{};
    for (int i = 0; i < 3; ++i) {
        bounds.min[i] = std::min(ref[i], next_ref[i]) - radius;
        bounds.max[i] = std::max(ref[i], next_ref[i]) + radius;
    }
    bounds.min[3] = bounds.max[3] = 1;
    CCPoly orbit_polys[512];
    const int count = MouseCameraCollectPolys(*scene, bounds, orbit_polys, 512);
    // A full collector cannot prove that the corridor contains every polygon.
    if (count < 0 || count >= 512) return;
    if (MouseCameraYaw(*this, look.yaw, orbit_polys, count) != 0) CancelRotBack();
    MouseCameraPitch(*this, look.pitch, orbit_polys, count);
}
