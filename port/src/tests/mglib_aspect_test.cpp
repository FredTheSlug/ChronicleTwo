#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "common.h"
#include "gfx/gfx.hpp"
#include "gfx_fixture.hpp"
#include "mglib.hpp"
#include "menudraw.hpp"
#include "mg_drawprim.hpp"
#include "mg_memory.hpp"
#include "mg_texture.hpp"
#include "mainloop_port.hpp"
#include "menumain.hpp"
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

// Pixel-snapped edges can land one device pixel off, and a device pixel is 1 / scale logical units.
void ExpectQuadAtLogicalPlace(int width, int height) {
    mgSetBackGround(0.0f, 0.0f, 0.0f, 128.0f);
    mgBeginFrame(nullptr);

    const std::array<gfx::Vertex2D, 4> quad = {
        QuadCorner(100.0f, 200.0f),
        QuadCorner(164.0f, 200.0f),
        QuadCorner(164.0f, 264.0f),
        QuadCorner(100.0f, 264.0f),
    };
    mgPortDraw2D(gfx::Primitive::Quads, quad, gfx::TextureBinding{}, mgPortDrawState());

    mgEndFrame(nullptr);

    std::vector<uint8_t> rgba;
    uint32_t             out_width = 0;
    uint32_t             out_height = 0;
    ASSERT_TRUE(mgPortCanonicalReadColor(gfx::kMainTarget, rgba, out_width, out_height));
    EXPECT_EQ(static_cast<int>(out_width), width);
    EXPECT_EQ(static_cast<int>(out_height), height);

    uint32_t min_x = out_width;
    uint32_t max_x = 0;
    uint32_t min_y = out_height;
    uint32_t max_y = 0;
    for (uint32_t y = 0; y < out_height; ++y) {
        for (uint32_t x = 0; x < out_width; ++x) {
            const uint8_t *p = &rgba[(static_cast<size_t>(y) * out_width + x) * 4];
            if (p[0] > 200 && p[1] > 200 && p[2] > 200) {
                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
            }
        }
    }
    ASSERT_LE(min_x, max_x);
    ASSERT_LE(min_y, max_y);

    const gfx::LogicalMapping mapping = gfx::GetLogicalMapping(gfx::kMainTarget);
    const double tol_x = std::max<double>(1.0, 1.0 / mapping.scale_x);
    const double tol_y = std::max<double>(1.0, 1.0 / mapping.scale_y);

    const double left = (static_cast<double>(min_x) - mapping.offset_x) / mapping.scale_x;
    const double right = (static_cast<double>(max_x + 1) - mapping.offset_x) / mapping.scale_x;
    const double top = (static_cast<double>(min_y) - mapping.offset_y) / mapping.scale_y;
    const double bottom = (static_cast<double>(max_y + 1) - mapping.offset_y) / mapping.scale_y;

    EXPECT_NEAR(left, 100.0, tol_x);
    EXPECT_NEAR(right, 164.0, tol_x);
    EXPECT_NEAR(top, 200.0, tol_y);
    EXPECT_NEAR(bottom, 264.0, tol_y);
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

TEST(MglibAspect, QuadAtSameLogicalPlaceAt1280x720) {
    AspectRenderer renderer(1280, 720);
    ExpectQuadAtLogicalPlace(1280, 720);
}

TEST(MglibAspect, QuadAtSameLogicalPlaceAt3840x2160) {
    AspectRenderer renderer(3840, 2160);
    ExpectQuadAtLogicalPlace(3840, 2160);
}

TEST(MglibAspect, QuadAtSameLogicalPlaceAt800x1200) {
    AspectRenderer renderer(800, 1200);
    ExpectQuadAtLogicalPlace(800, 1200);
}

TEST(MglibAspect, FrameBorderStaysAtVisibleEdges) {
    AspectRenderer renderer(1920, 1080);
    mgSetBackGround(200, 150, 100, 128);
    mgBeginFrame(nullptr);
    PortDrawFrameBorder();
    mgEndFrame(nullptr);
    std::vector<uint8_t> rgba;
    uint32_t width = 0, height = 0;
    ASSERT_TRUE(mgPortCanonicalReadColor(gfx::kMainTarget, rgba, width, height));
    ASSERT_EQ(width, 1920u);
    ASSERT_EQ(height, 1080u);
    // Both old 4:3 boundaries are interior pixels of the expanded scene.
    const auto mapping = gfx::GetLogicalMapping(gfx::kMainTarget);
    for (float edge : {0.0f, 511.0f}) {
        const int x = static_cast<int>(mapping.offset_x + edge * mapping.scale_x);
        for (int dx = -2; dx <= 2; ++dx) {
            const auto index = (static_cast<size_t>(height / 2) * width + x + dx) * 4;
            EXPECT_GT(rgba[index], 190) << "black seam at " << x + dx;
        }
    }
}

// The menu backdrop is 704 pixels wide and slides left by 192 pixels for the
// item page. Its lens at texture x=350 must land at logical x=158, alongside
// the model, even though the backdrop covers the entire logical frame.
static void ExpectMenuLensAtRetailPosition(int window_width, int window_height) {
    AspectRenderer renderer(window_width, window_height);
    std::vector<u_long128> arena(4096), pixels(1024 * 512 * 4 / 16), ornament(64 * 64 * 4 / 16);
    mgCMemory memory;
    memory.stSetBuffer(arena.data(), static_cast<int>(arena.size()));
    mgTexManager.SetTableBuffer(16, 4, &memory);
    mgTexManager.Initialize(0x1000, -1);
    auto *rgba = reinterpret_cast<uint8_t *>(pixels.data());
    for (int y = 0; y < 416; ++y) {
        for (int x = 0; x < 704; ++x) {
            rgba[(y * 1024 + x) * 4 + 3] = 128;
            if (x >= 702) rgba[(y * 1024 + x) * 4 + 1] = 255;
        }
    }
    for (int y = 204; y < 212; ++y) {
        for (int x = 346; x < 354; ++x) {
            rgba[(y * 1024 + x) * 4] = 255;
            rgba[(y * 1024 + x) * 4 + 3] = 128;
        }
    }
    u_long128 *levels[MG_TEXTURE_LEVEL_MAX] = {pixels.data()};
    char background_name[] = "bg16", ornament_name[] = "mnmain";
    ASSERT_NE(mgTexManager.EnterTexture(0, background_name, levels, 1024, 512, 32, nullptr, 0, 0), nullptr);
    levels[0] = ornament.data();
    ASSERT_NE(mgTexManager.EnterTexture(0, ornament_name, levels, 64, 64, 32, nullptr, 0, 0), nullptr);
    MenuMainFrameModeSet(MENU_MAIN_FRAME_OPEN, 1);
    for (int i = 0; i < 30; ++i) MenuMainFrameStep();
    MenuMainFrameModeSet(MENU_MAIN_FRAME_SLIDE_LEFT, 1);
    for (int i = 0; i < 30; ++i) MenuMainFrameStep();
    ASSERT_EQ(MenuMainFrame_PutRect.left, -192);

    mgSetBackGround(0, 0, 0, 128);
    mgBeginFrame(nullptr);
    int loaded_texture = -1;
    MenuMainFrameDraw(loaded_texture, 128);
    mgEndFrame(nullptr);
    std::vector<uint8_t> image;
    uint32_t width = 0, height = 0;
    ASSERT_TRUE(mgPortCanonicalReadColor(gfx::kMainTarget, image, width, height));
    uint32_t min_x = width, max_x = 0, min_y = height, max_y = 0;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            if (image[(static_cast<size_t>(y) * width + x) * 4] > 200) {
                min_x = std::min(min_x, x); max_x = std::max(max_x, x);
                min_y = std::min(min_y, y); max_y = std::max(max_y, y);
            }
        }
    }
    ASSERT_LE(min_x, max_x);
    const auto mapping = gfx::GetLogicalMapping(gfx::kMainTarget);
    EXPECT_NEAR(((min_x + max_x + 1) * 0.5f - mapping.offset_x) / mapping.scale_x, 158.0f, 1.0f);
    EXPECT_NEAR(((min_y + max_y + 1) * 0.5f - mapping.offset_y) / mapping.scale_y, 240.0f, 1.0f);
    // A wide window shows past the authored right edge. The edge texel fills
    // that flank, while the lens and all interior artwork stay in place.
    EXPECT_GT(image[(static_cast<size_t>(height / 2) * width + width - 2) * 4 + 1], 200);
    mgTexManager.Initialize(0x1000, -1);
}

