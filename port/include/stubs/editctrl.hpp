#pragma once

#include <editctrl.hpp>

// Give the native caller access to the retail eye state and static control
// helpers, using the same linkage seam as the main-loop port stubs.
extern int          LadderMode asm("EditCtrl_LadderMode");
extern EditViewMode ViewMode asm("EditCtrl_ViewMode");
extern float        viewAngleH asm("EditCtrl_EyeHeading");
extern float        viewAngleV asm("EditCtrl_EyeTilt");

static void LadderControl(CScene *scene, CPadControl *pad);
static void CharaControl(CScene *scene, CPadControl *pad);
static void CameraControl(CScene *scene, CPadControl *pad);

void PortEditLadderControl(CScene *scene, CPadControl *pad) { LadderControl(scene, pad); }

void PortEditCharaControl(CScene *scene, CPadControl *pad) { CharaControl(scene, pad); }

void PortEditCameraControl(CScene *scene, CPadControl *pad) { CameraControl(scene, pad); }
