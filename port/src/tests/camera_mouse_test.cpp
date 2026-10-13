#include <gtest/gtest.h>
#include <cmath>
#include <numbers>
#include "camera_mouse.hpp"
#include "cameracontrol.hpp"
#include "scenesnd.hpp"
#include "padcontrol.hpp"
#include "mainloop.hpp"
#include "platform/input.hpp"
#include "map.hpp"

extern int PortLoopNo asm("MainLoop_LoopNo");

namespace {
CCameraControl Camera() {
    CCameraControl camera;
    camera.SetPos(0, 40, 100);
    camera.SetRef(0, 40, 0);
    camera.SetFollow(0, 10, 0);
    camera.SetFollowOffset(0, 30, 0);
    camera.ControlOn();
    camera.SetSpeed(4, 3);
    return camera;
}
CCPoly Wall(float x) {
    CCPoly poly{};
    float vertices[3][4] = {{x, -1000, -1000, 1}, {x, 1000, -1000, 1}, {x, 0, 1000, 1}};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 4; ++j) poly.vertex[i][j] = vertices[i][j];
    poly.normal[0] = 1;
    return poly;
}
}

TEST(CameraMouse, DirectYawTurnsCurrentAndPendingPosesWithoutStickEasing) {
    auto camera = Camera();
    constexpr float yaw = std::numbers::pi_v<float> / 2;
    EXPECT_NEAR(MouseCameraYaw(camera, yaw, nullptr, 0), yaw, 0.0001f);
    EXPECT_NEAR(camera.pos[0], -100, 0.001f);
    EXPECT_NEAR(camera.pos[2], 0, 0.001f);
    EXPECT_NEAR(camera.next_pos[0], -100, 0.001f);
    EXPECT_FLOAT_EQ(camera.pos[1], 40);
    EXPECT_FLOAT_EQ(camera.ref[0], 0);
    EXPECT_FLOAT_EQ(camera.pos_speed, 4);
    camera.Step(1);
    EXPECT_NEAR(camera.pos[0], -100, 0.001f);
}

TEST(CameraMouse, SweepStopsBeforeAThinWallEvenWhenTheFinalEyeIsClear) {
    auto camera = Camera();
    auto wall = Wall(-50);
    const float applied = MouseCameraYaw(camera, std::numbers::pi_v<float>, &wall, 1);
    EXPECT_GT(applied, 0);
    EXPECT_LT(applied, std::numbers::pi_v<float> / 2);
    EXPECT_GT(camera.pos[0], -46.1f);
    EXPECT_NEAR(camera.pos[0], camera.next_pos[0], 0.001f);
    // Refused yaw is not saved: a clear reverse moves immediately.
    const float previous = camera.pos[0];
    EXPECT_LT(MouseCameraYaw(camera, -0.1f, &wall, 1), 0);
    EXPECT_GT(camera.pos[0], previous);
}

TEST(CameraMouse, InvalidStartingCorridorAndNonfiniteInputLeaveThePoseAlone) {
    auto camera = Camera();
    auto wall = Wall(0);
    EXPECT_EQ(MouseCameraYaw(camera, 0.5f, &wall, 1), 0);
    EXPECT_FLOAT_EQ(camera.pos[2], 100);
    EXPECT_EQ(MouseCameraYaw(camera, INFINITY, nullptr, 0), 0);
    EXPECT_EQ(MouseCameraYaw(camera, NAN, nullptr, 0), 0);
}

TEST(CameraMouse, PendingCorridorMustAlsoBeClear) {
    auto camera = Camera();
    camera.next_pos[0] = -80;
    auto wall = Wall(-50);
    EXPECT_EQ(MouseCameraYaw(camera, 0.5f, &wall, 1), 0);
    EXPECT_FLOAT_EQ(camera.pos[0], 0);
    EXPECT_FLOAT_EQ(camera.next_pos[0], -80);
}

TEST(CameraMouse, CameraOwnershipRejectsScriptsLocksOtherViewsAndOtherCameras) {
    auto camera = Camera();
    CScene scene;
    CPadControl pad{};
    scene.AssignCamera(0, &camera, const_cast<char *>("MainCam"));
    scene.active_camera = 0;
    EXPECT_TRUE(TownMouseCameraOwns(&camera, &scene, &pad, true, false, false));
    EXPECT_FALSE(TownMouseCameraOwns(&camera, &scene, nullptr, true, false, false));
    EXPECT_FALSE(TownMouseCameraOwns(&camera, &scene, &pad, false, false, false));
    EXPECT_FALSE(TownMouseCameraOwns(&camera, &scene, &pad, true, true, false));
    EXPECT_FALSE(TownMouseCameraOwns(&camera, &scene, &pad, true, false, true));
    scene.event_run = 1;
    EXPECT_FALSE(TownMouseCameraOwns(&camera, &scene, &pad, true, false, false));
    scene.event_run = 0;
    camera.rot_cancel = CAMERA_ROT_CANCEL_ANALOG;
    EXPECT_FALSE(TownMouseCameraOwns(&camera, &scene, &pad, true, false, false));
    camera.rot_cancel = 0;
    camera.ControlOff();
    EXPECT_FALSE(TownMouseCameraOwns(&camera, &scene, &pad, true, false, false));
    camera.ControlOn();
    camera.FollowOff();
    EXPECT_FALSE(TownMouseCameraOwns(&camera, &scene, &pad, true, false, false));
}

