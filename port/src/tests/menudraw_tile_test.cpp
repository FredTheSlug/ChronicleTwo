#include <gtest/gtest.h>

#include "common.h"
#include "mglib.hpp"

// Defined in port/src/menudraw.cpp. The grid helpers are extracted so they can be checked without a GS capture seam.
float MenuTilePatternFirst(float v, int size);
int   MenuTilePatternCount(float first, int size, float limit);

namespace {

constexpr float kLeftFlank = -170.67f;
constexpr float kRightFlank = 683.0f;
constexpr float kBottomFlank = 736.0f;

class MenuTileTest : public ::testing::Test {
protected:
    void SetUp() override {
        saved_width_ = mgScreenWidth;
        saved_height_ = mgScreenHeight;
        mgScreenWidth = 512;
        mgScreenHeight = 480;
    }

    void TearDown() override {
        mgScreenWidth = saved_width_;
        mgScreenHeight = saved_height_;
    }

private:
    int saved_width_ = 0;
    int saved_height_ = 0;
};

} // namespace

TEST_F(MenuTileTest, ColumnsCoverBothWideFlanksForEveryCallerTileWidth) {
    // Tile widths used by DrawMenuTilePattern callers are 128 and 256; 64 checks the generic rule.
    for (int tile : {256, 128, 64}) {
        for (float scroll : {-1.0f, -128.0f, -255.0f, -tile + 0.5f}) {
            const float first = MenuTilePatternFirst(scroll, tile);
            const int   count = MenuTilePatternCount(first, tile, static_cast<float>(mgScreenWidth) + 256.0f);
            ASSERT_GT(count, 0) << "tile " << tile << " scroll " << scroll;

            const float last_left = first + static_cast<float>((count - 1) * tile);
            EXPECT_LE(first, kLeftFlank) << "tile " << tile << " scroll " << scroll;
            EXPECT_GE(last_left + tile, kRightFlank) << "tile " << tile << " scroll " << scroll;
        }
    }
}

TEST_F(MenuTileTest, FirstColumnKeepsPhaseAndLiesAtOrLeftOfMinus256) {
    for (int tile : {256, 128, 64}) {
        for (float scroll : {-1.0f, -127.5f, -255.0f}) {
            const float first = MenuTilePatternFirst(scroll, tile);
            EXPECT_LE(first, -256.0f) << "tile " << tile << " scroll " << scroll;
            EXPECT_GT(first, -256.0f - tile) << "tile " << tile << " scroll " << scroll;

            const float steps = (scroll - first) / static_cast<float>(tile);
            EXPECT_FLOAT_EQ(steps, static_cast<float>(static_cast<int>(steps))) << "tile " << tile;
        }
    }
}

TEST_F(MenuTileTest, FirstRowStartsAboveZeroWhenScrollIsPositive) {
    // A row origin at or below -256 has to move up by whole tiles, so rows reach the top flank.
    const float first = MenuTilePatternFirst(40.0f, 128);
    EXPECT_LE(first, -256.0f);
    EXPECT_GT(first, -384.0f);
}

TEST_F(MenuTileTest, ColumnCountIsContiguousAndBoundedByLimit) {
    const float first = MenuTilePatternFirst(-128.0f, 256);
    const int   count = MenuTilePatternCount(first, 256, 768.0f);

    ASSERT_EQ(count, 5);
    EXPECT_FLOAT_EQ(first, -384.0f);
    EXPECT_LE(first + static_cast<float>((count - 1) * 256), 768.0f);
    EXPECT_GT(first + static_cast<float>(count * 256), 768.0f);
}

TEST_F(MenuTileTest, RowsCoverBottomFlankForEveryCallerTileHeight) {
    for (int tile : {256, 128, 64}) {
        for (float scroll : {-1.0f, -128.0f, -255.0f}) {
            const float first = MenuTilePatternFirst(scroll, tile);
            const int   count = MenuTilePatternCount(first, tile, static_cast<float>(mgScreenHeight) + 256.0f);
            ASSERT_GT(count, 0) << "tile " << tile << " scroll " << scroll;

            const float last_top = first + static_cast<float>((count - 1) * tile);
            EXPECT_LE(first, -256.0f) << "tile " << tile;
            EXPECT_GE(last_top + tile, kBottomFlank) << "tile " << tile << " scroll " << scroll;
        }
    }
}

TEST_F(MenuTileTest, ZeroSizeProducesNoTiles) {
    EXPECT_EQ(MenuTilePatternCount(-256.0f, 0, 768.0f), 0);
    EXPECT_FLOAT_EQ(MenuTilePatternFirst(-100.0f, 0), -100.0f);
}
