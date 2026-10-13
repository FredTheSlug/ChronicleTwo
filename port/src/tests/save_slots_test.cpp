#include <gtest/gtest.h>

#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include "mainloop.hpp"
#include "inventmn.hpp"
#include "gamepad.hpp"
#include "menuaqua.hpp"
#include "menucls1.hpp"
#include "menucommon.hpp"
#include "menumain.hpp"
#include "menuop.hpp"
#include "platform/files.hpp"
#include "platform/paths.hpp"
#include "platform_fixture.hpp"
#include "save_slots.hpp"

extern CSaveData *PortActiveSaveData asm("MainLoop_ActiveSaveData");
namespace fs = std::filesystem;

namespace {
class SaveSlotTest : public testing::Test {
protected:
    fs::path root;
    std::unique_ptr<SAVEDATA_FORMAT> image = std::make_unique<SAVEDATA_FORMAT>();
    std::unique_ptr<SAVEDATA_FORMAT> buffer = std::make_unique<SAVEDATA_FORMAT>();
    std::unique_ptr<CSaveData> live = std::make_unique<CSaveData>();
    CSaveData *previous = PortActiveSaveData;
    CMemoryCardManager manager;

    void SetUp() override {
        root = fs::temp_directory_path() / ("dc2_save_slots_" + std::to_string(dc::test::ProcessId()));
        fs::remove_all(root);
        fs::create_directories(root / "saves");
        PathsSetSaveRoot(root);
        std::memset(image.get(), 0, sizeof(*image));
        std::strcpy(image->version, "dc2Ver4");
        image->map_no = 10;
        image->save_data.map_no = 10;
        image->save_data.game_progress = 2;
        image->save_data.play_time = 123456;
        image->unique_counter = 1;
        PortActiveSaveData = live.get();
        manager.save_buffer = buffer.get();
        manager.InitForMC();
        SaveSlots = {};
    }
    void TearDown() override {
        manager.FinishForMC();
        PortActiveSaveData = previous;
        SaveSlots = {};
        SaveSlotNew = false;
        PathsReset();
        std::error_code error;
        fs::remove_all(root, error);
    }
    void Write(const fs::path &path, std::size_t size = sizeof(SAVEDATA_FORMAT)) {
        fs::create_directories(path.parent_path());
        ASSERT_EQ(FilesWrite(path, image.get(), size, false), FilesResult::kWritten);
    }
    std::vector<char> Bytes(const fs::path &path) {
        std::ifstream input(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }
    fs::path Legacy(int card, int number) {
        auto name = SaveSlotPath(number).filename();
        return root / (card ? "mc1" : "mc0") / name / name;
    }
};

struct MenuRig {
    CSaveMenuClass menu;
    CMenuKeyFunc common = {};
    CMenuPosDataManage layout = {};
    CMenuPosDataForm form = {};
    CDC2Mes message;
    CMenuKeyFunc *old_common = MenuCommonInfo;
    CMenuPosDataManage *old_layout = MenuPosData;
    CDC2Mes *old_messages[9];
    CMenuPosDataForm *old_forms[9];
    int old_language = LanguageCode;

