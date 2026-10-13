#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numbers>

#include "camera_control_port.hpp"
#include "dng_main.hpp"
#include "editctrl_port.hpp"
#include "editloop.hpp"
#include "first_person_walk.hpp"
#include "mainloop.hpp"
#include "mainloop_port.hpp"
#include "photo.hpp"
#include "platform/input_script.hpp"
#include "stubs/photo.hpp"
#include "userdata.hpp"

TEST(FirstPersonWalk, ViewRelativeForwardBackAndStrafePreserveMagnitude) {
    for (float heading : {0.0f, 0.7f, -1.3f, std::numbers::pi_v<float>}) {
        for (float angle : {0.0f, 1.0f, -2.0f}) {
            for (auto key : {
                     InputKeyboardMovement{0,  -1},
                     {0,  1 },
                     {1,  0 },
                     {-1, 0 },
                     {1,  -1}
            }) {
                const auto  stick = FirstPersonWalkStick(key, heading, angle);
                const float x = stick.x * std::cos(angle) + stick.y * std::sin(angle);
                const float z = -stick.x * std::sin(angle) + stick.y * std::cos(angle);
                const float length = std::hypot(key.x, key.y);
                EXPECT_NEAR(x, (-key.x * std::cos(heading) - key.y * std::sin(heading)) / length, 0.00001f);
                EXPECT_NEAR(z, (key.x * std::sin(heading) - key.y * std::cos(heading)) / length, 0.00001f);
                EXPECT_NEAR(std::hypot(x, z), 1, 0.00001f);
            }
        }
    }
    const auto invalid = FirstPersonWalkStick({0, -1}, std::numeric_limits<float>::infinity(), 0);
    EXPECT_EQ(invalid.x, 0);
    EXPECT_EQ(invalid.y, 0);
}

TEST(FirstPersonWalk, TownGatesSpecialOwnersAndKeepsPadLook) {
    auto          &scene = *GetMainScene();
    CCharacter2    chara;
    CCameraControl camera;
    camera.ControlOff();
    camera.FollowOff();
    scene.AssignCamera(0, &camera, const_cast<char *>("MainCam"));
    scene.AssignChara(0, &chara, const_cast<char *>("player"));
    scene.player_chara = scene.active_camera = scene.event_run = 0;
    PortLoopNo = LOOP_EDIT;
    PortTownLoopMode = EDIT_LOOP_WALK;
    PortTownControlMode = EDIT_CONTROL_PLAYER;
    PortTownLockChara = PortTownPause = PortEditLadderMode = 0;
    PortEditViewMode = EDIT_VIEW_MODE_EYE;
    GamePad__2.key_lock = DebugInfo.debug_camera = 0;
    ASSERT_TRUE(TownFirstPersonOwns(&scene, &PadCtrl));
    for (int gate = 0; gate < 9; ++gate) {
        if (gate == 0) {
            PortEditViewMode = EDIT_VIEW_MODE_PHOTO;
        }
        if (gate == 1) {
            PortTownPause = 1;
        }
        if (gate == 2) {
            PortTownLockChara = 1;
        }
        if (gate == 3) {
            scene.event_run = 1;
        }
        if (gate == 4) {
            GamePad__2.key_lock = 1;
        }
        if (gate == 5) {
            PortTownControlMode = EDIT_CONTROL_EVENT;
        }
        if (gate == 6) {
            PortTownLoopMode = EDIT_LOOP_WALK_MENU;
        }
        if (gate == 7) {
            PortEditLadderMode = EDIT_LADDER_MODE_BOTTOM;
        }
        if (gate == 8) {
            camera.FollowOn();
        }
        EXPECT_FALSE(TownFirstPersonOwns(&scene, &PadCtrl)) << gate;
        PortEditViewMode = EDIT_VIEW_MODE_EYE;
        PortTownPause = PortTownLockChara = scene.event_run = GamePad__2.key_lock = PortEditLadderMode = 0;
        PortTownControlMode = EDIT_CONTROL_PLAYER;
        PortTownLoopMode = EDIT_LOOP_WALK;
        camera.FollowOff();
    }
    InputResetBindings();
    InputScript script;
    std::string error;
    ASSERT_TRUE(InputScriptParse("0 key:w key:d 220 128 128 128\n", script, error));
    InputScriptInstall(script);
    auto state = InputGetPad(0);
    GamePad__2.pad[0].left_x = state.left_x;
    GamePad__2.pad[0].left_y = state.left_y;
    EXPECT_FLOAT_EQ(GamePad__2.GetLYf(), 0);
    EXPECT_GT(GamePad__2.GetLXf(), 0.5f);
    PortEditViewMode = EDIT_VIEW_MODE_PHOTO;
    EXPECT_LT(GamePad__2.GetLYf(), -0.5f);
    InputScriptInstall({});
}

TEST(FirstPersonWalk, DungeonGatesPhotoRidepodPauseEventsAndActions) {
    CScene           scene;
    CCameraControl   camera;
    CActionChara     chara;
    CUserDataManager user;
    DngMainScene = &scene;
    BattleAreaScene = &scene.battle_area;
    MainChara__2 = &chara;
    DngUserData = &user;
    PortLoopNo = LOOP_DUNGEON;
    DngStatus = {};
    DngStatus.eye_view = 1;
    scene.active_camera = scene.event_run = 0;
    scene.AssignCamera(0, &camera, const_cast<char *>("MainCam"));
    camera.FollowOff();
    scene.battle_area.camera_mode = DNG_CAMERA_FOLLOW;
    scene.battle_area.pause_flag = 0;
    scene.battle_area.script.event_no = -1;
    scene.battle_area.script.running = 0;
    user.active_chr_no = USER_CHARA_MAX;
    chara.menu_flag = 1;
    chara.hold_type = ACTION_HOLD_NONE;
    chara.lock_on = 0;
    chara.damage_req = ACTION_DAMAGE_REQ_NONE;
    PortDungeonDebugPause = GamePad__2.key_lock = DebugInfo.debug_camera = 0;
    TakePhotoMode = TAKE_PHOTO_OFF;
    ASSERT_TRUE(DungeonFirstPersonOwns(&camera));
    for (int gate = 0; gate < 8; ++gate) {
        if (gate == 0) {
            TakePhotoMode = TAKE_PHOTO_AIM;
        }
        if (gate == 1) {
            user.active_chr_no = USER_CHARA_ROBO;
        }
        if (gate == 2) {
            scene.battle_area.pause_flag = DNG_PAUSE_PLAYER_CONTROL;
        }
        if (gate == 3) {
            scene.battle_area.script.running = 1;
        }
        if (gate == 4) {
            PortDungeonDebugPause = 1;
        }
        if (gate == 5) {
            chara.menu_flag = 0;
        }
        if (gate == 6) {
            DngStatus.mode = DNG_STATUS_MENU;
        }
        if (gate == 7) {
            chara.damage_req = ACTION_DAMAGE_REQ_DEAD;
        }
        EXPECT_FALSE(DungeonFirstPersonOwns(&camera)) << gate;
        TakePhotoMode = TAKE_PHOTO_OFF;
        user.active_chr_no = USER_CHARA_MAX;
        scene.battle_area.pause_flag = scene.battle_area.script.running = PortDungeonDebugPause = 0;
        chara.menu_flag = 1;
        DngStatus.mode = DNG_STATUS_FIELD;
        chara.damage_req = ACTION_DAMAGE_REQ_NONE;
    }
    DngMainScene = nullptr;
    BattleAreaScene = nullptr;
    MainChara__2 = nullptr;
    DngUserData = nullptr;
}
