#include "common.h"

#include <cmath>

#include "camera_control_port.hpp"
#include "camera_mouse.hpp"
#include "dng_main.hpp"
#include "editctrl_port.hpp"
#include "editloop.hpp"
#include "first_person_walk.hpp"
#include "mainloop.hpp"
#include "photo.hpp"
#include "subgame.hpp"
#include "userdata.hpp"

namespace {
bool                  walking_script = false;
InputKeyboardMovement script_stick;
CActionChara         *walking_chara = nullptr;
CScene               *walking_scene = nullptr;
std::uint64_t         walking_read = 0;
} // namespace

InputKeyboardMovement FirstPersonWalkStick(InputKeyboardMovement keys, float heading, float camera_angle) {
    if (!std::isfinite(keys.x) || !std::isfinite(keys.y) || !std::isfinite(heading) || !std::isfinite(camera_angle)) {
        return {};
    }
    const float length = std::hypot(keys.x, keys.y);
    if (length > 1) {
        keys.x /= length;
        keys.y /= length;
    }
    const float x = -keys.x * std::cos(heading) - keys.y * std::sin(heading);
    const float z = keys.x * std::sin(heading) - keys.y * std::cos(heading);
    return {x * std::cos(camera_angle) - z * std::sin(camera_angle),
            x * std::sin(camera_angle) + z * std::cos(camera_angle)};
}

bool TownFirstPersonOwns(CScene *scene, CPadControl *pad) {
    if (!scene || !pad || GetNowLoopNo() != LOOP_EDIT || IsEditMode() ||
        PortTownLoopMode != EDIT_LOOP_WALK || PortTownControlMode != EDIT_CONTROL_PLAYER ||
        PortTownLockChara || PortTownPause || PortEditViewMode != EDIT_VIEW_MODE_EYE ||
        PortEditLadderMode != EDIT_LADDER_MODE_NONE || scene->event_run || scene->active_camera != 0 ||
        GamePad__2.key_lock || SubGameRunning() || NowTakePhoto() || DebugInfo.debug_camera || mgCCamera::StopCamera) {
        return false;
    }
    auto *base = scene->GetCamera(0);
    if (!base || base->Iam() != CAMERA_KIND_CONTROL || base->suspended || !scene->GetCharacter(scene->player_chara)) {
        return false;
    }
    auto *camera = static_cast<CCameraControl *>(base);
    return !camera->follow_on && !camera->control_on && !(camera->rot_cancel & CAMERA_ROT_CANCEL_ANALOG);
}

bool DungeonFirstPersonOwns(CCameraControl *camera) {
    auto      *scene = DngMainScene;
    auto      *area = BattleAreaScene;
    auto      *chara = MainChara__2;
    const auto read = InputGetMouseLook().read;
    const bool continuing = chara && chara == walking_chara && scene == walking_scene && (read == walking_read || read == walking_read + 1);
    return GetNowLoopNo() == LOOP_DUNGEON && DngStatus.mode == DNG_STATUS_FIELD && DngStatus.eye_view &&
           camera && scene && area && chara && !NowTakePhoto() && !PortDungeonDebugPause &&
           !SubGameRunning() && !GamePad__2.key_lock && !DebugInfo.debug_camera &&
           DngUserData && (DngUserData->active_chr_no == USER_CHARA_MAX || DngUserData->active_chr_no == USER_CHARA_MONICA) &&
           !scene->event_run && scene->active_camera == 0 && scene->GetCamera(0) == camera &&
           !camera->follow_on && !camera->suspended && !mgCCamera::StopCamera &&
           !(camera->rot_cancel & CAMERA_ROT_CANCEL_ANALOG) &&
           (area->camera_mode == DNG_CAMERA_FOLLOW || area->camera_mode == DNG_CAMERA_FOLLOW_LOW) &&
           !(area->pause_flag & (DNG_PAUSE_PLAYER_STEP | DNG_PAUSE_PLAYER_CONTROL | DNG_PAUSE_FIELD_STEP | DNG_PAUSE_PAD_RESET)) &&
           area->script.event_no == -1 && !area->script.running &&
           chara->damage_req == ACTION_DAMAGE_REQ_NONE && !chara->lock_on && chara->hold_type == ACTION_HOLD_NONE &&
           (chara->CheckRunEvent() || continuing);
}