TEST(CameraMouse, TownControlConsumesOneSampleAndDoesNotReplayLockedMotion) {
    auto camera = Camera();
    CScene &scene = *GetMainScene();
    scene.AssignCamera(0, &camera, const_cast<char *>("MainCam"));
    scene.active_camera = 0;
    scene.event_run = 0;
    PortLoopNo = LOOP_EDIT;
    GamePad__2.key_lock = 0;
    InputResetBindings();
    InputPadState input{.connected = true};
    InputSetOverride(0, &input);
    InputSetScriptedDevices({.mouse_dx = 400});
    InputLatchPad(0);
    CPadControl pad{};
    pad.Initialize();
    float rotation[4]{};
    camera.MoveCamera(&pad, rotation, nullptr, 0);
    EXPECT_LT(camera.pos[0], -60);
    const float turned = camera.pos[0];
    camera.MoveCamera(&pad, rotation, nullptr, 0);
    EXPECT_FLOAT_EQ(camera.pos[0], turned);
    InputLatchPad(0);
    GamePad__2.key_lock = 1;
    camera.MoveCamera(&pad, rotation, nullptr, 0);
    EXPECT_FLOAT_EQ(camera.pos[0], turned);
    GamePad__2.key_lock = 0;
    camera.MoveCamera(&pad, rotation, nullptr, 0);
    EXPECT_FLOAT_EQ(camera.pos[0], turned);
    InputSetScriptedDevices({});
    InputSetOverride(0, nullptr);
}

TEST(CameraMouse, MultipleRevolutionsRetainTheRequestedFinalHeading) {
    auto camera = Camera();
    const float yaw = 4.5f * std::numbers::pi_v<float>;
    EXPECT_FLOAT_EQ(MouseCameraYaw(camera, yaw, nullptr, 0), yaw);
    EXPECT_NEAR(camera.pos[0], -100, 0.001f);
    EXPECT_NEAR(camera.pos[2], 0, 0.001f);
}

TEST(CameraMouse, PitchChangesBothEyesDirectlyAndDiscardsMotionAtHeightLimits) {
    auto camera = Camera();
    camera.GetActiveParam()->height = 0;
    camera.GetActiveParam()->min_height = -15;
    camera.GetActiveParam()->max_height = 40;
    EXPECT_LT(MouseCameraPitch(camera, 0.1f, nullptr, 0), 0);
    EXPECT_LT(camera.pos[1], 40);
    EXPECT_FLOAT_EQ(camera.pos[1], camera.next_pos[1]);
    EXPECT_FLOAT_EQ(camera.ref[1], 40);
    MouseCameraPitch(camera, -10, nullptr, 0);
    EXPECT_FLOAT_EQ(camera.GetActiveParam()->height, 40);
    const float limited = camera.pos[1];
    EXPECT_EQ(MouseCameraPitch(camera, -10, nullptr, 0), 0);
    EXPECT_FLOAT_EQ(camera.pos[1], limited);
    EXPECT_LT(MouseCameraPitch(camera, 0.02f, nullptr, 0), 0);
    EXPECT_LT(camera.pos[1], limited);
}

TEST(CameraMouse, PitchCannotSweepThroughACeiling) {
    auto camera = Camera();
    CCPoly ceiling{};
    const float vertices[3][4] = {{-1000, 60, -1000, 1}, {1000, 60, -1000, 1}, {0, 60, 1000, 1}};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 4; ++j) ceiling.vertex[i][j] = vertices[i][j];
    ceiling.normal[1] = -1;
    MouseCameraPitch(camera, -0.5f, &ceiling, 1);
    EXPECT_LE(camera.pos[1], 40.001f);
    EXPECT_LE(camera.next_pos[1], 40.001f);
}

namespace {
class CapacityMap : public CMap {
public:
    int calls = 0;
    int polygons = 0;
    int GetCameraPoly(CCPoly *out, mgVu0FBOX &, int maximum) override {
        ++calls;
        const int written = std::min(polygons, maximum);
        for (int i = 0; i < written; ++i) out[i] = Wall(1000);
        return written;
    }
};
}

TEST(CameraMouse, AFullMapCollectorCannotEnterTheNextMapWithZeroCapacity) {
    CScene scene;
    CapacityMap first, second;
    first.polygons = 8;
    second.polygons = 1;
    scene.AssignMap(0, &first, const_cast<char *>("first"));
    scene.AssignMap(1, &second, const_cast<char *>("second"));
    scene.map[0].status |= SCENE_DATA_ACTIVE;
    scene.map[1].status |= SCENE_DATA_ACTIVE;
    mgVu0FBOX box{};
    CCPoly output[8]{};
    EXPECT_EQ(MouseCameraCollectPolys(scene, box, output, 8), -1);
    EXPECT_EQ(first.calls, 1);
    EXPECT_EQ(second.calls, 0);
}
