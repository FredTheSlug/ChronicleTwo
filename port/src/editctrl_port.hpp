#pragma once
#include "editctrl.hpp"

// Retail town control state and unit-local control entry points.
extern int          PortEditLadderMode asm("EditCtrl_LadderMode");
extern EditViewMode PortEditViewMode asm("EditCtrl_ViewMode");
extern float        PortEditEyeHeading asm("EditCtrl_EyeHeading");
extern float        PortEditEyeTilt asm("EditCtrl_EyeTilt");
void                PortEditLadderControl(CScene *, CPadControl *);
void                PortEditCharaControl(CScene *, CPadControl *);
void                PortEditCameraControl(CScene *, CPadControl *);
