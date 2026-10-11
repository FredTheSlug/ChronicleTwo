#include <gtest/gtest.h>

#include "collision.hpp"
#include "editcoll.hpp"
#include "stubs/collision.hpp"
#include "stubs/editmap2.hpp"

namespace {

void SetPoint(float *dst, float x, float y, float z) {
    dst[0] = x;
    dst[1] = y;
    dst[2] = z;
    dst[3] = 1.0f;
}

void SetIdentity(float (*matrix)[4]) {
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            matrix[row][col] = row == col ? 1.0f : 0.0f;
        }
    }
}

} // namespace

TEST(CollisionAsm, PlaneNormalXZPointsUpForCounterclockwiseFromAbove) {
    float normal[4] = {};
    float p0[4] = {};
    float p1[4] = {};
    float p2[4] = {};
    SetPoint(p0, 0.0f, 0.0f, 0.0f);
    SetPoint(p1, 0.0f, 0.0f, 1.0f);
    SetPoint(p2, 1.0f, 0.0f, 0.0f);

    PlaneNormalXZ(normal, p0, p1, p2);

    EXPECT_FLOAT_EQ(normal[0], 0.0f);
    EXPECT_FLOAT_EQ(normal[1], 1.0f);
    EXPECT_FLOAT_EQ(normal[2], 0.0f);
    EXPECT_FLOAT_EQ(normal[3], 0.0f);
}

TEST(CollisionAsm, PlaneNormalXZIgnoresHeightAndFlipsWithWinding) {
    float normal[4] = {};
    float p0[4] = {};
    float p1[4] = {};
    float p2[4] = {};
    SetPoint(p0, 0.0f, 3.0f, 0.0f);
    SetPoint(p1, 0.0f, -2.0f, 1.0f);
    SetPoint(p2, 1.0f, 9.0f, 0.0f);

    PlaneNormalXZ(normal, p0, p1, p2);

    EXPECT_FLOAT_EQ(normal[0], 0.0f);
    EXPECT_FLOAT_EQ(normal[1], 1.0f);
    EXPECT_FLOAT_EQ(normal[2], 0.0f);

    float swapped[4] = {};
    PlaneNormalXZ(swapped, p0, p2, p1);

    EXPECT_FLOAT_EQ(swapped[1], -1.0f);
}

