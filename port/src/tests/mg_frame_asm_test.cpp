#include <gtest/gtest.h>

#include <cstring>

#include "mg_frame.hpp"
#include "mg_math.hpp"
#include "mglib.hpp"
#include "stubs/mg_frame.hpp"

namespace {

void SetIdentity(float (*m)[4]) {
    memset(m, 0, sizeof(float[4][4]));
    m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
}

void SetCube(float (*corners)[4]) {
    const float cube[8][4] = {
        {-1.0f, -1.0f, -1.0f, 1.0f}, {1.0f, -1.0f, -1.0f, 1.0f}, {-1.0f, 1.0f, -1.0f, 1.0f},
        {1.0f, 1.0f, -1.0f, 1.0f},   {-1.0f, -1.0f, 1.0f, 1.0f}, {1.0f, -1.0f, 1.0f, 1.0f},
        {-1.0f, 1.0f, 1.0f, 1.0f},   {1.0f, 1.0f, 1.0f, 1.0f},
    };
    memcpy(corners, cube, sizeof(cube));
}

// Corners at w = 1 in the near half (z = -1) and at x, y = +-2 in the far half (z = 1).
void SetMixedDepthCorners(float (*corners)[4]) {
    const float points[8][4] = {
        {-1.0f, -1.0f, -1.0f, 1.0f}, {1.0f, -1.0f, -1.0f, 1.0f}, {-1.0f, 1.0f, -1.0f, 1.0f},
        {1.0f, 1.0f, -1.0f, 1.0f},   {-2.0f, -2.0f, 1.0f, 1.0f}, {2.0f, -2.0f, 1.0f, 1.0f},
        {-2.0f, 2.0f, 1.0f, 1.0f},   {2.0f, 2.0f, 1.0f, 1.0f},
    };
    memcpy(corners, points, sizeof(points));
}

// The identity with row 2 column 3 at 1 and row 3 column 3 at 2: a corner comes out as (x, y, z, z + 2).
void SetDepthMatrix(float (*m)[4]) {
    SetIdentity(m);
    m[2][3] = 1.0f;
    m[3][3] = 2.0f;
}

} // namespace

TEST(MgFrameAsm, Test1BoxesCornersAfterMatrixThenScreen) {
    float corners[8][4];
    float screen[4][4];
    float matrix[4][4];
    float box_max[4];
    float box_min[4];

    SetCube(corners);
    SetIdentity(screen);
    SetIdentity(matrix);
    screen[0][0] = 2.0f;
    screen[1][1] = 3.0f;
    screen[2][2] = 4.0f;
    matrix[3][0] = 5.0f;
    matrix[3][1] = -1.0f;
    matrix[3][2] = 2.0f;

    test1(corners, screen, matrix, box_max, box_min);

    // Corner (x, y, z, 1) goes to (2 (x + 5), 3 (y - 1), 4 (z + 2), 1).
    EXPECT_FLOAT_EQ(box_max[0], 12.0f);
    EXPECT_FLOAT_EQ(box_max[1], 0.0f);
    EXPECT_FLOAT_EQ(box_max[2], 12.0f);
    EXPECT_FLOAT_EQ(box_max[3], 1.0f);
    EXPECT_FLOAT_EQ(box_min[0], 8.0f);
    EXPECT_FLOAT_EQ(box_min[1], -6.0f);
    EXPECT_FLOAT_EQ(box_min[2], 4.0f);
    EXPECT_FLOAT_EQ(box_min[3], 1.0f);
}

