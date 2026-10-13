#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "dataread.hpp"
#include "platform/texture_catalog.hpp"
#include "texture_source.hpp"

namespace {

struct CatalogFixture {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("texture_pack_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    CatalogFixture() { std::filesystem::create_directories(path); }
    ~CatalogFixture() {
        texture_catalog::Clear();
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

} // namespace

TEST(TexturePack, RemovedMembersUseDistinctCatalogHandlesAndSurviveCopies) {
    CatalogFixture fixture;
    std::ofstream(fixture.path / "catalog.json")
        << R"({"schema_version":1,"sources":[{"source":"chara/foo.chr/a.img","kind":"img","entries":[]},{"source":"chara/foo.chr/b.img","kind":"img","entries":[]}]})";
    std::string error;
    ASSERT_TRUE(texture_catalog::Load(fixture.path / "catalog.json", error)) << error;

    alignas(PACK_ENTRY) std::array<unsigned char, 3 * sizeof(PACK_ENTRY)> pack{};
    auto *first = reinterpret_cast<PACK_ENTRY *>(pack.data());
    auto *second = first + 1;
    std::memcpy(first->name, "a.img", 6);
    std::memcpy(second->name, "b.img", 6);
    first->next = second->next = sizeof(PACK_ENTRY);
    first->offset = second->offset = 2 * sizeof(PACK_ENTRY);
    TextureSourceRegister(pack.data(), "chara/foo.chr");

    char a_name[] = "a.img", b_name[] = "b.img";
    int a_size = -1, b_size = -1;
    u_int *a = GetPackFile(reinterpret_cast<u_int *>(pack.data()), a_name, &a_size);
    u_int *b = GetPackFile(reinterpret_cast<u_int *>(pack.data()), b_name, &b_size);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NE(a, b);
    EXPECT_EQ(a_size, 16);
    EXPECT_EQ(b_size, 16);
    EXPECT_EQ(TextureSourceLookup(a), "chara/foo.chr/a.img");
    EXPECT_EQ(TextureSourceLookup(b), "chara/foo.chr/b.img");

    std::array<u_int, 4> copied{};
    std::memcpy(copied.data(), a, sizeof copied);
    TextureSourceCopy(a, copied.data(), sizeof copied);
    EXPECT_EQ(TextureSourceLookup(copied.data()), "chara/foo.chr/a.img");
}

TEST(TexturePack, EmbeddedTextureOffsetSurvivesWholeFileCopy) {
    TextureSourceClear();
    std::array<unsigned char, 192> wrapper{};
    std::array<unsigned char, 192> moved{};
    TextureSourceRegister(wrapper.data(), "dungeon/eff_script/e211aex_knife.chr", wrapper.size());
    ASSERT_EQ(TextureSourceLookup(wrapper.data() + 80),
              "dungeon/eff_script/e211aex_knife.chr#offset:80");
    std::memcpy(moved.data(), wrapper.data(), wrapper.size());
    TextureSourceCopy(wrapper.data(), moved.data(), moved.size());
    EXPECT_EQ(TextureSourceLookup(moved.data() + 80),
              "dungeon/eff_script/e211aex_knife.chr#offset:80");
    TextureSourceClear();
}
