#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <memory>
#include <vector>

#include "gamedata.hpp"
#include "gfx_fixture.hpp"
#include "mainloop.hpp"
#include "menudraw.hpp"
#include "menumain.hpp"
#include "mg_drawprim_port.hpp"
#include "mg_texture_port.hpp"
#include "mglib.hpp"
#include "savedata.hpp"

extern CSaveData *PortActiveSaveData asm("MainLoop_ActiveSaveData");

namespace {
class MenuIconGpu : public testing::Test {
protected:
    dc::test::GfxFixture gfx_;
    std::vector<u_long128> arena_ = std::vector<u_long128>(8192);
    mgCMemory memory_;
    std::vector<u_long128> atlas_ = std::vector<u_long128>(256 * 1024 / 16);
    std::array<u_long128, 64> palette_{};
    CDataCommon common_{};
    int saved_width_ = mgScreenWidth, saved_height_ = mgScreenHeight;
    CGameData saved_data_ = GameItemDataManage;
    CMenuPosDataManage *saved_menu_ = MenuPosData;
    CScene *saved_scene_ = MenuMainScene;
    sceVif1Packet *saved_packet_ = mgVif1Packet;
    CSaveData *saved_save_ = PortActiveSaveData;
    CMenuPosDataManage menu_;
    std::unique_ptr<CSaveData> save_ = std::make_unique<CSaveData>();

    void SetUp() override {
        mgScreenWidth = 640;
        mgScreenHeight = 480;
        memory_.stSetBuffer(arena_.data(), static_cast<int>(arena_.size()));
        mgTexManager.SetTableBuffer(16, 4, &memory_);
        mgTexManager.Initialize(0x1000, -1);
        // Icon 11 is on the second atlas row; the reserved cell deliberately differs.
        std::memset(atlas_.data(), 3, atlas_.size() * 16);
        auto *bytes = reinterpret_cast<u8 *>(atlas_.data());
        for (int row = 32; row < 64; ++row) {
            std::memset(bytes + row * 256 + 96, 7, 32);
            std::memset(bytes + row * 256 + 128, 9, 32);
        }
        auto *colors = reinterpret_cast<u8 *>(palette_.data());
        colors[7 * 4 + 1] = 255;
        colors[7 * 4 + 3] = 128;
        colors[17 * 4 + 2] = 255;
        colors[17 * 4 + 3] = 128;
        colors[3 * 4] = 255;
        colors[3 * 4 + 3] = 128;
        common_.icon_no = 11;
        common_.icon_texture_no = 1;
        GameItemDataManage.common_data = &common_;
        ASSERT_EQ(GameItemDataManage.GetCommonData(1), &common_);
        MenuPosData = &menu_;
        MenuMainScene = nullptr;
        PortActiveSaveData = save_.get();
    }
    void TearDown() override {
        mgVif1Packet = saved_packet_;
        mgPortSetPrimSink({});
        mgTexManager.Initialize(0x1000, -1);
        mgScreenWidth = saved_width_;
        mgScreenHeight = saved_height_;
        GameItemDataManage = saved_data_;
        MenuPosData = saved_menu_;
        MenuMainScene = saved_scene_;
        PortActiveSaveData = saved_save_;
    }
    mgCTexture *Enter(char *name, int width, int height, u_long128 *pixels, u_long128 *clut) {
        u_long128 *levels[MG_TEXTURE_LEVEL_MAX] = {pixels};
        return mgTexManager.EnterTexture(0, name, levels, width, height, 8, clut, 0, 0);
    }
};
}