    MenuRig() {
        LanguageCode = LANG_ENGLISH;
        common.key_enable = true;
        MenuCommonInfo = &common;
        MenuPosData = &layout;
        for (int i = 0; i < 9; ++i) {
            old_messages[i] = MenuDCMsg[i];
            old_forms[i] = MenuMesForm[i];
            MenuDCMsg[i] = &message;
            MenuMesForm[i] = &form;
        }
        menu.first_step = false;
        menu.CBaseMenuClass::mode = MENU_ASK_MODE_NONE;
        menu.mode = SAVE_MENU_MODE_SAVE;
        menu.step = SAVE_MENU_STEP_READY;
        menu.page = SAVE_MENU_PAGE_FILE_LIST;
        menu.phase = SAVE_LIST_PHASE_SELECT;
        menu.cursor_form = &form;
        menu.list_form = &form;
        menu.scrlbar_form = &form;
        menu.script = const_cast<char *>("");
        menu.script_size = 0;
        GamePad__2.key_lock = 0;
        GamePad__2.previous_pad[0].button = 0;
        GamePad__2.pad[0].button = 0;
    }
    ~MenuRig() {
        MenuCommonInfo = old_common;
        MenuPosData = old_layout;
        LanguageCode = old_language;
        for (int i = 0; i < 9; ++i) {
            MenuDCMsg[i] = old_messages[i];
            MenuMesForm[i] = old_forms[i];
        }
        GamePad__2.pad[0].button = 0;
    }
    void Press(int button) {
        GamePad__2.pad[0].button = button;
        menu.KeyStep();
        GamePad__2.pad[0].button = 0;
    }
    void Idle() { for (int i = 0; i < 10; ++i) menu.KeyStep(); }
};
} // namespace

TEST_F(SaveSlotTest, NamesAreCanonicalAndIgnoreTemporaryFiles) {
    bool second = false;
    EXPECT_EQ(SaveSlotNumber("BESCES-51190dkcl0", second), 0);
    EXPECT_FALSE(second);
    EXPECT_EQ(SaveSlotNumber("BESCES-51190dkcl999999999.card2", second), 999999999);
    EXPECT_TRUE(second);
    for (auto *name : {"BESCES-51190dkcl01", "BESCES-51190dkcl-1", "BESCES-51190dkcl1000000000",
                       "BESCES-51190dkcl0.123.tmp", "BESCES-51190dkcl", "icon.sys", "../BESCES-51190dkcl0"}) {
        EXPECT_EQ(SaveSlotNumber(name, second), -1) << name;
    }
}

TEST_F(SaveSlotTest, ImportsBothCardsByteExactlyWithoutTouchingOriginals) {
    for (int number = 0; number < 3; ++number) Write(Legacy(0, number));
    image->unique_counter = 99;
    Write(Legacy(1, 0));
    auto list = SaveSlotScan();
    ASSERT_EQ(list.saves.size(), 4);
    EXPECT_FALSE(list.read_failed);
    for (int number = 0; number < 3; ++number) EXPECT_EQ(Bytes(Legacy(0, number)), Bytes(SaveSlotPath(number)));
    EXPECT_EQ(Bytes(Legacy(1, 0)), Bytes(SaveSlotPath(0, true)));
    EXPECT_NE(Bytes(SaveSlotPath(0)), Bytes(SaveSlotPath(0, true)));
    EXPECT_EQ(SaveSlotScan().saves.size(), 4); // Reopening never duplicates imports.
}

TEST_F(SaveSlotTest, ExistingFlatFileWinsOverOlderCardCopy) {
    Write(Legacy(0, 0));
    image->unique_counter = 42;
    Write(SaveSlotPath(0));
    auto before = Bytes(SaveSlotPath(0));
    auto list = SaveSlotScan();
    ASSERT_EQ(list.saves.size(), 1);
    EXPECT_EQ(list.saves[0].info.unique_counter, 42);
    EXPECT_EQ(Bytes(SaveSlotPath(0)), before);
    EXPECT_NE(Bytes(Legacy(0, 0)), before);
}

TEST_F(SaveSlotTest, UnusableEntriesStayVisibleAndReserveTheirNames) {
    Write(SaveSlotPath(0), 100);
    image->incomplete = 1;
    Write(SaveSlotPath(1));
    image->incomplete = 0;
    image->check_digit = 999999;
    Write(SaveSlotPath(2));
    image->check_digit = 0;
    std::memset(image->version, 'x', sizeof(image->version));
    Write(SaveSlotPath(3));
    fs::create_directory(SaveSlotPath(4));
    fs::create_directories(Legacy(0, 5).parent_path());
    auto before = Bytes(SaveSlotPath(2));
    auto list = SaveSlotScan();
    ASSERT_EQ(list.saves.size(), 4);
    EXPECT_EQ(list.saves[0].state, SaveSlotState::WrongSize);
    EXPECT_EQ(list.saves[1].state, SaveSlotState::Incomplete);
    EXPECT_EQ(list.saves[2].state, SaveSlotState::Ready);
    EXPECT_EQ(SaveSlotRead(SaveSlotPath(2), *buffer), SaveSlotState::BadChecksum);
    EXPECT_EQ(list.saves[3].state, SaveSlotState::WrongVersion);
    EXPECT_EQ(SaveSlotRows(list, true), 5);
    EXPECT_EQ(SaveSlotRows(list, false), 4);
    EXPECT_EQ(SaveSlotFirstFree(list), 5);
    EXPECT_EQ(Bytes(SaveSlotPath(2)), before);
}

TEST_F(SaveSlotTest, CatalogHasNoRetailSlotLimitAndOnlyOneNewRow) {
    for (int number = 0; number < 31; ++number) {
        image->unique_counter = number + 1;
        Write(SaveSlotPath(number));
    }
    manager.GetAllSaveFileInfo();
    EXPECT_EQ(manager.CheckDataFileNum(), 31);
    EXPECT_EQ(manager.GetUpdateFile(), 30);
    EXPECT_EQ(manager.CheckMaxUniqueCounter(), 31);
    EXPECT_EQ(SaveSlotRows(SaveSlots, false), 31);
    EXPECT_EQ(SaveSlotRows(SaveSlots, true), 32);
    for (int row = -1; row <= 32; ++row) {
        EXPECT_EQ(SaveSlotIsNew(SaveSlots, row, true), row == 31);
        EXPECT_FALSE(SaveSlotIsNew(SaveSlots, row, false));
    }
    SaveSlotNew = true;
    manager.SaveToMc(31);
    ASSERT_EQ(manager.error.code, MC_ERROR_NONE);
    EXPECT_TRUE(fs::is_regular_file(SaveSlotPath(31)));
    EXPECT_EQ(SaveSlotRows(SaveSlots, true), 33);
}

TEST_F(SaveSlotTest, NewSaveCannotReplaceAFileCreatedAfterDiscovery) {
    manager.GetAllSaveFileInfo();
    Write(SaveSlotPath(0));
    auto original = Bytes(SaveSlotPath(0));
    SaveSlotNew = true;
    manager.SaveToMc(0);
    ASSERT_EQ(manager.error.code, MC_ERROR_NONE);
    EXPECT_EQ(Bytes(SaveSlotPath(0)), original);
    EXPECT_TRUE(fs::is_regular_file(SaveSlotPath(1)));
    EXPECT_EQ(manager.file_no, 1);
}

TEST_F(SaveSlotTest, LoadBeyondRetailLimitRestoresPayloadAndTransition) {
    image->program_loop_no = LOOP_DUNGEON;
    image->dungeon_no = 2;
    image->floor_id = 4;
    image->dng_tree_flag = 1;
    Write(SaveSlotPath(81));
    manager.GetAllSaveFileInfo();
    ASSERT_EQ(manager.LoadFromMc(0), MC_STEP_DONE);
    EXPECT_EQ(manager.error.code, MC_ERROR_NONE);
    EXPECT_EQ(live->map_no, 10);
    EXPECT_EQ(live->play_time, 123456);
    EXPECT_EQ(manager.load_program_loop_no, LOOP_DUNGEON);
    EXPECT_EQ(manager.load_dungeon_no, 2);
    EXPECT_EQ(manager.load_floor_id, 4);
    EXPECT_EQ(manager.load_dng_tree_flag, 1);
}

TEST_F(SaveSlotTest, FailedLoadDoesNotPartiallyReplaceLiveStateOrDeleteFile) {
    image->check_digit_half = 123456;
    Write(SaveSlotPath(0));
    auto before = Bytes(SaveSlotPath(0));
    live->map_no = 42;
    live->play_time = 777;
    manager.GetAllSaveFileInfo();
    manager.LoadFromMc(0);
    EXPECT_EQ(manager.error.code, MC_ERROR_LOAD);
    EXPECT_EQ(live->map_no, 42);
    EXPECT_EQ(live->play_time, 777);
    EXPECT_EQ(Bytes(SaveSlotPath(0)), before);
}

TEST_F(SaveSlotTest, ConfirmedOverwritePublishesCompleteImageAndKeepsLegacyBackup) {
    Write(Legacy(0, 0));
    auto original = Bytes(Legacy(0, 0));
    manager.GetAllSaveFileInfo();
    live->map_no = 21;
    live->play_time = 9999;
    SetDngTreeFlag(1);
    SaveSlotNew = false; // The menu calls the writer only after its Yes confirmation.
    manager.SaveToMc(0);
    ASSERT_EQ(manager.error.code, MC_ERROR_NONE);
    EXPECT_EQ(SaveSlotRead(SaveSlotPath(0), *image), SaveSlotState::Ready);
    EXPECT_EQ(image->save_data.map_no, 21);
    EXPECT_EQ(image->save_data.play_time, 9999);
    EXPECT_EQ(image->incomplete, 0);
    EXPECT_EQ(image->dng_tree_flag, 1);
    EXPECT_EQ(Bytes(Legacy(0, 0)), original);
    int files = 0;
    for (const auto &entry : fs::directory_iterator(SaveSlotsRoot())) {
        EXPECT_FALSE(entry.path().string().ends_with(".tmp"));
        ++files;
    }
    EXPECT_EQ(files, 1);
}

TEST_F(SaveSlotTest, FailedPublicationPreservesOccupiedNameAndReportsError) {
    fs::create_directory(SaveSlotPath(0));
    std::ofstream(SaveSlotPath(0) / "keep.txt") << "keep";
    manager.GetAllSaveFileInfo();
    SaveSlotNew = false;
    manager.SaveToMc(0);
    EXPECT_EQ(manager.error.code, MC_ERROR_FILE);
    EXPECT_TRUE(fs::exists(SaveSlotPath(0) / "keep.txt"));
    EXPECT_TRUE(SaveSlotScan().saves.empty());
}

TEST_F(SaveSlotTest, InvalidCatalogRootReportsErrorAndRefusesNewSave) {
    fs::remove(root / "saves");
    std::ofstream(root / "saves") << "occupied";
    manager.GetAllSaveFileInfo();
    EXPECT_TRUE(SaveSlots.read_failed);
    SaveSlotNew = true;
    manager.SaveToMc(0);
    EXPECT_EQ(manager.error.code, MC_ERROR_FILE);
    EXPECT_TRUE(fs::is_regular_file(root / "saves"));
}

TEST_F(SaveSlotTest, CorruptRowUsesRetailOverwriteConfirmationAndNoNeverWrites) {
    Write(SaveSlotPath(0), 100);
    auto before = Bytes(SaveSlotPath(0));
    manager.GetAllSaveFileInfo();
    MenuRig rig;
    rig.Idle();
    rig.Press(PAD_CROSS);
    EXPECT_EQ(rig.menu.phase, SAVE_LIST_PHASE_CONFIRM_SAVE);
    EXPECT_EQ(rig.menu.save_kind, SAVE_FILE_OVERWRITE);
    EXPECT_EQ(rig.message.mes_no, 0xBC4);
    EXPECT_EQ(rig.message.GetMsgCursor(), 1);
    rig.Idle();
    rig.Press(PAD_CROSS);
    EXPECT_EQ(rig.menu.phase, SAVE_LIST_PHASE_SELECT);
    EXPECT_EQ(manager.GetFuncNo(), MC_FUNC_IDLE);
    EXPECT_EQ(Bytes(SaveSlotPath(0)), before);
}

TEST_F(SaveSlotTest, OverwriteRequiresYesAndRetainsConfirmationInputDelay) {
    Write(SaveSlotPath(0));
    manager.GetAllSaveFileInfo();
    MenuRig rig;
    rig.Idle();
    rig.Press(PAD_CROSS);
    rig.Idle();
    rig.Press(PAD_LEFT | PAD_CROSS);
    EXPECT_EQ(rig.menu.phase, SAVE_LIST_PHASE_CONFIRM_SAVE);
    EXPECT_EQ(rig.message.GetMsgCursor(), 0);
    EXPECT_EQ(manager.GetFuncNo(), MC_FUNC_IDLE);
    rig.Idle();
    rig.Press(PAD_CROSS);
    EXPECT_EQ(rig.menu.phase, SAVE_LIST_PHASE_SAVING);
    EXPECT_EQ(manager.GetFuncNo(), MC_FUNC_SAVE);
    EXPECT_FALSE(SaveSlotNew);
}

TEST_F(SaveSlotTest, MenuScrollsPastThirteenAndClampsItsVisibleWindow) {
    for (int i = 0; i < 31; ++i) Write(SaveSlotPath(i));
    manager.GetAllSaveFileInfo();
    MenuRig rig;
    rig.menu.select = 12;
    rig.menu.top = 10;
    rig.Idle();
    rig.Press(PAD_DOWN);
    EXPECT_EQ(rig.menu.select, 13);
    EXPECT_EQ(rig.menu.top, 11);
    rig.menu.select = 31;
    rig.menu.top = 31;
    rig.Press(PAD_DOWN);
    EXPECT_EQ(rig.menu.select, 31);
    EXPECT_EQ(rig.menu.top, 29);
    EXPECT_TRUE(SaveSlotIsNew(SaveSlots, rig.menu.select, true));
}

TEST_F(SaveSlotTest, DeletedImportsStayDeletedAcrossLaterScans) {
    Write(Legacy(0, 0));
    Write(Legacy(1, 0));
    ASSERT_EQ(SaveSlotScan().saves.size(), 2);
    fs::remove(SaveSlotPath(0));
    fs::remove(SaveSlotPath(0, true));
    SaveSlots = {};
    EXPECT_TRUE(SaveSlotScan().saves.empty());
    EXPECT_TRUE(fs::exists(Legacy(0, 0)));
    EXPECT_TRUE(fs::exists(Legacy(1, 0)));
}

TEST_F(SaveSlotTest, ExistingFlatSaveRetiresItsLegacyCopyBeforeManualDeletion) {
    Write(Legacy(0, 3));
    image->unique_counter = 42;
    Write(SaveSlotPath(3));
    ASSERT_EQ(SaveSlotScan().saves.size(), 1);
    fs::remove(SaveSlotPath(3));
    EXPECT_TRUE(SaveSlotScan().saves.empty());
}

TEST_F(SaveSlotTest, ListingDefersChecksumValidationUntilLoad) {
    image->check_digit = 999999;
    Write(SaveSlotPath(0));
    auto list = SaveSlotScan();
    ASSERT_EQ(list.saves.size(), 1);
    EXPECT_EQ(list.saves[0].state, SaveSlotState::Ready);
    EXPECT_EQ(list.saves[0].info.play_time, 123456);
    EXPECT_EQ(SaveSlotRead(SaveSlotPath(0), *buffer), SaveSlotState::BadChecksum);
}

TEST_F(SaveSlotTest, HeaderCacheUsesSizeAndTimeWhileLoadAlwaysRevalidates) {
    Write(SaveSlotPath(0));
    ASSERT_EQ(SaveSlotScan().saves.size(), 1);
    const auto stamp = fs::last_write_time(SaveSlotPath(0));
    std::strcpy(image->version, "invalid");
    ASSERT_EQ(FilesWrite(SaveSlotPath(0), image.get(), sizeof(*image), true), FilesResult::kWritten);
    fs::last_write_time(SaveSlotPath(0), stamp);
    auto cached = SaveSlotScan();
    ASSERT_EQ(cached.saves.size(), 1);
    EXPECT_EQ(cached.saves[0].state, SaveSlotState::Ready);
    EXPECT_EQ(SaveSlotRead(SaveSlotPath(0), *buffer), SaveSlotState::WrongVersion);
    fs::last_write_time(SaveSlotPath(0), stamp + std::chrono::seconds(2));
    EXPECT_EQ(SaveSlotScan().saves[0].state, SaveSlotState::WrongVersion);
    fs::remove(SaveSlotPath(0));
    Write(SaveSlotPath(0), 100);
    EXPECT_EQ(SaveSlotScan().saves[0].state, SaveSlotState::WrongSize);
}

TEST_F(SaveSlotTest, UnreadableUnrelatedEntriesAreSkipped) {
    Write(SaveSlotPath(0));
    fs::create_directory(SaveSlotPath(1));
    auto list = SaveSlotScan();
    ASSERT_EQ(list.saves.size(), 1);
    EXPECT_FALSE(list.read_failed);
    EXPECT_EQ(list.saves[0].number, 0);
}

TEST_F(SaveSlotTest, DurableSaveRemainsSuccessfulWhenPostWriteScanFails) {
    Write(SaveSlotPath(0));
    manager.GetAllSaveFileInfo();
    // The catalog was readable at selection; an unrelated legacy-card root
    // becomes unreadable before the confirmed overwrite is published.
    std::ofstream(root / "mc1") << "occupied";
    live->map_no = 21;
    SaveSlotNew = false;
    ASSERT_EQ(manager.SaveToMc(0), MC_STEP_DONE);
    EXPECT_EQ(manager.error.code, MC_ERROR_NONE);
    EXPECT_EQ(manager.total_transferred, sizeof(SAVEDATA_FORMAT));
    EXPECT_TRUE(SaveSlots.read_failed);
    ASSERT_EQ(SaveSlotRead(SaveSlotPath(0), *image), SaveSlotState::Ready);
    EXPECT_EQ(image->save_data.map_no, 21);
}

TEST_F(SaveSlotTest, VersionTwoLoadsAndUsesRetailInventionConversion) {
    std::strcpy(image->version, "dc2Ver2");
    // Retail's old INVENT_CREATED_ITEM record is twelve bytes; conversion
    // copies the first two shorts of each of its 128 entries to four-byte cards.
    struct OldCard { short item_id; unsigned short flags; unsigned char unused[8]; };
    static_assert(sizeof(OldCard) == 12);
    auto *source = image->save_data.GetUserDataManager()->GetInventUserData();
    for (int i = 0; i < 128; ++i) {
        const OldCard card{static_cast<short>(i + 1), static_cast<unsigned short>(i + 100), {}};
        auto *bytes = reinterpret_cast<char *>(source) + offsetof(CInventUserData, created_item);
        std::memcpy(bytes + i * sizeof(OldCard), &card, sizeof(card));
    }
    image->check_digit = image->check_digit_half = 999999; // Retail does not check Ver2 digits.
    Write(Legacy(0, 7));
    manager.GetAllSaveFileInfo();
    ASSERT_EQ(SaveSlots.saves.size(), 1);
    EXPECT_EQ(SaveSlots.saves[0].state, SaveSlotState::Ready);
    ASSERT_EQ(manager.LoadFromMc(0), MC_STEP_DONE);
    ASSERT_EQ(manager.error.code, MC_ERROR_NONE);
    auto *converted = live->GetUserDataManager()->GetInventUserData();
    for (int i = 0; i < 128; ++i) {
        EXPECT_EQ(converted->created_item[i].item_id, i + 1);
        EXPECT_EQ(converted->created_item[i].unk_2, i + 100);
    }
    EXPECT_EQ(live->map_no, 10);
}