TEST(MgFrameAsm, Test1KeepsDepthInW) {
    float corners[8][4];
    float screen[4][4];
    float matrix[4][4];
    float box_max[4];
    float box_min[4];

    SetMixedDepthCorners(corners);
    SetIdentity(screen);
    SetDepthMatrix(matrix);

    test1(corners, screen, matrix, box_max, box_min);

    EXPECT_FLOAT_EQ(box_max[0], 2.0f);
    EXPECT_FLOAT_EQ(box_max[1], 2.0f);
    EXPECT_FLOAT_EQ(box_max[2], 1.0f);
    EXPECT_FLOAT_EQ(box_max[3], 3.0f);
    EXPECT_FLOAT_EQ(box_min[0], -2.0f);
    EXPECT_FLOAT_EQ(box_min[1], -2.0f);
    EXPECT_FLOAT_EQ(box_min[2], -1.0f);
    EXPECT_FLOAT_EQ(box_min[3], 1.0f);
}

TEST(MgFrameAsm, Test2DividesXYByAbsoluteW) {
    float corners[8][4];
    float screen[4][4];
    float matrix[4][4];
    float box_max[4];
    float box_min[4];

    SetMixedDepthCorners(corners);
    SetIdentity(screen);
    SetDepthMatrix(matrix);
    test1(corners, screen, matrix, box_max, box_min);

    // The far corners have w = 3, so their x and y shrink to +-2/3; the near corners are untouched.
    test2(box_max, box_min);

    EXPECT_FLOAT_EQ(box_max[0], 1.0f);
    EXPECT_FLOAT_EQ(box_max[1], 1.0f);
    EXPECT_FLOAT_EQ(box_max[2], 1.0f);
    EXPECT_FLOAT_EQ(box_max[3], 3.0f);
    EXPECT_FLOAT_EQ(box_min[0], -1.0f);
    EXPECT_FLOAT_EQ(box_min[1], -1.0f);
    EXPECT_FLOAT_EQ(box_min[2], -1.0f);
    EXPECT_FLOAT_EQ(box_min[3], 1.0f);
}

TEST(MgFrameAsm, InsideScreenReturnsOverlapWithScreenBox) {
    float corners[8][4];
    float matrix[4][4];
    float box_max[4];
    float box_min[4];

    SetCube(corners);
    SetIdentity(matrix);
    SetIdentity(mgRenderInfo.world_screen_rel);
    const float screen_max[4] = {100.0f, 100.0f, 0.0f, 1000.0f};
    const float screen_min[4] = {-100.0f, -100.0f, 0.0f, 0.0f};
    memcpy(mgRenderInfo.screen_box_max, screen_max, sizeof(screen_max));
    memcpy(mgRenderInfo.screen_box_min, screen_min, sizeof(screen_min));

    EXPECT_EQ(mgInsideScreen(corners, matrix, box_max, box_min), 1);
    EXPECT_FLOAT_EQ(box_max[0], 1.0f);
    EXPECT_FLOAT_EQ(box_max[1], 1.0f);
    EXPECT_FLOAT_EQ(box_max[2], 1.0f);
    EXPECT_FLOAT_EQ(box_max[3], 1.0f);
    EXPECT_FLOAT_EQ(box_min[0], -1.0f);
    EXPECT_FLOAT_EQ(box_min[1], -1.0f);
    EXPECT_FLOAT_EQ(box_min[2], -1.0f);
    EXPECT_FLOAT_EQ(box_min[3], 1.0f);
}

TEST(MgFrameAsm, InsideScreenRejectsBoxBeyondScreenEdge) {
    float corners[8][4];
    float matrix[4][4];
    float box_max[4];
    float box_min[4];

    SetCube(corners);
    SetIdentity(matrix);
    matrix[3][0] = 200.0f;
    SetIdentity(mgRenderInfo.world_screen_rel);
    const float screen_max[4] = {100.0f, 100.0f, 0.0f, 1000.0f};
    const float screen_min[4] = {-100.0f, -100.0f, 0.0f, 0.0f};
    memcpy(mgRenderInfo.screen_box_max, screen_max, sizeof(screen_max));
    memcpy(mgRenderInfo.screen_box_min, screen_min, sizeof(screen_min));

    EXPECT_EQ(mgInsideScreen(corners, matrix, box_max, box_min), 0);
}