TEST_F(MenuIconGpu, ActiveIconsRefreshPixelsAndPaletteAfterInPlaceCopy) {
    char item_name[] = "itemicon", weapon_name[] = "wepicon";
    Enter(item_name, 256, 1024, atlas_.data(), palette_.data());
    Enter(weapon_name, 256, 1024, atlas_.data(), palette_.data());
    std::array<u_long128, 256> item_pixels{}, weapon_pixels{};
    std::array<u_long128, 64> item_palette{}, weapon_palette{};
    char dummy1[] = "icon_dmy1", dummy2[] = "icon_dmy2";
    mgCTexture *dest[] = {Enter(dummy1, 64, 64, item_pixels.data(), item_palette.data()),
                         Enter(dummy2, 64, 64, weapon_pixels.data(), weapon_palette.data())};
    save_->user_data.chara_data[0].active_item[0].item_no = 1;
    save_->user_data.chara_data[0].equip[0].item_no = 1;
    const auto saved_user = std::make_unique<CUserDataManager>(save_->user_data);
    const auto key = dest[0]->tex0.value;
    const auto old_handles = MgTextureLookup(dest[0]);
    ASSERT_EQ(CopyActiveIconTexture(dest, 0, nullptr), 1);
    EXPECT_EQ(dest[0]->tex0.value, key);
    EXPECT_EQ(MgTextureLookup(dest[0]).texture, old_handles.texture);
    EXPECT_EQ(MgTextureLookup(dest[0]).palette, old_handles.palette);
    EXPECT_EQ(std::memcmp(saved_user.get(), &save_->user_data, sizeof(CUserDataManager)), 0);
    // CPU copies already worked before the fix, but the renderer still held dummy pixels.
    EXPECT_EQ(reinterpret_cast<u8 *>(item_pixels.data())[0], 7);
    for (mgCTexture *texture : dest) {
        auto handles = MgTextureLookup(texture);
        std::vector<u8> pixels;
        uint32_t width, height;
        ASSERT_TRUE(gfx::ReadbackTexture(handles.texture, pixels, width, height));
        EXPECT_EQ(pixels[0], 7);
        EXPECT_EQ(pixels[32], 0); // absent second slot stays transparent
        ASSERT_TRUE(gfx::ReadbackTexture(handles.palette, pixels, width, height));
        EXPECT_EQ(pixels[7 * 4 + 1], 255);
        EXPECT_EQ(pixels[7 * 4 + 3], 255);
    }
}

TEST_F(MenuIconGpu, ItemDrawStagesTheSelectedAtlasCellBeforeSampling) {
    char name[] = "itemicon";
    auto *texture = Enter(name, 256, 1024, atlas_.data(), palette_.data());
    menu_.item_icon_tex[0][1] = texture;
    mgCMemory memory;
    sceVif1Packet packet{};
    mgVif1Packet = &packet;
    mgRENDER_INFO info{};
    mgCDrawManager manager;
    mgCDrawPrim prim;
    manager.render_info = &info;
    prim.draw_manager = &manager;
    prim.Initialize(&memory, &packet);
    prim.write = nullptr;
    prim.Coord(1);
    int draws = 0;
    mgPortSetPrimSink([&](gfx::Primitive kind, std::span<const gfx::Vertex2D> vertices,
                         const gfx::TextureBinding &binding, const gfx::DrawState &state) {
        auto draw_state = state;
        draw_state.scissor = false;
        gfx::Draw2D(kind, vertices, binding, draw_state);
        ++draws;
    });
    u8 rgba[] = {128, 128, 128, 128};
    gfx::BeginRecording();
    const u8 clear[] = {0, 0, 0, 255};
    gfx::Clear(true, clear, true, 0.0f);
    DrawOneItem(&prim, mgRect<float>(10, 10, 32, 32), 1, 0, nullptr, rgba, 0);
    common_.icon_no = 12;
    DrawOneItem(&prim, mgRect<float>(50, 10, 32, 32), 1, 0, nullptr, rgba, 0);
    auto list = gfx::EndRecording();
    ASSERT_TRUE(gfx::RenderList(*list, 1.0f, {.canonical = true}));
    ASSERT_TRUE(gfx::ReadbackFrame(gfx_.pixels, gfx_.width, gfx_.height));
    EXPECT_GE(draws, 2);
    EXPECT_TRUE(gfx_.PixelNear(20, 20, 0, 255, 0));
    EXPECT_TRUE(gfx_.PixelNear(60, 20, 0, 0, 255));
}

TEST_F(MenuIconGpu, EffectPaletteComesFromSelectedDescriptorAndRestoresNormally) {
    char name[] = "itemicon";
    auto *base = Enter(name, 256, 1024, atlas_.data(), palette_.data());
    std::array<u_long128, 64> alternate = palette_;
    auto *colors = reinterpret_cast<u8 *>(alternate.data());
    colors[7 * 4 + 1] = 0;
    colors[7 * 4 + 2] = 255;
    mgCTexture variant = *base;
    variant.clut = alternate.data();
    menu_.item_icon_tex[0][1] = base;
    menu_.item_icon_tex[2][1] = &variant;
    ASSERT_EQ(GetMenuItemIconTexInfo(1, 2), &variant);
    auto handles = MgTextureLookup(base);
    std::vector<u8> pixels;
    uint32_t width, height;
    ASSERT_TRUE(gfx::ReadbackTexture(handles.palette, pixels, width, height));
    EXPECT_EQ(pixels[7 * 4 + 1], 0);
    EXPECT_EQ(pixels[7 * 4 + 2], 255);
    mgTexManager.ReloadCLUT(base, static_cast<sceVif1Packet *>(nullptr));
    ASSERT_TRUE(gfx::ReadbackTexture(handles.palette, pixels, width, height));
    EXPECT_EQ(pixels[7 * 4 + 1], 255);
    EXPECT_EQ(pixels[7 * 4 + 2], 0);
}
