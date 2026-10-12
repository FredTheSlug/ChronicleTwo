#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>

#include "common.h"
#include "gfx/gfx.hpp"
#include "gfx_fixture.hpp"
#include "mglib.hpp"
#include "mglib_port.hpp"
#include "platform/clock.hpp"
#include "platform/window.hpp"

using namespace dc::test;

namespace {

class AspectRenderer {
public:
    AspectRenderer(int width, int height) {
        SetEnv("VK_LAYER_VALIDATE_SYNC", "1", 0);
        WindowConfig window{width, height, true};
        window.vulkan = false;
        WindowInit(window);

        gfx::RendererConfig config;
        config.app_name = "Dark Chronicle aspect tests";
        config.logical_width = 512.0f;
        config.logical_height = 480.0f;
        config.display_aspect = 4.0f / 3.0f;
        config.validation = true;
        config.offscreen = true;
        config.pipeline_cache = std::filesystem::temp_directory_path() / "dc_gfx_test" /
                                ("mglib_aspect_" + std::to_string(ProcessId()) + ".bin");
        gfx::RendererInit(WindowHandle(), config);

        ClockSetUnbounded(true);
        mgInit(MG_SCREEN_MODE_512X480, 3);
    }

    ~AspectRenderer() {
        gfx::RendererShutdown();
        WindowShutdown();
        EXPECT_EQ(gfx::ValidationMessageCount(), 0u);
    }
};

bool Inside(float x) {
    const mgPortExtent e = mgPortVisibleExtent();
    return x >= e.left && x <= e.right;
}

} // namespace

TEST(MglibAspect, LogicalScreenAt640x480) {
    AspectRenderer renderer(640, 480);

    EXPECT_EQ(mgScreenWidth, 512);
    EXPECT_EQ(mgScreenHeight, 480);

    mgBeginFrame(nullptr);
    mgEndFrame(nullptr);

    EXPECT_EQ(mgScreenWidth, 512);
    EXPECT_EQ(mgScreenHeight, 480);
}

TEST(MglibAspect, LogicalScreenAt1280x720) {
    AspectRenderer renderer(1280, 720);

    EXPECT_EQ(mgScreenWidth, 512);
    EXPECT_EQ(mgScreenHeight, 480);

    mgBeginFrame(nullptr);
    mgEndFrame(nullptr);

    EXPECT_EQ(mgScreenWidth, 512);
    EXPECT_EQ(mgScreenHeight, 480);
}

TEST(MglibAspect, LogicalScreenAt800x1200) {
    AspectRenderer renderer(800, 1200);

    EXPECT_EQ(mgScreenWidth, 512);
    EXPECT_EQ(mgScreenHeight, 480);

    mgBeginFrame(nullptr);
    mgEndFrame(nullptr);

    EXPECT_EQ(mgScreenWidth, 512);
    EXPECT_EQ(mgScreenHeight, 480);
}

TEST(MglibAspect, ExtentIsScreenAtFourThree) {
    AspectRenderer renderer(1024, 768);

    const mgPortExtent e = mgPortVisibleExtent();
    EXPECT_NEAR(e.left, 0.0f, 0.01f);
    EXPECT_NEAR(e.right, 512.0f, 0.01f);
    EXPECT_NEAR(e.top, 0.0f, 0.01f);
    EXPECT_NEAR(e.bottom, 480.0f, 0.01f);
    EXPECT_NEAR(e.half_width, 256.0f, 0.01f);
    EXPECT_NEAR(e.half_height, 240.0f, 0.01f);

    EXPECT_EQ(mgScreenOffx, 2048 - 256);
    EXPECT_FLOAT_EQ(e.gs_left, static_cast<float>(mgScreenOffx));
    EXPECT_FLOAT_EQ(e.gs_right, static_cast<float>(mgScreenOffx) + 512.0f);
}

TEST(MglibAspect, ExtentIsWiderAtSixteenNine) {
    AspectRenderer renderer(1280, 720);

    const mgPortExtent e = mgPortVisibleExtent();
    EXPECT_LT(e.left, -40.0f);
    EXPECT_GT(e.right, 552.0f);
    EXPECT_NEAR(e.top, 0.0f, 0.5f);
    EXPECT_NEAR(e.bottom, 480.0f, 0.5f);
    EXPECT_GT(e.half_width, 296.0f);
    EXPECT_NEAR(e.half_height, 240.0f, 0.5f);

    EXPECT_TRUE(-40.0f >= e.left && -40.0f <= e.right);
}

TEST(MglibAspect, ExtentKeepsWidthWhenNarrow) {
    AspectRenderer renderer(800, 1200);

    const mgPortExtent e = mgPortVisibleExtent();
    EXPECT_NEAR(e.left, 0.0f, 0.5f);
    EXPECT_NEAR(e.right, 512.0f, 0.5f);
    EXPECT_LT(e.top, 0.0f);
    EXPECT_GT(e.bottom, 480.0f);
}

TEST(MglibAspect, CullAtMinusFortyWhenWider) {
    AspectRenderer renderer(1280, 720);

    EXPECT_TRUE(Inside(-40.0f));
}

TEST(MglibAspect, CullAtMinusFortyAtFourThree) {
    AspectRenderer renderer(1024, 768);

    EXPECT_FALSE(Inside(-40.0f));
}
