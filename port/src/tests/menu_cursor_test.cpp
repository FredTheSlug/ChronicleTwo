#include <gtest/gtest.h>

#include "menudraw.hpp"
#include "mg_drawprim.hpp"
#include "mg_drawprim_port.hpp"
#include "mg_texture.hpp"
#include "mglib.hpp"
#include "mglib_port.hpp"

TEST(MenuCursor, BothBandsKeepTheCursorTextureAndColor) {
    mgRENDER_INFO info{};
    sceVif1Packet packet{};
    auto *saved_info = mgDrawManager.render_info;
    auto *saved_packet = mgVif1Packet;
    const int saved_width = mgScreenWidth, saved_height = mgScreenHeight;
    mgScreenWidth = 512;
    mgScreenHeight = 480;
    mgDrawManager.render_info = &info;
    mgVif1Packet = &packet;
    mgPortSetTextureLookup(nullptr, [](uint64_t tex0) -> mgPortTexture {
        return tex0 != 0 ? mgPortTexture{42, 43} : mgPortTexture{};
    });
    int horizontal = 0, vertical = 0;
    mgPortSetPrimSink([&](gfx::Primitive kind, std::span<const gfx::Vertex2D> vertices,
                         const gfx::TextureBinding &binding, const gfx::DrawState &) {
        if (vertices.empty()) return;
        if (kind == gfx::Primitive::Quads) ++horizontal;
        if (kind == gfx::Primitive::Triangles) ++vertical;
        EXPECT_EQ(binding.texture, 42u);
        for (const auto &vertex : vertices) {
            EXPECT_EQ(vertex.color[0], 90);
            EXPECT_EQ(vertex.color[1], 100);
            EXPECT_EQ(vertex.color[2], 110);
            EXPECT_EQ(vertex.color[3], 60);
        }
    });
    mgCTexture texture;
    texture.tex0.TBP0 = 1234;
    texture.tex0.TW = texture.tex0.TH = 7;
    DrawMenuWakuRect(&texture, {238.0f, 44.0f, 36.0f, 42.0f}, {16, 32, 18, 12}, 60, 90, 100, 110);
    EXPECT_GT(horizontal, 0);
    EXPECT_GT(vertical, 0);
    mgPortSetPrimSink({});
    mgPortSetTextureLookup(nullptr, nullptr);
    mgDrawManager.render_info = saved_info;
    mgVif1Packet = saved_packet;
    mgScreenWidth = saved_width;
    mgScreenHeight = saved_height;
    mgPortResetGs();
}