TEST(MgFrameAsm, GetLocalMatrixScalesColumnsAndAddsPosition) {
    mgCFrame frame;
    frame.use_srt = 1;
    frame.rot_type = 0;
    SetIdentity(frame.trans_matrix);
    frame.trans_matrix[3][0] = 5.0f;
    frame.trans_matrix[3][1] = -1.0f;
    frame.trans_matrix[3][2] = 2.0f;
    frame.scale[0] = 2.0f;
    frame.scale[1] = 3.0f;
    frame.scale[2] = 4.0f;
    frame.scale[3] = 0.0f;
    frame.position[0] = 10.0f;
    frame.position[1] = 20.0f;
    frame.position[2] = 30.0f;
    frame.position[3] = 1.0f;

    float matrix[4][4];
    frame.GetLocalMatrix(matrix);

    EXPECT_FLOAT_EQ(matrix[0][0], 2.0f);
    EXPECT_FLOAT_EQ(matrix[0][1], 0.0f);
    EXPECT_FLOAT_EQ(matrix[0][2], 0.0f);
    EXPECT_FLOAT_EQ(matrix[0][3], 0.0f);
    EXPECT_FLOAT_EQ(matrix[1][0], 0.0f);
    EXPECT_FLOAT_EQ(matrix[1][1], 3.0f);
    EXPECT_FLOAT_EQ(matrix[1][2], 0.0f);
    EXPECT_FLOAT_EQ(matrix[1][3], 0.0f);
    EXPECT_FLOAT_EQ(matrix[2][0], 0.0f);
    EXPECT_FLOAT_EQ(matrix[2][1], 0.0f);
    EXPECT_FLOAT_EQ(matrix[2][2], 4.0f);
    EXPECT_FLOAT_EQ(matrix[2][3], 0.0f);
    EXPECT_FLOAT_EQ(matrix[3][0], 15.0f);
    EXPECT_FLOAT_EQ(matrix[3][1], 19.0f);
    EXPECT_FLOAT_EQ(matrix[3][2], 32.0f);
    EXPECT_FLOAT_EQ(matrix[3][3], 1.0f);
}

TEST(MgFrameAsm, GetLocalMatrixLocalOriginGivesSameTranslation) {
    mgCFrame frame;
    frame.use_srt = 1;
    frame.rot_type = MG_FRAME_ROT_LOCAL_ORIGIN;
    SetIdentity(frame.trans_matrix);
    frame.trans_matrix[3][0] = 5.0f;
    frame.trans_matrix[3][1] = -1.0f;
    frame.trans_matrix[3][2] = 2.0f;
    frame.scale[0] = 2.0f;
    frame.scale[1] = 3.0f;
    frame.scale[2] = 4.0f;
    frame.scale[3] = 0.0f;
    frame.position[0] = 10.0f;
    frame.position[1] = 20.0f;
    frame.position[2] = 30.0f;
    frame.position[3] = 1.0f;

    float matrix[4][4];
    frame.GetLocalMatrix(matrix);

    EXPECT_FLOAT_EQ(matrix[3][0], 15.0f);
    EXPECT_FLOAT_EQ(matrix[3][1], 19.0f);
    EXPECT_FLOAT_EQ(matrix[3][2], 32.0f);
    EXPECT_FLOAT_EQ(matrix[3][3], 1.0f);
}

TEST(MgFrameAsm, GetLocalMatrixCopiesTransMatrixWhenSrtClear) {
    mgCFrame frame;
    frame.use_srt = 0;
    SetIdentity(frame.trans_matrix);
    frame.trans_matrix[3][0] = 7.0f;
    frame.trans_matrix[3][1] = 8.0f;
    frame.trans_matrix[3][2] = 9.0f;

    float matrix[4][4];
    frame.GetLocalMatrix(matrix);

    EXPECT_EQ(memcmp(matrix, frame.trans_matrix, sizeof(matrix)), 0);
}
