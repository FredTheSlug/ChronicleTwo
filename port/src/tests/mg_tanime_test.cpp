#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "gfx/gfx.hpp"
#include "gfx_fixture.hpp"
#include "mg_memory.hpp"
#include "mg_tanime.hpp"
#include "mg_texture.hpp"
#include "mg_texture_port.hpp"
#include "mglib_port.hpp"

namespace {

using Quads = std::vector<u_long128>;
using Levels = std::array<u_long128 *, MG_TEXTURE_LEVEL_MAX>;

constexpr int kTextureCount = 16;
constexpr int kBlockCount = 2;
constexpr size_t kStorageQuads = 4096;
constexpr int kSide = 16;
constexpr int kCopySide = 8;
constexpr int kCopyOrigin = 4;
constexpr uint64_t kRetailTexa = 0x0000008000000080ULL;

Quads Pack(const std::vector<uint8_t> &bytes) {
    Quads quads((bytes.size() + 15) / 16 + 1, 0);
    std::memcpy(quads.data(), bytes.data(), bytes.size());
    return quads;
}

std::vector<uint8_t> Clut8() {
    std::vector<uint8_t> clut(256 * 4);
    for (size_t p = 0; p < 256; p++) {
        clut[p * 4] = static_cast<uint8_t>(p);
        clut[p * 4 + 1] = static_cast<uint8_t>(255 - p);
        clut[p * 4 + 2] = 0x30;
        clut[p * 4 + 3] = 0x80;
    }
    return clut;
}

class Harness {
public:
    Harness() {
        memory.stSetBuffer(storage.data(), static_cast<int>(kStorageQuads));
        manager.SetTableBuffer(kTextureCount, kBlockCount, &memory);
        manager.Initialize(0x1000, -1);
    }

    ~Harness() {
        manager.Initialize(0x1000, -1);
    }

    Harness(const Harness &) = delete;
    Harness &operator=(const Harness &) = delete;

    Quads         storage = Quads(kStorageQuads, 0);
    mgCMemory     memory;
    mgCTextureManager manager;
};

// Restores the process-wide state a case may leave behind.
class StateReset {
public:
    StateReset() = default;
    ~StateReset() {
        mgPortResetGs();
        mgCTextureAnime::stop_anime = 0;
    }
    StateReset(const StateReset &) = delete;
    StateReset &operator=(const StateReset &) = delete;
};

mgCTexture *Enter(Harness &h, std::string name, u_long128 *pixels, u_long128 *clut) {
    Levels levels{};
    levels[0] = pixels;
    return h.manager.EnterTexture(0, name.data(), levels.data(), kSide, kSide, 8, clut, 0, 0);
}

void SetCopy(mgCTexAnimeData &data, mgCTexture *src, mgCTexture *dest, int wait) {
    data.type = static_cast<signed char>(MG_TEX_ANIME_TYPE_COPY);
    data.src_tex = src;
    data.dest_tex = dest;
    data.src_x = 0;
    data.src_y = 0;
    data.src_w = kCopySide * MG_TEX_ANIME_SUBTEXEL;
    data.src_h = kCopySide * MG_TEX_ANIME_SUBTEXEL;
    data.dest_x = kCopyOrigin * MG_TEX_ANIME_SUBTEXEL;
    data.dest_y = kCopyOrigin * MG_TEX_ANIME_SUBTEXEL;
    data.dest_w = kCopySide * MG_TEX_ANIME_SUBTEXEL;
    data.dest_h = kCopySide * MG_TEX_ANIME_SUBTEXEL;
    data.wait = static_cast<short>(wait);
    data.bug_patch = 0;
}

} // namespace

TEST(MgTanime, CopyRecordHoldsTwoCallsThenAdvances) {
    StateReset reset;
    Harness    h;
    Quads      clut = Pack(Clut8());
    Quads      src_px = Pack(std::vector<uint8_t>(kSide * kSide, 0x21));
    Quads      dst_px = Pack(std::vector<uint8_t>(kSide * kSide, 0x55));
    mgCTexture *src = Enter(h, "tanime_src", src_px.data(), clut.data());
    mgCTexture *dst = Enter(h, "tanime_dst", dst_px.data(), clut.data());
    ASSERT_NE(src, nullptr);
    ASSERT_NE(dst, nullptr);

    CList<mgCTexAnimeData> first;
    CList<mgCTexAnimeData> second;
    SetCopy(first.data, src, dst, 2);
    SetCopy(second.data, src, dst, 1);
    first.next = &second;
    second.prev = &first;

    mgCTextureAnime anime;
    anime.list[0] = &first;
    anime.now[0] = &first;
    anime.enable[0] = 1;

    anime.TexAnime(0, nullptr);
    EXPECT_EQ(anime.frame[0], 1);
    EXPECT_EQ(anime.now[0], &first);

    anime.TexAnime(0, nullptr);
    EXPECT_EQ(anime.frame[0], 2);
    EXPECT_EQ(anime.now[0], &first);

    anime.TexAnime(0, nullptr);
    EXPECT_EQ(anime.frame[0], 0);
    EXPECT_EQ(anime.now[0], &second);

    anime.TexAnime(0, nullptr);
    EXPECT_EQ(anime.frame[0], 1);
    EXPECT_EQ(anime.now[0], &second);

    anime.TexAnime(0, nullptr);
    EXPECT_EQ(anime.frame[0], 0);
    EXPECT_EQ(anime.now[0], &first);
}

