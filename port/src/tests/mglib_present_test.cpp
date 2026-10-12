#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "common.h"
#include "gfx/gfx.hpp"
#include "gfx_fixture.hpp"
#include "libgraph.h"
#include "mglib.hpp"
#include "mglib_port.hpp"
#include "platform/clock.hpp"
#include "platform/window.hpp"

using namespace dc::test;

namespace {

class PresentRenderer {
public:
    PresentRenderer(int width, int height) {
        SetEnv("VK_LAYER_VALIDATE_SYNC", "1", 0);
        WindowConfig window{width, height, true};
        window.vulkan = false;
        WindowInit(window);

        gfx::RendererConfig config;
        config.app_name = "Dark Chronicle present tests";
        config.logical_width = 512.0f;
        config.logical_height = 480.0f;
        config.display_aspect = 4.0f / 3.0f;
        config.validation = true;
        config.offscreen = true;
        config.pipeline_cache = std::filesystem::temp_directory_path() / "dc_gfx_test" /
                                ("mglib_present_" + std::to_string(ProcessId()) + ".bin");
        gfx::RendererInit(WindowHandle(), config);

        ClockSetUnbounded(true);
        mgInit(MG_SCREEN_MODE_512X480, 3);
    }

    ~PresentRenderer() {
        gfx::RendererShutdown();
        WindowShutdown();
        EXPECT_EQ(gfx::ValidationMessageCount(), 0u);
    }
};

gfx::Vertex2D QuadCorner(float x, float y) {
    gfx::Vertex2D vertex = {};
    vertex.x = x;
    vertex.y = y;
    vertex.z = 1.0f;
    vertex.color[0] = 0xFF;
    vertex.color[1] = 0xFF;
    vertex.color[2] = 0xFF;
    vertex.color[3] = 0xFF;
    return vertex;
}

size_t PixelOffset(const gfx::LogicalMapping &mapping, uint32_t width, float x, float y) {
    const size_t px = static_cast<size_t>(x * mapping.scale_x + mapping.offset_x);
    const size_t py = static_cast<size_t>(y * mapping.scale_y + mapping.offset_y);
    return (py * width + px) * 4;
}

} // namespace

TEST(MglibPresent, InterpScopeRestoresKey) {
    PresentRenderer renderer(640, 480);

    int some_int = 0;
    int other = 0;

    gfx::SetInterpKey(7);
    {
        mgPortInterpScope scope(&some_int, kMgPortFrameTeleportDistance);
        EXPECT_EQ(gfx::CurrentInterpKey(), reinterpret_cast<uintptr_t>(&some_int));
        {
            mgPortInterpScope inner(&other, 1.0f, false, true);
            EXPECT_TRUE(gfx::CurrentNoInterpolation());
        }
        EXPECT_FALSE(gfx::CurrentNoInterpolation());
    }
    EXPECT_EQ(gfx::CurrentInterpKey(), 7u);
    gfx::SetInterpKey(0);
}

TEST(MglibPresent, TickIsRecordedAndRenderedCanonically) {
    PresentRenderer renderer(640, 480);

    mgSetBackGround(64.0f, 128.0f, 192.0f, 128.0f);
    mgBeginFrame(nullptr);
    EXPECT_TRUE(mgPortRecording());

    const std::array<gfx::Vertex2D, 4> quad = {
        QuadCorner(100.0f, 100.0f),
        QuadCorner(200.0f, 100.0f),
        QuadCorner(200.0f, 200.0f),
        QuadCorner(100.0f, 200.0f),
    };
    mgPortDraw2D(gfx::Primitive::Quads, quad, gfx::TextureBinding{}, mgPortDrawState());

    mgEndFrame(nullptr);
    EXPECT_FALSE(mgPortRecording());

    ASSERT_NE(mgPortLastList(), nullptr);
    EXPECT_EQ(gfx::ListStats(*mgPortLastList()).draws_2d, 1u);

    std::vector<uint8_t> rgba;
    uint32_t             width = 0;
    uint32_t             height = 0;
    ASSERT_TRUE(mgPortCanonicalReadColor(gfx::kMainTarget, rgba, width, height));

    const gfx::LogicalMapping mapping = gfx::GetLogicalMapping(gfx::kMainTarget);

    const size_t inside = PixelOffset(mapping, width, 150.0f, 150.0f);
    EXPECT_NEAR(rgba[inside + 0], 255, 1);
    EXPECT_NEAR(rgba[inside + 1], 255, 1);
    EXPECT_NEAR(rgba[inside + 2], 255, 1);

    const size_t outside = PixelOffset(mapping, width, 50.0f, 50.0f);
    EXPECT_NEAR(rgba[outside + 0], 64, 1);
    EXPECT_NEAR(rgba[outside + 1], 128, 1);
    EXPECT_NEAR(rgba[outside + 2], 192, 1);

    mgBeginFrame(nullptr);
    mgEndFrame(nullptr);
    EXPECT_NE(mgPortPreviousList(), nullptr);
}

TEST(MglibPresent, FrameTargetFlag) {
    PresentRenderer renderer(1280, 720);

    const gfx::TextureHandle framed = mgPortRenderTarget(0x3000u, 512, 480, SCE_GS_PSMCT32, true);
    const std::optional<gfx::TextureInfo> framed_info = gfx::GetTextureInfo(framed);
    ASSERT_TRUE(framed_info.has_value());
    EXPECT_TRUE(framed_info->frame_target);

    const gfx::TextureHandle plain = mgPortRenderTarget(0x3000u, 512, 480, SCE_GS_PSMCT32, false);
    const std::optional<gfx::TextureInfo> plain_info = gfx::GetTextureInfo(plain);
    ASSERT_TRUE(plain_info.has_value());
    EXPECT_FALSE(plain_info->frame_target);

    EXPECT_EQ(mgPortRenderTarget(0x3000u, 512, 480, SCE_GS_PSMCT32, false), plain);
}

TEST(MglibPresent, CutBeforeTickIsAccepted) {
    PresentRenderer renderer(640, 480);

    mgPortCutInterpolation();
    mgBeginFrame(nullptr);
    mgEndFrame(nullptr);

    EXPECT_FALSE(mgPortRecording());
}
