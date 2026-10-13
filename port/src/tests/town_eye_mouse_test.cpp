#include <gtest/gtest.h>
#include "editctrl.hpp"
#include "mainloop.hpp"
#include "platform/input.hpp"

extern int PortLoopNo asm("MainLoop_LoopNo");
extern EditViewMode PortTownViewMode asm("EditCtrl_ViewMode");
extern float PortTownEyeHeading asm("EditCtrl_EyeHeading");
extern float PortTownEyeTilt asm("EditCtrl_EyeTilt");
extern int PortTownLadderMode asm("EditCtrl_LadderMode");
void TownMouseEyeAfterControl(CScene *, CPadControl *);

TEST(TownEyeMouse, NativeStateAndBothLookTargetsTurnOnceAndDropOtherOwnersMotion) {
    const int old_loop = PortLoopNo;
    const auto old_mode = PortTownViewMode;
    const float old_heading = PortTownEyeHeading, old_tilt = PortTownEyeTilt;
    const auto old_lock = GamePad__2.key_lock;
    const auto old_debug = DebugInfo.debug_camera;
    const int old_ladder = PortTownLadderMode;
    CScene scene;
    CCharacter2 chara;
    chara.SetPosition(0, 0, 0);
    scene.AssignChara(0, &chara, const_cast<char *>("player"));
    scene.player_chara = 0;
    CCameraControl camera;
    camera.ControlOff();
    camera.FollowOff();
    camera.SetPos(0, 28, 0);
    camera.SetRef(0, 28, 10);
    scene.AssignCamera(0, &camera, const_cast<char *>("MainCam"));
    scene.active_camera = 0;
    PortLoopNo = LOOP_EDIT;
    PortTownViewMode = EDIT_VIEW_MODE_EYE;
    PortTownLadderMode = EDIT_LADDER_MODE_NONE;
    PortTownEyeHeading = PortTownEyeTilt = 0;
    DebugInfo.debug_camera = GamePad__2.key_lock = 0;
    CPadControl pad{};
    InputResetBindings();
    InputPadState input{.connected = true};
    InputSetOverride(0, &input);
    InputSetScriptedDevices({.mouse_dx = 100, .mouse_dy = 10000});
    InputLatchPad(0);
    TownMouseEyeAfterControl(&scene, &pad);
    EXPECT_LT(PortTownEyeHeading, -0.1f);
    EXPECT_FLOAT_EQ(PortTownEyeTilt, 0.65f);
    EXPECT_LT(camera.ref[0], -1);
    EXPECT_FLOAT_EQ(camera.ref[0], camera.next_ref[0]);
    EXPECT_FLOAT_EQ(camera.pos[1], 28);
    const float heading = PortTownEyeHeading;
    TownMouseEyeAfterControl(&scene, &pad);
    EXPECT_FLOAT_EQ(PortTownEyeHeading, heading);
    for (int owner = 0; owner < 10; ++owner) {
        PortTownViewMode = owner == 0 ? EDIT_VIEW_MODE_WALK :
                           owner == 1 ? EDIT_VIEW_MODE_PHOTO : EDIT_VIEW_MODE_EYE;
        scene.event_run = owner == 2;
        GamePad__2.key_lock = owner == 3;
        DebugInfo.debug_camera = owner == 4;
        camera.suspended = owner == 5;
        scene.active_camera = owner == 6 ? 1 : 0;
        PortTownLadderMode = owner == 7 ? EDIT_LADDER_MODE_BOTTOM : EDIT_LADDER_MODE_NONE;
        camera.follow_on = owner == 8;
        InputLatchPad(0);
        TownMouseEyeAfterControl(&scene, owner == 9 ? nullptr : &pad);
        PortTownViewMode = EDIT_VIEW_MODE_EYE;
        scene.event_run = GamePad__2.key_lock = DebugInfo.debug_camera = 0;
        camera.suspended = camera.follow_on = scene.active_camera = 0;
        PortTownLadderMode = EDIT_LADDER_MODE_NONE;
        TownMouseEyeAfterControl(&scene, &pad);
        EXPECT_FLOAT_EQ(PortTownEyeHeading, heading) << owner;
    }
    InputSetScriptedDevices({.mouse_dy = -10000});
    InputLatchPad(0);
    TownMouseEyeAfterControl(&scene, &pad);
    EXPECT_FLOAT_EQ(PortTownEyeTilt, -1.0f);
    InputSetScriptedDevices({});
    InputSetOverride(0, nullptr);
    PortLoopNo = old_loop;
    PortTownViewMode = old_mode;
    PortTownEyeHeading = old_heading;
    PortTownEyeTilt = old_tilt;
    GamePad__2.key_lock = old_lock;
    DebugInfo.debug_camera = old_debug;
    PortTownLadderMode = old_ladder;
}