bool FirstPersonLookOwns() {
    if (GetNowLoopNo() == LOOP_EDIT) {
        return TownFirstPersonOwns(GetMainScene(), &PadCtrl);
    }
    if (GetNowLoopNo() != LOOP_DUNGEON || !DngMainScene) {
        return false;
    }
    auto *camera = DngMainScene->GetCamera(DngMainScene->active_camera);
    return camera && camera->Iam() == CAMERA_KIND_CONTROL && DungeonFirstPersonOwns(static_cast<CCameraControl *>(camera));
}

// The retail eye routines read these float getters. Keep controller look while
// keyboard movement travels through the independent channel and normal solver.
PORT_OVERRIDE float CGamePad::GetLXf() {
    if (this == &GamePad__2 && walking_script) {
        return script_stick.x;
    }
    if (this == &GamePad__2 && InputGetPad(0).keyboard_left && FirstPersonLookOwns()) {
        CGamePad look = *this;
        look.pad[0].left_x = InputGetLookPad(0).left_x;
        return look.GetLX() / 128.0f;
    }
    return GetLX() / 128.0f;
}

PORT_OVERRIDE float CGamePad::GetLYf() {
    if (this == &GamePad__2 && walking_script) {
        return script_stick.y;
    }
    if (this == &GamePad__2 && InputGetPad(0).keyboard_left && FirstPersonLookOwns()) {
        CGamePad look = *this;
        look.pad[0].left_y = InputGetLookPad(0).left_y;
        return look.GetLY() / 128.0f;
    }
    return GetLY() / 128.0f;
}

bool TownFirstPersonWalk(CScene *scene, CPadControl *pad) {
    if (!TownFirstPersonOwns(scene, pad)) {
        return false;
    }
    const auto keys = InputGetKeyboardMovement();
    if (keys.x == 0 && keys.y == 0) {
        return false;
    }
    auto       *camera = static_cast<CCameraControl *>(scene->GetCamera(0));
    const auto  stick = FirstPersonWalkStick(keys, PortEditEyeHeading, camera->GetAngle());
    CPadControl movement = *pad;
    movement.analog[PAD_ANALOG_LEFT_X_2].value = stick.x;
    movement.analog[PAD_ANALOG_LEFT_Y_2].value = stick.y;
    // Only the movement helper sees WALK. It retains speed, gravity, collisions,
    // landing and map events. A door/ladder event may deliberately end eye view.
    PortEditViewMode = EDIT_VIEW_MODE_WALK;
    PortEditCharaControl(scene, &movement);
    if (!scene->event_run && PortEditLadderMode == EDIT_LADDER_MODE_NONE) {
        PortEditViewMode = EDIT_VIEW_MODE_EYE;
    }
    return true;
}

void DungeonFirstPersonWalk(CCameraControl &camera) {
    if (!DungeonFirstPersonOwns(&camera)) {
        walking_chara = nullptr;
        walking_scene = nullptr;
        return;
    }
    const auto keys = InputGetKeyboardMovement();
    if (keys.x == 0 && keys.y == 0 && walking_chara != MainChara__2) {
        return;
    }
    walking_chara = MainChara__2;
    walking_scene = DngMainScene;
    walking_read = InputGetMouseLook().read;
    sceVu0FVECTOR before, after;
    MainChara__2->GetPosition(before);
    script_stick = FirstPersonWalkStick(keys, viewAngleH__2, camera.GetAngle());
    const CPadControl saved_control = PadCtrl;
    const auto        saved_pad = GamePad__2.pad[0];
    const auto        saved_previous = GamePad__2.previous_pad[0];
    // Run only locomotion: no attack, item, menu or lock-on button leaks into the
    // action script while the player is hidden in first person.
    for (auto &button : PadCtrl.btn) {
        button.value = 0;
    }
    GamePad__2.pad[0].button = GamePad__2.previous_pad[0].button = 0;
    walking_script = true;
    MainChara__2->RunScript(DngMainScene, &ActionScriptEnv);
    walking_script = false;
    PadCtrl = saved_control;
    GamePad__2.pad[0] = saved_pad;
    GamePad__2.previous_pad[0] = saved_previous;
    MainChara__2->GetPosition(after);
    // Translate the eye by the displacement accepted by retail collision.
    for (int i = 0; i < 3; ++i) {
        const float delta = after[i] - before[i];
        camera.pos[i] += delta;
        camera.next_pos[i] += delta;
        camera.ref[i] += delta;
        camera.next_ref[i] += delta;
    }
    camera.mgCCamera::Step(0);
    if (keys.x == 0 && keys.y == 0 && MainChara__2->CheckRunEvent()) {
        walking_chara = nullptr;
        walking_scene = nullptr;
    }
}
