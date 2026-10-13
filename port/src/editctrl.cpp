#include "common.h"
#include "editctrl.hpp"
#include "camera_mouse.hpp"
#include "editloop.hpp"
#include "mainloop.hpp"
#include "subgame.hpp"
#include "photo.hpp"
#include "platform/input.hpp"

#include <cmath>

extern int PortEditLadderMode asm("EditCtrl_LadderMode");
extern EditViewMode PortEditViewMode asm("EditCtrl_ViewMode");
extern float PortEditEyeHeading asm("EditCtrl_EyeHeading");
extern float PortEditEyeTilt asm("EditCtrl_EyeTilt");
void PortEditLadderControl(CScene *, CPadControl *);
void PortEditCharaControl(CScene *, CPadControl *);
void PortEditCameraControl(CScene *, CPadControl *);

void TownMouseEyeAfterControl(CScene *scene, CPadControl *pad) {
    const InputMouseLook &look = InputGetMouseLook();
    static std::uint64_t consumed_read = 0;
    if (look.read == consumed_read) return;
    consumed_read = look.read;
    if (!scene || !pad || GetNowLoopNo() != LOOP_EDIT || IsEditMode() ||
        (PortEditViewMode != EDIT_VIEW_MODE_EYE &&
         !(PortEditViewMode == EDIT_VIEW_MODE_PHOTO && NowTakePhoto())) || PortEditLadderMode != EDIT_LADDER_MODE_NONE ||
        !EditOnGround() || scene->event_run || scene->active_camera != 0 ||
        GamePad__2.key_lock || SubGameRunning() || DebugInfo.debug_camera || mgCCamera::StopCamera) return;
    mgCCamera *base = scene->GetCamera(0);
    CCharacter2 *chara = scene->GetCharacter(scene->player_chara);
    if (!base || !chara || base->Iam() != CAMERA_KIND_CONTROL || base->suspended) return;
    auto *camera = static_cast<CCameraControl *>(base);
    if (camera->follow_on || camera->control_on || (camera->rot_cancel & CAMERA_ROT_CANCEL_ANALOG)) return;
    sceVu0FVECTOR eye;
    chara->GetPosition(eye);
    eye[1] += 28;
    for (int i = 0; i < 3; ++i)
        if (std::fabs(camera->pos[i] - eye[i]) > 0.001f ||
            std::fabs(camera->next_pos[i] - eye[i]) > 0.001f) return;
    if (MouseCameraEye(*camera, PortEditEyeHeading, PortEditEyeTilt, look.yaw, look.pitch)) {
        camera->mgCCamera::Step(0);
        camera->angle = camera->next_angle = camera->angle_h;
    }
}

PORT_OVERRIDE int EditControl(CScene *scene, CPadControl *pad) {
    // Keep retail movement, ladder, eye-entry/exit and photo behavior, granting
    // native eye look only after those helpers establish this frame's owner.
    if (PortEditLadderMode != EDIT_LADDER_MODE_NONE) {
        PortEditLadderControl(scene, pad);
    } else {
        PortEditCharaControl(scene, pad);
        PortEditCameraControl(scene, scene->event_run ? nullptr : pad);
    }
    TownMouseEyeAfterControl(scene, pad);
    return 0;
}
