#include <gtest/gtest.h>

#include "editctrl.hpp"
#include "editctrl_port.hpp"
#include "mainloop.hpp"
#include "mainloop_port.hpp"
#include "platform/input.hpp"

void TownMouseEyeAfterControl(CScene *, CPadControl *);

TEST(TownEyeMouse, NativeStateAndBothLookTargetsTurnOnceAndDropOtherOwnersMotion) {
    const int   old_loop = PortLoopNo;
    const auto  old_mode = PortEditViewMode;
    const float old_heading = PortEditEyeHeading, old_tilt = PortEditEyeTilt;
    const auto  old_lock = GamePad__2.key_lock;
    const auto  old_debug = DebugInfo.debug_camera;
    const int   old_ladder = PortEditLadderMode;
    CScene      scene;
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
    PortEditViewMode = EDIT_VIEW_MODE_EYE;
    PortEditLadderMode = EDIT_LADDER_MODE_NONE;
    PortEditEyeHeading = PortEditEyeTilt = 0;
    DebugInfo.debug_camera = GamePad__2.key_lock = 0;
    CPadControl pad{};
    InputResetBindings();
    InputPadState input{.connected = true};
    InputSetOverride(0, &input);
    InputSetScriptedDevices({.mouse_dx = 100, .mouse_dy = 10000});
    InputLatchPad(0);
    TownMouseEyeAfterControl(&scene, &pad);
    EXPECT_LT(PortEditEyeHeading, -0.1f);
    EXPECT_FLOAT_EQ(PortEditEyeTilt, 0.65f);
    EXPECT_LT(camera.ref[0], -1);
    EXPECT_FLOAT_EQ(camera.ref[0], camera.next_ref[0]);
    EXPECT_FLOAT_EQ(camera.pos[1], 28);
    const float heading = PortEditEyeHeading;
    TownMouseEyeAfterControl(&scene, &pad);
    EXPECT_FLOAT_EQ(PortEditEyeHeading, heading);
    for (int owner = 0; owner < 10; ++owner) {
        PortEditViewMode = owner == 0 ? EDIT_VIEW_MODE_WALK : owner == 1 ? EDIT_VIEW_MODE_PHOTO
                                                                         : EDIT_VIEW_MODE_EYE;
        scene.event_run = owner == 2;
        GamePad__2.key_lock = owner == 3;
        DebugInfo.debug_camera = owner == 4;
        camera.suspended = owner == 5;
        scene.active_camera = owner == 6 ? 1 : 0;
        PortEditLadderMode = owner == 7 ? EDIT_LADDER_MODE_BOTTOM : EDIT_LADDER_MODE_NONE;
        camera.follow_on = owner == 8;
        InputLatchPad(0);
        TownMouseEyeAfterControl(&scene, owner == 9 ? nullptr : &pad);
        PortEditViewMode = EDIT_VIEW_MODE_EYE;
        scene.event_run = GamePad__2.key_lock = DebugInfo.debug_camera = 0;
        camera.suspended = camera.follow_on = scene.active_camera = 0;
        PortEditLadderMode = EDIT_LADDER_MODE_NONE;
        TownMouseEyeAfterControl(&scene, &pad);
        EXPECT_FLOAT_EQ(PortEditEyeHeading, heading) << owner;
    }
    InputSetScriptedDevices({.mouse_dy = -10000});
    InputLatchPad(0);
    TownMouseEyeAfterControl(&scene, &pad);
    EXPECT_FLOAT_EQ(PortEditEyeTilt, -1.0f);
    InputSetScriptedDevices({});
    InputSetOverride(0, nullptr);
    PortLoopNo = old_loop;
    PortEditViewMode = old_mode;
    PortEditEyeHeading = old_heading;
    PortEditEyeTilt = old_tilt;
    GamePad__2.key_lock = old_lock;
    DebugInfo.debug_camera = old_debug;
    PortEditLadderMode = old_ladder;
}