TEST(CollisionAsm, ClipBoxXZReportsOverlappingBoxes) {
    float max_a[4] = {2.0f, 0.0f, 2.0f, 1.0f};
    float min_a[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float max_b[4] = {4.0f, 0.0f, 4.0f, 1.0f};
    float min_b[4] = {1.0f, 0.0f, 1.0f, 1.0f};

    EXPECT_EQ(ClipBoxXZ(max_a, min_a, max_b, min_b), 1);
}

TEST(CollisionAsm, ClipBoxXZCountsTouchingBoxesAsOverlapping) {
    float max_a[4] = {2.0f, 0.0f, 2.0f, 1.0f};
    float min_a[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float max_b[4] = {4.0f, 0.0f, 4.0f, 1.0f};
    float min_b[4] = {2.0f, 0.0f, 2.0f, 1.0f};

    EXPECT_EQ(ClipBoxXZ(max_a, min_a, max_b, min_b), 1);
}

TEST(CollisionAsm, ClipBoxXZRejectsBoxesSeparatedOnX) {
    float max_a[4] = {2.0f, 0.0f, 2.0f, 1.0f};
    float min_a[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float max_b[4] = {-1.0f, 0.0f, 2.0f, 1.0f};
    float min_b[4] = {-3.0f, 0.0f, 0.0f, 1.0f};

    EXPECT_EQ(ClipBoxXZ(max_a, min_a, max_b, min_b), 0);
}

TEST(CollisionAsm, ClipBoxXZRejectsBoxesSeparatedOnZ) {
    float max_a[4] = {2.0f, 0.0f, 2.0f, 1.0f};
    float min_a[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float max_b[4] = {1.0f, 0.0f, -1.0f, 1.0f};
    float min_b[4] = {0.0f, 0.0f, -3.0f, 1.0f};

    EXPECT_EQ(ClipBoxXZ(max_a, min_a, max_b, min_b), 0);
}

TEST(CollisionAsm, ClipBoxXZIgnoresHeight) {
    float max_a[4] = {2.0f, -100.0f, 2.0f, 1.0f};
    float min_a[4] = {0.0f, -100.0f, 0.0f, 1.0f};
    float max_b[4] = {4.0f, 100.0f, 4.0f, 1.0f};
    float min_b[4] = {1.0f, 100.0f, 1.0f, 1.0f};

    EXPECT_EQ(ClipBoxXZ(max_a, min_a, max_b, min_b), 1);
}

TEST(CollisionAsm, TranceNormalIdentityKeepsCornersAndReturnsCrossProduct) {
    float matrix[4][4];
    SetIdentity(matrix);
    pre_trance_normal(matrix);

    float corners[12] = {};
    SetPoint(corners + 0, 0.0f, 0.0f, 0.0f);
    SetPoint(corners + 4, 0.0f, 0.0f, 1.0f);
    SetPoint(corners + 8, 1.0f, 0.0f, 0.0f);
    float normal[4] = {};

    trance_normal(corners, corners + 4, corners + 8, normal);

    EXPECT_FLOAT_EQ(corners[0], 0.0f);
    EXPECT_FLOAT_EQ(corners[2], 0.0f);
    EXPECT_FLOAT_EQ(corners[4 + 2], 1.0f);
    EXPECT_FLOAT_EQ(corners[8], 1.0f);
    EXPECT_FLOAT_EQ(normal[0], 0.0f);
    EXPECT_FLOAT_EQ(normal[1], 1.0f);
    EXPECT_FLOAT_EQ(normal[2], 0.0f);
    EXPECT_FLOAT_EQ(normal[3], 0.0f);
}

TEST(CollisionAsm, TranceNormalAppliesTranslationRowToCorners) {
    float matrix[4][4];
    SetIdentity(matrix);
    matrix[3][0] = 5.0f;
    matrix[3][1] = 6.0f;
    matrix[3][2] = 7.0f;
    pre_trance_normal(matrix);

    float corners[12] = {};
    SetPoint(corners + 0, 0.0f, 0.0f, 0.0f);
    SetPoint(corners + 4, 0.0f, 0.0f, 1.0f);
    SetPoint(corners + 8, 1.0f, 0.0f, 0.0f);
    float normal[4] = {};

    trance_normal(corners, corners + 4, corners + 8, normal);

    EXPECT_FLOAT_EQ(corners[0], 5.0f);
    EXPECT_FLOAT_EQ(corners[1], 6.0f);
    EXPECT_FLOAT_EQ(corners[2], 7.0f);
    EXPECT_FLOAT_EQ(corners[3], 1.0f);
    EXPECT_FLOAT_EQ(corners[4 + 2], 8.0f);
    EXPECT_FLOAT_EQ(corners[8], 6.0f);
    EXPECT_FLOAT_EQ(normal[1], 1.0f);
}

TEST(CollisionAsm, TranceNormalScalesNormalWithTransformedEdges) {
    float matrix[4][4] = {
        {2.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 3.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 4.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    };
    pre_trance_normal(matrix);

    float corners[12] = {};
    SetPoint(corners + 0, 0.0f, 0.0f, 0.0f);
    SetPoint(corners + 4, 0.0f, 0.0f, 1.0f);
    SetPoint(corners + 8, 1.0f, 0.0f, 0.0f);
    float normal[4] = {};

    trance_normal(corners, corners + 4, corners + 8, normal);

    EXPECT_FLOAT_EQ(corners[4 + 2], 4.0f);
    EXPECT_FLOAT_EQ(corners[8], 2.0f);
    EXPECT_FLOAT_EQ(normal[0], 0.0f);
    EXPECT_FLOAT_EQ(normal[1], 8.0f);
    EXPECT_FLOAT_EQ(normal[2], 0.0f);
    EXPECT_FLOAT_EQ(normal[3], 0.0f);
}

TEST(CollisionAsm, PickUpNearPolyReturnsOnlyPolygonsInsideBox) {
    CCollisionMDT collision;
    collision.bbox.min[0] = -1000.0f;
    collision.bbox.min[1] = -1000.0f;
    collision.bbox.min[2] = -1000.0f;
    collision.bbox.max[0] = 1000.0f;
    collision.bbox.max[1] = 1000.0f;
    collision.bbox.max[2] = 1000.0f;

    CCPoly polys[2] = {};
    SetPoint(polys[0].vertex[0], 0.0f, 0.0f, 0.0f);
    SetPoint(polys[0].vertex[1], 10.0f, 0.0f, 0.0f);
    SetPoint(polys[0].vertex[2], 0.0f, 0.0f, 10.0f);
    SetPoint(polys[0].normal, 0.0f, 1.0f, 0.0f);
    polys[0].ground_kind = 7;
    SetPoint(polys[1].vertex[0], 500.0f, 0.0f, 500.0f);
    SetPoint(polys[1].vertex[1], 510.0f, 0.0f, 500.0f);
    SetPoint(polys[1].vertex[2], 500.0f, 0.0f, 510.0f);
    SetPoint(polys[1].normal, 0.0f, 1.0f, 0.0f);
    collision.poly = polys;
    collision.poly_count = 2;

    mgVu0FBOX box = {};
    box.min[0] = -5.0f;
    box.min[1] = -5.0f;
    box.min[2] = -5.0f;
    box.max[0] = 5.0f;
    box.max[1] = 5.0f;
    box.max[2] = 5.0f;

    CCPoly out[4] = {};
    EXPECT_EQ(collision.PickUpNearPoly(out, box, 4), 1);
    EXPECT_FLOAT_EQ(out[0].vertex[1][0], 10.0f);
    EXPECT_EQ(out[0].ground_kind, 7);
}
