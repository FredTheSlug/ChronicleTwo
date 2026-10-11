#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>

#include "mg_drawprim.hpp"

namespace {

void ReadLanes(const u_long128 *quad, int *lanes) {
    memcpy(lanes, quad, sizeof(int) * 4);
}

} // namespace

TEST(MgDrawPrimAsm, Data0TruncatesEachLaneTowardZero) {
    mgCDrawPrim prim;
    alignas(16) u_long128 buffer[2] = {};
    prim.write = buffer;

    float data[4] = {1.5f, -2.75f, 100.9f, 0.0f};
    prim.Data0(data);

    int lanes[4];
    ReadLanes(buffer, lanes);
    EXPECT_EQ(lanes[0], 1);
    EXPECT_EQ(lanes[1], -2);
    EXPECT_EQ(lanes[2], 100);
    EXPECT_EQ(lanes[3], 0);
}

TEST(MgDrawPrimAsm, Data0AdvancesWriteByOneQuadword) {
    mgCDrawPrim prim;
    alignas(16) u_long128 buffer[2] = {};
    prim.write = buffer;

    float data[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    prim.Data0(data);

    EXPECT_EQ(prim.write, buffer + 1);
}

TEST(MgDrawPrimAsm, Data0OutOfRangeGivesIntMin) {
    mgCDrawPrim prim;
    alignas(16) u_long128 buffer[1] = {};
    prim.write = buffer;

    float data[4] = {3.0e10f, -3.0e10f, std::numeric_limits<float>::quiet_NaN(), 2147483648.0f};
    prim.Data0(data);

    int lanes[4];
    ReadLanes(buffer, lanes);
    EXPECT_EQ(lanes[0], std::numeric_limits<int>::min());
    EXPECT_EQ(lanes[1], std::numeric_limits<int>::min());
    EXPECT_EQ(lanes[2], std::numeric_limits<int>::min());
    EXPECT_EQ(lanes[3], std::numeric_limits<int>::min());
}

TEST(MgDrawPrimAsm, Data4ScalesBy16BeforeTruncating) {
    mgCDrawPrim prim;
    alignas(16) u_long128 buffer[1] = {};
    prim.write = buffer;

    float data[4] = {1.5f, -2.75f, 0.0625f, 3.9f};
    prim.Data4(data);

    int lanes[4];
    ReadLanes(buffer, lanes);
    EXPECT_EQ(lanes[0], 24);
    EXPECT_EQ(lanes[1], -44);
    EXPECT_EQ(lanes[2], 1);
    EXPECT_EQ(lanes[3], 62);
}

TEST(MgDrawPrimAsm, VertexPacksXYAndZIntoOneWord) {
    mgCDrawPrim prim;
    prim.coord = 1;
    prim.offset_x = 0;
    prim.offset_y = 0;
    alignas(8) u_long buffer[2] = {};
    prim.command_write = buffer;

    float pos[4] = {10.25f, 3.5f, 7.75f, 1.0f};
    prim.Vertex(pos);

    EXPECT_EQ(buffer[0], 0x00000007003800A4ULL);
    EXPECT_EQ(buffer[1], 5ULL);
    EXPECT_EQ(prim.command_write, buffer + 2);
}

TEST(MgDrawPrimAsm, VertexAddsScreenOffsetInSixteenths) {
    mgCDrawPrim prim;
    prim.coord = 1;
    prim.offset_x = 8;
    prim.offset_y = 16;
    alignas(8) u_long buffer[2] = {};
    prim.command_write = buffer;

    float pos[4] = {10.25f, 3.5f, 7.75f, 1.0f};
    prim.Vertex(pos);

    EXPECT_EQ(buffer[0], 0x00000007004800ACULL);
    EXPECT_EQ(buffer[1], 5ULL);
}

TEST(MgDrawPrimAsm, ColorPacksBytesWithQBits) {
    mgCDrawPrim prim;
    alignas(8) u_long buffer[2] = {};
    prim.command_write = buffer;

    float color[4] = {128.0f, 64.5f, 255.0f, 0.0f};
    prim.Color(color);

    EXPECT_EQ(buffer[0], 0x3F80000000FF4080ULL);
    EXPECT_EQ(buffer[1], 1ULL);
    EXPECT_EQ(prim.command_write, buffer + 2);
}
