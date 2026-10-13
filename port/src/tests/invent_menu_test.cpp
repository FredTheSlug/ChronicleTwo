#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "actionchara.hpp"
#include "dataread.hpp"
#include "inventmn.hpp"
#include "menuchr.hpp"
#include "menucls1.hpp"
#include "menudraw.hpp"
#include "menumain.hpp"
#include "mg_memory.hpp"
#include "mg_texture.hpp"
#include "mglib.hpp"
#include "stubs/inventmn.hpp"

namespace {
std::vector<u_int> MenuPack() {
    const std::string                          forms = "FORM_MALLOC 1; FORM_OFFSET_NO 0; FORM_SET \"poly_chr0\"; INIT_DRAWLIST;";
    const std::string                          recipes = "DATATABLESET 1; DATASET 42,1,2,3,210,5,0,1.0,2.0,3.0,4.0;";
    const std::pair<const char *, std::string> files[] = {
        {"inv_bg.img", std::string("IMG\0", 4) + std::string(12, '\0')},
        {"invent.cfg", forms},
        {"inv6.lst", recipes},
        {"inv_com.cfg", "INIT_END;"}
    };
    std::vector<u_int> pack(1024);
    auto              *bytes = reinterpret_cast<char *>(pack.data());
    std::size_t        data = 5 * sizeof(PACK_ENTRY);
    for (std::size_t i = 0; i < 4; ++i) {
        PACK_ENTRY entry{};
        std::strcpy(entry.name, files[i].first);
        entry.offset = data - i * sizeof(PACK_ENTRY);
        entry.size = files[i].second.size();
        entry.next = sizeof(PACK_ENTRY);
        std::memcpy(bytes + i * sizeof(PACK_ENTRY), &entry, sizeof(entry));
        std::memcpy(bytes + data, files[i].second.data(), files[i].second.size());
        data += (files[i].second.size() + 3) & ~std::size_t(3);
    }
    return pack;
}

TEST(InventMenu, PendingPhotoPageDoesNotFinishCharacterLoadingBeforeItsFormsExist) {
    auto         menu = std::make_unique<CMenuInvent>();
    CActionChara character;
    character.Initialize(nullptr);
    MenuActionChara[0] = &character;
    MenuActionChara[3] = nullptr;
    menu->photo_only = 1;
    menu->opened = false;
    menu->chara_load_step = INVENT_CHARA_LOAD_WAIT;
    menu->poly_chr_form[0] = nullptr;
    menu->script = nullptr;
    menu->script_size = 0;
    menu->tex_block[1] = -1;
    StartReadBG();

    menu->LoadCharaCheck();

    EXPECT_EQ(menu->chara_load_step, INVENT_CHARA_LOAD_WAIT);
    EXPECT_EQ(menu->poly_chr_form[0], nullptr);
    MenuActionChara[0] = nullptr;
}
} // namespace

TEST(InventMenu, LoadsPackedFormsAndRecipesBeforeAttachingThePhotoCharacter) {
    std::vector<u_long128> storage(4096), texture_storage(4096);
    mgCMemory              textures;
    textures.stSetBuffer(texture_storage.data(), texture_storage.size());
    mgTexManager.SetTableBuffer(64, 8, &textures);
    mgTexManager.Initialize(0x1000, -1);
    CMenuPosDataManage positions{};
    positions.Initialize();
    CMenuMoveItem     move;
    CInventDataManage recipes;
    auto              photos = std::make_unique<CInventUserData>();
    photos->ResetAddress();
    auto menu = std::make_unique<CMenuInvent>();
    menu->data_stack.stSetBuffer(storage.data(), storage.size());
    menu->tex_block[3] = 1;
    MenuPosData = &positions;
    MenuMoveItemPtr = &move;
    InventManagePt = &recipes;
    InventUserDataPtr = photos.get();
    auto pack = MenuPack();
    menu->EnterDataMenu(reinterpret_cast<u8 *>(pack.data()));

    EXPECT_NE(menu->poly_chr_form[0], nullptr);
    EXPECT_EQ(positions.form_num, 1);
    ASSERT_EQ(recipes.num, 1);
    EXPECT_EQ(recipes.table[0].item_id, 42);
    EXPECT_EQ(recipes.table[0].materials.material[0].num, 5);
    EXPECT_EQ(menu->script_size, 9);
    mgTexManager.Initialize(0x1000, -1);
}
