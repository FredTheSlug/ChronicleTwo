#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
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
#include "present.hpp"

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

using Matrix = std::array<float, 16>;

// Column-major, as MeshTransform and MeshConstants hold them.
Matrix Identity() {
    Matrix m{};
    m[0] = m[5] = m[10] = m[15] = 1.0f;
    return m;
}

Matrix Translation(float x) {
    Matrix m = Identity();
    m[12] = x;
    return m;
}

Matrix Multiply(const Matrix &a, const Matrix &b) {
    Matrix out{};
    for (size_t col = 0; col < 4; ++col) {
        for (size_t row = 0; row < 4; ++row) {
            for (size_t k = 0; k < 4; ++k) {
                out[col * 4 + row] += a[k * 4 + row] * b[col * 4 + k];
            }
        }
    }
    return out;
}

// One world unit is four pixels on the 640x480 target, and depth is a constant 0.5.
constexpr float kPixelsPerUnit = 4.0f;

Matrix Projection() {
    Matrix m{};
    m[0] = 2.0f * kPixelsPerUnit / 640.0f;
    m[5] = 2.0f * kPixelsPerUnit / 480.0f;
    m[14] = 0.5f;
    m[15] = 1.0f;
    return m;
}

void Store(const Matrix &m, float (&out)[16]) {
    std::memcpy(out, m.data(), sizeof out);
}

gfx::Vertex3D RedVertex(float x, float y) {
    gfx::Vertex3D vertex = {};
    vertex.position[0] = x;
    vertex.position[1] = y;
    vertex.normal[2] = 1.0f;
    vertex.color[0] = 0xFF;
    vertex.color[3] = 0xFF;
    return vertex;
}

// A 10 by 10 unit red square, centred on its model origin, moved to x by the model translation.
void DrawRedQuad(float x) {
    const std::vector<gfx::Vertex3D> vertices = {
        RedVertex(-5.0f, -5.0f),
        RedVertex(5.0f, -5.0f),
        RedVertex(5.0f, 5.0f),
        RedVertex(-5.0f, 5.0f),
    };
    const std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};

    const Matrix projection = Projection();
    const Matrix view = Identity();
    const Matrix model = Translation(x);

    gfx::MeshTransform transform = gfx::IdentityMeshTransform();
    Store(projection, transform.projection);
    Store(view, transform.view);
    Store(model, transform.model);

    gfx::MeshConstants constants{};
    constants.flags = gfx::kMeshVertexColor;
    Store(Multiply(Multiply(projection, view), model), constants.mvp);
    constants.diffuse[0] = constants.diffuse[1] = constants.diffuse[2] = constants.diffuse[3] = 1.0f;

    gfx::DrawState state;
    state.depth_test = gfx::DepthTest::Always;

    mgPortDrawMeshImmediate(vertices, indices, constants, gfx::TextureBinding{}, state, &transform);
}

// Mean column of the pixels that read as the quad's red.
void RedCentroidX(const std::vector<uint8_t> &rgba, uint32_t width, uint32_t height, double &x) {
    double sum = 0.0;
    size_t count = 0;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t px = 0; px < width; ++px) {
            const uint8_t *p = &rgba[(static_cast<size_t>(y) * width + px) * 4];
            if (p[0] > 200 && p[1] < 60 && p[2] < 60) {
                sum += px;
                ++count;
            }
        }
    }
    ASSERT_GT(count, 0u);
    x = sum / static_cast<double>(count);
}

void RecordTicks(int count, const void *object, float first_x) {
    for (int tick = 0; tick < count; ++tick) {
        mgBeginFrame(nullptr);
        {
            mgPortInterpScope scope(object, 1000.0f);
            DrawRedQuad(first_x + 10.0f * static_cast<float>(tick));
        }
        mgEndFrame(nullptr);
    }
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

TEST(MglibPresent, DisplayFrameAtHalfInterpolatesTaggedMesh) {
    PresentRenderer renderer(640, 480);
    PresentResetStats();
    PresentSetSettings(PresentSettings{.interpolation = true, .display_per_tick = 0});

    static int object;
    mgPortCutInterpolation();

    std::vector<uint8_t> rgba;
    uint32_t             width = 0;
    uint32_t             height = 0;

    RecordTicks(1, &object, 0.0f);
    ASSERT_TRUE(mgPortCanonicalReadColor(gfx::kMainTarget, rgba, width, height));
    double first = 0.0;
    RedCentroidX(rgba, width, height, first);

    RecordTicks(1, &object, 10.0f);
    ASSERT_TRUE(mgPortCanonicalReadColor(gfx::kMainTarget, rgba, width, height));
    double second = 0.0;
    RedCentroidX(rgba, width, height, second);

    ASSERT_NE(mgPortLastList(), nullptr);
    ASSERT_NE(mgPortPreviousList(), nullptr);
    EXPECT_NEAR(second - first, 10.0 * kPixelsPerUnit, 1.0);

    gfx::RenderOptions options;
    options.previous = mgPortPreviousList();
    options.present = true;
    ASSERT_TRUE(gfx::RenderList(*mgPortLastList(), 0.5f, options));
    ASSERT_TRUE(gfx::ReadbackFrame(rgba, width, height));

    double half = 0.0;
    RedCentroidX(rgba, width, height, half);
    EXPECT_NEAR(half, (first + second) / 2.0, 1.0);
}

TEST(MglibPresent, InterpolationOffPresentsOncePerTick) {
    PresentRenderer renderer(640, 480);
    PresentResetStats();
    PresentSetSettings(PresentSettings{.interpolation = false});

    static int object;
    mgPortCutInterpolation();
    RecordTicks(3, &object, 0.0f);

    const PresentStats stats = PresentGetStats();
    EXPECT_EQ(stats.ticks, 3u);
    EXPECT_EQ(stats.display_frames, 0u);
    EXPECT_EQ(stats.canonical_presents, 3u);
    EXPECT_EQ(stats.presented, 3u);
}

TEST(MglibPresent, DisplayPerTickRendersAndDiscards) {
    PresentRenderer renderer(640, 480);
    PresentResetStats();
    PresentSetSettings(PresentSettings{.interpolation = true, .display_per_tick = 4});

    static int object;
    mgPortCutInterpolation();
    RecordTicks(3, &object, 0.0f);

    const PresentStats stats = PresentGetStats();
    EXPECT_EQ(stats.ticks, 3u);
    EXPECT_EQ(stats.display_frames, 12u);
    EXPECT_EQ(stats.canonical_presents, 3u);
    EXPECT_EQ(stats.presented, 3u);
}

TEST(MglibPresent, DisplayPerTickLeavesCanonicalImage) {
    PresentRenderer renderer(640, 480);
    PresentResetStats();

    static int object;
    std::vector<uint8_t> with_display;
    std::vector<uint8_t> without_display;
    uint32_t             width = 0;
    uint32_t             height = 0;

    PresentSetSettings(PresentSettings{.interpolation = true, .display_per_tick = 4});
    mgPortCutInterpolation();
    RecordTicks(3, &object, 0.0f);
    ASSERT_TRUE(mgPortCanonicalReadColor(gfx::kMainTarget, with_display, width, height));

    PresentSetSettings(PresentSettings{.interpolation = true, .display_per_tick = 0});
    mgPortCutInterpolation();
    RecordTicks(3, &object, 0.0f);
    ASSERT_TRUE(mgPortCanonicalReadColor(gfx::kMainTarget, without_display, width, height));

    // Equal images only prove something if the tick's quad is in them.
    double centroid = 0.0;
    RedCentroidX(with_display, width, height, centroid);
    EXPECT_EQ(with_display, without_display);
}