TEST(MglibAspect, ItemMenuLensStaysWithModelAtSixteenNine) {
    ExpectMenuLensAtRetailPosition(1920, 1080);
}

TEST(MglibAspect, ItemMenuLensStaysWithModelAtFourThree) {
    ExpectMenuLensAtRetailPosition(640, 480);
}

TEST(MglibAspect, MenuFormsClipParkedStatsAndKeepActivePanel) {
    AspectRenderer renderer(1920, 1080);
    CMenuPosDataManage layout{};
    auto *saved_layout = MenuPosData;
    MenuPosData = &layout;
    MENU_BASETEXINFO texture_info{};
    layout.tex_info = &texture_info;
    layout.tex_info_num = 1;
    MENU_PARTS_EFFECT_STRUCT1 fill{};
    fill.param[0] = 20; fill.param[1] = 220; fill.param[2] = 30; fill.param[3] = 128;
    MENUFORMPARTS_TYPE part{};
    part.active = part.draw_flag = 1;
    part.dtype = MENUFORMPARTS_DTYPE_SQ_BETA;
    // Retail SPECHECK comparison panel is 236x164, parked at -260 and shown at 7.
    part.w = 236; part.h = 164;
    part.effect = &fill;
    CMenuPosDataForm form{};
    form.active = form.draw_flag = 1;
    form.rgba[3] = 128;
    form.dtype = MENUFORM_DTYPE_NORMAL;
    form.y = 296;
    form.parts = &part;
    form.parts_num = 1;
    const auto mapping = gfx::GetLogicalMapping(gfx::kMainTarget);
    for (float x : {-260.0f, -80.0f, 7.0f}) {
        form.x = x;
        mgSetBackGround(200, 150, 100, 128);
        mgBeginFrame(nullptr);
        int state = -1;
        form.MenuFormDraw(state);
        // A following independent draw must still reach the wide flank.
        auto a = QuadCorner(-75, 50), b = QuadCorner(-50, 50);
        auto c = QuadCorner(-50, 75), d = QuadCorner(-75, 75);
        const std::array outside{a, b, c, d};
        gfx::DrawState outside_state{};
        outside_state.depth_test = gfx::DepthTest::Always;
        outside_state.depth_write = false;
        mgPortDraw2D(gfx::Primitive::Quads, outside, {}, outside_state);
        mgEndFrame(nullptr);
        std::vector<uint8_t> rgba;
        uint32_t width = 0, height = 0;
        ASSERT_TRUE(mgPortCanonicalReadColor(gfx::kMainTarget, rgba, width, height));
        const auto pixel = [&](float px, float py, int channel) {
            const int sx = static_cast<int>(mapping.offset_x + px * mapping.scale_x);
            const int sy = static_cast<int>(mapping.offset_y + py * mapping.scale_y);
            return rgba[(static_cast<size_t>(sy) * width + sx) * 4 + channel];
        };
        EXPECT_GT(pixel(-50, 350, 0), 190) << "parked panel leaks at x=" << x;
        if (x >= -80) EXPECT_GT(pixel(40, 350, 1), 200) << "active/entering panel lost";
        if (x == 7) {
            EXPECT_GT(pixel(8, 350, 1), 200);
            EXPECT_GT(pixel(242, 350, 1), 200);
        }
        EXPECT_GT(pixel(-60, 60, 0), 240) << "form clip leaked to following draw";
    }
    // The same form mechanism also carries full-screen fills: those still cover Fill's sides.
    form.x = form.y = 0;
    part.w = 512; part.h = 480;
    mgSetBackGround(200, 150, 100, 128);
    mgBeginFrame(nullptr);
    int state = -1;
    form.MenuFormDraw(state);
    mgEndFrame(nullptr);
    std::vector<uint8_t> rgba;
    uint32_t width = 0, height = 0;
    ASSERT_TRUE(mgPortCanonicalReadColor(gfx::kMainTarget, rgba, width, height));
    EXPECT_GT(rgba[(static_cast<size_t>(height / 2) * width + 100) * 4 + 1], 200);
    MenuPosData = saved_layout;
}