TEST(MgTanime, StopAnimeFreezesGroup) {
    StateReset reset;
    Harness    h;
    Quads      clut = Pack(Clut8());
    Quads      src_px = Pack(std::vector<uint8_t>(kSide * kSide, 0x21));
    Quads      dst_px = Pack(std::vector<uint8_t>(kSide * kSide, 0x55));
    mgCTexture *src = Enter(h, "tanime_stop_src", src_px.data(), clut.data());
    mgCTexture *dst = Enter(h, "tanime_stop_dst", dst_px.data(), clut.data());
    ASSERT_NE(src, nullptr);
    ASSERT_NE(dst, nullptr);

    CList<mgCTexAnimeData> first;
    CList<mgCTexAnimeData> second;
    SetCopy(first.data, src, dst, 0);
    SetCopy(second.data, src, dst, 0);
    first.next = &second;
    second.prev = &first;

    mgCTextureAnime anime;
    anime.list[0] = &first;
    anime.now[0] = &first;
    anime.enable[0] = 1;

    mgCTextureAnime::stop_anime = 1;
    for (int call = 0; call < 3; call++) {
        anime.TexAnime(0, nullptr);
    }

    EXPECT_EQ(anime.frame[0], 0);
    EXPECT_EQ(anime.now[0], &first);
}

TEST(MgTanime, TexAnimeWritesRetailTexa) {
    StateReset reset;
    mgPortGs().texa = 0;

    mgCTextureAnime anime;
    anime.TexAnime(0, nullptr);

    EXPECT_EQ(mgPortGs().texa, kRetailTexa);
}

class MgTanimeGpu : public testing::Test {
protected:
    dc::test::GfxFixture gfx_;
    Harness              h_;
};

TEST_F(MgTanimeGpu, CopyRecordMovesPixels) {
    StateReset reset;
    Quads      clut = Pack(Clut8());
    std::vector<uint8_t> src_bytes(kSide * kSide);
    for (size_t i = 0; i < src_bytes.size(); i++) {
        src_bytes[i] = static_cast<uint8_t>(i * 7 + 3);
    }
    Quads src_px = Pack(src_bytes);
    Quads dst_px = Pack(std::vector<uint8_t>(kSide * kSide, 0x55));

    mgCTexture *src = Enter(h_, "tanime_gpu_src", src_px.data(), clut.data());
    mgCTexture *dst = Enter(h_, "tanime_gpu_dst", dst_px.data(), clut.data());
    ASSERT_NE(src, nullptr);
    ASSERT_NE(dst, nullptr);
    const MgTextureHandles src_handles = MgTextureLookup(src);
    const MgTextureHandles dst_handles = MgTextureLookup(dst);
    ASSERT_NE(src_handles.texture, gfx::kNullTexture);
    ASSERT_NE(dst_handles.texture, gfx::kNullTexture);

    CList<mgCTexAnimeData> node;
    SetCopy(node.data, src, dst, -1);
    mgCTextureAnime anime;
    anime.list[0] = &node;
    anime.now[0] = &node;
    anime.enable[0] = 1;

    gfx_.Frame({0x10, 0x20, 0x30, 0x80}, [&anime] { anime.TexAnime(0, nullptr); });

    std::vector<uint8_t> pixels;
    uint32_t             width = 0;
    uint32_t             height = 0;
    ASSERT_TRUE(gfx::ReadbackTexture(dst_handles.texture, pixels, width, height));
    ASSERT_EQ(width, static_cast<uint32_t>(kSide));
    ASSERT_EQ(height, static_cast<uint32_t>(kSide));

    for (int y = 0; y < kSide; y++) {
        for (int x = 0; x < kSide; x++) {
            const bool inside = x >= kCopyOrigin && x < kCopyOrigin + kCopySide && y >= kCopyOrigin &&
                                y < kCopyOrigin + kCopySide;
            const uint8_t expected =
                inside ? src_bytes[static_cast<size_t>(y - kCopyOrigin) * kSide + (x - kCopyOrigin)] : 0x55;
            EXPECT_EQ(pixels[static_cast<size_t>(y) * width + x], expected) << "texel " << x << "," << y;
        }
    }
}
