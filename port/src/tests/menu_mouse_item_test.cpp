#include <gtest/gtest.h>

#include "../menu_mouse_item.hpp"
#include "gamepad.hpp"
#include "mainloop.hpp"
#include "menuaqua.hpp"
#include "menuchr.hpp"
#include "menucls1.hpp"
#include "menudraw.hpp"
#include "menumain.hpp"
#include "menusys.hpp"
#include "mglib.hpp"
#include "platform/clock.hpp"
#include "platform/input.hpp"

using namespace menumouse;

TEST(MenuMouse, GridUsesSixColumnsAndHalfOpenCellEdges) {
    const Point origin{240, 50};
    const Rect  clip{240, 44, 240, 250};
    EXPECT_EQ(GridHit({240, 50}, origin, clip, 144), (Target{ITEM_MENU_PANEL_BAG, 0}));
    EXPECT_EQ(GridHit({279.99f, 99.99f}, origin, clip, 144).cursor, 0);
    EXPECT_EQ(GridHit({280, 50}, origin, clip, 144).cursor, 1);
    EXPECT_EQ(GridHit({240, 100}, origin, clip, 144).cursor, 6);
    EXPECT_FALSE(GridHit({480, 50}, origin, clip, 144));
    EXPECT_FALSE(GridHit({240, 294}, origin, clip, 144));
    EXPECT_FALSE(GridHit({240, 49}, origin, clip, 144));
}

TEST(MenuMouse, GridFollowsAnimatedScrollAndRejectsCapacityOverflow) {
    const Rect clip{240, 44, 240, 250};
    EXPECT_EQ(GridHit({250, 55}, {240, -50}, clip, 144).cursor, 12);
    EXPECT_EQ(GridHit({250, 49}, {240, 0}, clip, 144).cursor, 0);
    EXPECT_EQ(GridHit({250, 51}, {240, 0}, clip, 144).cursor, 6);
    EXPECT_FALSE(GridHit({450, 260}, {240, 50}, clip, 29));
}

TEST(MenuMouse, CommandsRespectLineEdgesAndWindowBounds) {
    const Rect text{100, 80, 150, 72};
    EXPECT_EQ(CommandHit({110, 80}, text, 3, 24), 0);
    EXPECT_EQ(CommandHit({110, 104}, text, 3, 24), 1);
    EXPECT_EQ(CommandHit({110, 151.9f}, text, 3, 24), 2);
    EXPECT_EQ(CommandHit({110, 152}, text, 3, 24), -1);
    EXPECT_EQ(CommandHit({250, 90}, text, 3, 24), -1);
    EXPECT_EQ(CommandHit({110, 90}, text, 3, 0), -1);
}

TEST(MenuMouse, WheelClampsToRowsAndPreservesFractionalNotches) {
    EXPECT_EQ(ScrollTop(0, 144, 5, 1), 0);
    EXPECT_EQ(ScrollTop(0, 144, 5, -1), 1);
    EXPECT_EQ(ScrollTop(0, 144, 5, -0.1f), 1);
    EXPECT_EQ(ScrollTop(18, 144, 5, -5), 19);
    EXPECT_EQ(ScrollTop(0, 29, 5, -1), 0);
}

TEST(MenuMouse, MotionMappingAccountsForUiScaleAndWindowDpi) {
    EXPECT_FLOAT_EQ(UnitsPerCount(1.875f, 1280, 1280), 1 / 1.875f);
    EXPECT_FLOAT_EQ(UnitsPerCount(3.75f, 2560, 1280), 1 / 1.875f);
    EXPECT_FLOAT_EQ(UnitsPerCount(0.9375f, 1280, 1280), 1 / 0.9375f);
    EXPECT_FLOAT_EQ(UnitsPerCount(0, 0, 0), 1);
}

TEST(MenuMouse, IdleMouseDoesNotTakeControllerSelection) {
    Pointer p;
    EXPECT_FALSE(p.Update({}, {250, 100}, {1, 1}, {0, 0, 512, 480}, false).pointing);
    InputMenuMouse mouse{10, 5, 0, 0};
    EXPECT_TRUE(p.Update(mouse, {250, 100}, {0.5f, 1}, {-85, 0, 682, 480}, false).pointing);
    EXPECT_FLOAT_EQ(p.pos.x, 255);
    EXPECT_FLOAT_EQ(p.pos.y, 105);
    EXPECT_FALSE(p.Update(mouse, {}, {1, 1}, {-85, 0, 682, 480}, true).pointing);
    EXPECT_TRUE(p.Update(mouse, {300, 200}, {1, 1}, {-85, 0, 682, 480}, false).pointing);
    EXPECT_FLOAT_EQ(p.pos.x, 310);
}

TEST(MenuMouse, PointerReachesWidescreenMarginsAndClamps) {
    Pointer p;
    p.Update({-1000, 1000, 0, 0}, {256, 240}, {1, 1}, {-85, 0, 682, 480}, false);
    EXPECT_FLOAT_EQ(p.pos.x, -85);
    EXPECT_FLOAT_EQ(p.pos.y, 480);
}

TEST(MenuMouse, ButtonsHeldOnEntryRequireReleaseBeforeClick) {
    Pointer p;
    p.buttons = 1;
    EXPECT_EQ(p.Update({0, 0, 0, 1}, {}, {1, 1}, {0, 0, 512, 480}, false).pressed, 0u);
    EXPECT_EQ(p.Update({}, {}, {1, 1}, {0, 0, 512, 480}, false).released, 1u);
    EXPECT_EQ(p.Update({0, 0, 0, 1}, {}, {1, 1}, {0, 0, 512, 480}, false).pressed, 1u);
}

namespace {
const Target source{ITEM_MENU_PANEL_BAG, 1}, dest{ITEM_MENU_PANEL_BAG, 8};
const Take   press{true, true, 1, 0, 1}, hold{true, true, 0, 0, 1}, release{true, false, 0, 1, 0};
} // namespace

TEST(MenuMouse, ShortClickOpensCommandOnRelease) {
    Gesture g;
    EXPECT_EQ(g.Update(press, {100, 100}, source, true, false), Action::None);
    EXPECT_EQ(g.Update(hold, {102, 100}, source, true, false), Action::None);
    EXPECT_EQ(g.Update(release, {102, 100}, source, true, false), Action::Click);
    EXPECT_FALSE(g.pending);
}

TEST(MenuMouse, DragPicksUpSourceThenDropsOnRelease) {
    Gesture g;
    g.Update(press, {100, 100}, source, true, false);
    EXPECT_EQ(g.Update(hold, {120, 150}, dest, true, false), Action::PickUp);
    EXPECT_EQ(g.source, source);
    EXPECT_EQ(g.Update(hold, {120, 150}, dest, true, true), Action::None);
    EXPECT_EQ(g.Update(release, {120, 150}, dest, true, true), Action::Drop);
}

TEST(MenuMouse, FastDragWaitsForRetailPickupBeforeRelease) {
    Gesture g;
    g.Update(press, {100, 100}, source, true, false);
    EXPECT_EQ(g.Update(release, {120, 150}, dest, true, false), Action::PickUp);
    EXPECT_EQ(g.Update({true}, {120, 150}, dest, true, true), Action::Drop);
}

TEST(MenuMouse, DropOutsideReturnsItemWithoutDiscard) {
    Gesture g;
    g.Update(press, {100, 100}, source, true, false);
    g.Update(hold, {120, 150}, {}, false, false);
    EXPECT_EQ(g.Update(release, {120, 150}, {}, false, true), Action::Cancel);
}

TEST(MenuMouse, RightClickCancelsPendingAndActiveDrags) {
    Gesture g;
    g.Update(press, {}, source, true, false);
    EXPECT_EQ(g.Update({true, true, 2, 0, 3}, {}, source, true, false), Action::Cancel);
    EXPECT_FALSE(g.pending);
    EXPECT_EQ(g.Update(release, {}, source, true, false), Action::None);
}

TEST(MenuMouse, PadTakeoverCancelsOnlyTheMouseGesture) {
    Gesture g;
    g.Update(press, {}, source, true, false);
    EXPECT_EQ(g.Update({}, {}, dest, true, false), Action::None);
    EXPECT_FALSE(g.pending);
    EXPECT_FALSE(g.dragging);
}

TEST(MenuMouse, CarriedRetailItemCanBePlacedWithClick) {
    Gesture g;
    EXPECT_EQ(g.Update(press, {}, dest, true, true), Action::Drop);
    EXPECT_EQ(g.Update(press, {}, {}, false, true), Action::None);
}

namespace {
class ItemMouse : public testing::Test {
protected:
    CMenuKeyFunc        keys;
    CMenuPosDataManage  forms{};
    CMenuPosDataForm    board{}, character{}, hand{}, command{};
    MENUFORMPARTS_TYPE  parts[7]{};
    MENU_INPUTKEY_ARG   layouts[ITEM_MENU_PANEL_NUM]{};
    CGameDataUsed       items[144]{};
    CHARA_DATA          chara{};
    CDC2Mes             message;
    CMenuItemInfo       saved_menu = class_menu_item_info;
    CMENU_USERPARAM     saved_user = MenuUserParam;
    CMenuKeyFunc       *saved_keys = MenuCommonInfo;
    CMenuPosDataManage *saved_forms = MenuPosData;
    CMenuMoveItem      *saved_move = MenuMoveItemPtr;
    CDC2Mes            *saved_message = MenuDCMsg[5];
    CMenuPosDataForm   *saved_message_form = MenuMesForm[5];
    CGamePad            saved_pad = GamePad__2;
    int                 saved_width = mgScreenWidth, saved_height = mgScreenHeight;
    float               saved_board_x = MenuItemBrdUnderBrdPosXY[0], saved_board_y = MenuItemBrdUnderBrdPosXY[1];

    void SetUp() override {
        MenuMouseClose();
        MenuMouseEnable(false);
        InputSetScriptedDevices({});
        mgEndFrame(nullptr);
        MenuMouseTake();
        MenuMousePadTick();
        InputResetBindings();
        mgScreenWidth = 512;
        mgScreenHeight = 480;
        MenuPosData = &forms;
        MenuCommonInfo = &keys;
        MenuMoveItemPtr = nullptr;
        MenuDCMsg[5] = &message;
        MenuMesForm[5] = &command;
        GamePad__2.key_lock = 0;
        GamePad__2.pad[0].button = GamePad__2.previous_pad[0].button = 0;
        keys.now_mode = MENU_MODE_ITEM;
        keys.cursor = keys.top_line = 0;
        keys.key_arg = &layouts[ITEM_MENU_PANEL_BAG];
        keys.cursor_form = &hand;
        for (auto &layout : layouts) {
            layout.columns = layout.disp_columns = 6;
            layout.disp_lines = 5;
            layout.max = 138;
        }
        auto &menu = class_menu_item_info;
        menu.mode = MENU_ASK_MODE_NONE;
        menu.step = 0;
        menu.sub_menu = menu.next_sub_menu = MENU_ITEM_SUB_MENU_NONE;
        menu.view_mode = MENU_ITEM_VIEW_MAX;
        menu.sub_view = 0;
        menu.key_arg_no = ITEM_MENU_PANEL_BAG;
        menu.item_board_form = &board;
        menu.view_form[0] = &character;
        board.x = 226;
        board.y = 24;
        character.draw_flag = true;
        character.parts = parts;
        character.parts_num = 7;
        char *names[] = {const_cast<char *>("item0"), const_cast<char *>("item1"), const_cast<char *>("item2"),
                         const_cast<char *>("wep0"), const_cast<char *>("wep1"),
                         const_cast<char *>("c_i0"), const_cast<char *>("c_w0")};
        Point positions[] = {
            {50,  58 },
            {91,  58 },
            {133, 58 },
            {30,  400},
            {196, 400},
            {29,  50 },
            {3,   379}
        };
        for (int i = 0; i < 7; ++i) {
            parts[i].name = names[i];
            parts[i].x = positions[i].x;
            parts[i].y = positions[i].y;
            parts[i].dtype = MENUFORMPARTS_DTYPE_FUNCINFO;
        }
        for (int i = 0; i < 3; ++i) {
            menu.item_parts[0][i] = &parts[i];
        }
        menu.wep_parts[0][8] = &parts[3];
        menu.wep_parts[0][14] = &parts[4];
        MenuItemBrdUnderBrdPosXY[0] = 242;
        MenuItemBrdUnderBrdPosXY[1] = 50;
        MenuUserParam.used_data = items;
        MenuUserParam.chara[0] = &chara;
        ClockSetUnbounded(true);
        MenuMouseEnable(true);
        MenuMouseUpdate(keys); // Opening consumes held buttons without activating.
    }

    void TearDown() override {
        MenuMouseClose();
        MenuMouseEnable(false);
        InputSetScriptedDevices({});
        mgEndFrame(nullptr);
        MenuMouseTake();
        MenuMousePadTick();
        InputResetBindings();
        class_menu_item_info = saved_menu;
        MenuUserParam = saved_user;
        MenuCommonInfo = saved_keys;
        MenuPosData = saved_forms;
        MenuMoveItemPtr = saved_move;
        MenuDCMsg[5] = saved_message;
        MenuMesForm[5] = saved_message_form;
        GamePad__2 = saved_pad;
        mgScreenWidth = saved_width;
        mgScreenHeight = saved_height;
        MenuItemBrdUnderBrdPosXY[0] = saved_board_x;
        MenuItemBrdUnderBrdPosXY[1] = saved_board_y;
    }

    void Tick(float dx = 0, float dy = 0, unsigned buttons = 0, float wheel = 0) {
        InputSetScriptedDevices({.mouse_buttons = buttons, .mouse_dx = dx, .mouse_dy = dy, .mouse_wheel = wheel});
        mgEndFrame(nullptr);
        keys.SelDataInit();
    }
};
} // namespace

TEST_F(ItemMouse, HoverUpdatesRetailGridSelectionWithoutPressingAnything) {
    Tick(40, 50);
    EXPECT_EQ(class_menu_item_info.key_arg_no, ITEM_MENU_PANEL_BAG);
    EXPECT_EQ(keys.cursor, 7);
    EXPECT_EQ(keys.select_pos[0], 7);
    EXPECT_EQ(keys.key_arg, &layouts[ITEM_MENU_PANEL_BAG]);
    EXPECT_TRUE(keys.CheckKeyInput());
    EXPECT_EQ(MenuMouseSyntheticDown(), 0);
}

TEST_F(ItemMouse, AllActiveAndEquippedSlotsUseTheirDrawnIconPositions) {
    Tick(-196, -1); // First slot centre (66,74).
    EXPECT_EQ(class_menu_item_info.key_arg_no, ITEM_MENU_PANEL_ACTIVE_ITEMS);
    EXPECT_EQ(keys.cursor, 0);
    Tick(41, 0);
    EXPECT_EQ(keys.cursor, 1);
    Tick(42, 0);
    EXPECT_EQ(keys.cursor, 2);
    Tick(-103, 342);
    EXPECT_EQ(class_menu_item_info.key_arg_no, ITEM_MENU_PANEL_EQUIPMENT);
    EXPECT_EQ(keys.cursor, 0);
    Tick(166, 0);
    EXPECT_EQ(keys.cursor, 1);
    EXPECT_EQ(keys.key_arg, &layouts[ITEM_MENU_PANEL_EQUIPMENT]);
}

TEST_F(ItemMouse, HoverCharacterSetsTheRetailCharacterTarget) {
    Tick(-116, 150);
    EXPECT_EQ(class_menu_item_info.key_arg_no, ITEM_MENU_PANEL_CHARA);
    EXPECT_EQ(keys.cursor, 0);
}

TEST_F(ItemMouse, WheelChangesVisibleRowAndCursorTogether) {
    Tick(1, 0, 0, -5);
    EXPECT_EQ(keys.cursor, 30);
    EXPECT_EQ(keys.top_line, 5);
    EXPECT_TRUE(keys.CheckKeyInput());
    EXPECT_FLOAT_EQ(MenuItemBrdUnderBrdPosXY[1], -200);
    Tick(0, 0, 0, 100);
    EXPECT_EQ(keys.cursor, 0);
    EXPECT_EQ(keys.top_line, 0);
}

TEST_F(ItemMouse, DragEmitsRetailPickupThenCrossAtDestination) {
    items[0].item_no = 1;
    Tick(0, 0, 1);
    EXPECT_EQ(MenuMouseSyntheticDown(), 0);
    Tick(40, 50, 1);
    EXPECT_EQ(keys.cursor, 0); // Pickup still operates on the original item.
    EXPECT_EQ(MenuMouseSyntheticDown(), PAD_SQUARE);
    keys.have_item.item_no = 1; // The retail key step has picked it up.
    Tick(0, 0, 1);
    EXPECT_EQ(keys.cursor, 7); // Even if the mouse stops moving after pickup.
    Tick();
    EXPECT_EQ(keys.cursor, 7);
    EXPECT_EQ(MenuMouseSyntheticDown(), PAD_CROSS);
    EXPECT_TRUE(GamePad__2.Down(PAD_CROSS));
}

TEST_F(ItemMouse, ClickAndBackBecomeRetailButtonEdges) {
    items[0].item_no = 1;
    Tick(0, 0, 1);
    Tick();
    EXPECT_EQ(MenuMouseSyntheticDown(), PAD_CROSS);
    Tick(0, 0, 2);
    EXPECT_EQ(MenuMouseSyntheticDown(), PAD_CIRCLE);
    GamePad__2.key_lock = 1;
    EXPECT_FALSE(GamePad__2.Down(PAD_CIRCLE));
}

TEST_F(ItemMouse, CommandHoverAndClickUseTheDisplayedMessageRows) {
    auto &menu = class_menu_item_info;
    menu.mode = MENU_ASK_MODE_ITEM_COMMAND;
    menu.ask_para.form = &command;
    menu.ask_para.mes_no = 5;
    menu.ask_para.cmd_num = 3;
    command.draw_flag = true;
    message.text_x = 250;
    message.text_y = 100;
    message.text_w = 100;
    message.font_h = 24;
    message.draw_off_x = message.draw_off_y = 0;
    message.window_mode = MES_WIN_FLOATING;
    message.centering = 0;
    message.cursor = 0;
    Tick(0, 24); // Start at the selected command, then move down one row.
    EXPECT_EQ(message.GetMsgCursor(), 1);
    EXPECT_EQ(keys.cursor, 0);
    Tick(0, 0, 1);
    EXPECT_EQ(MenuMouseSyntheticDown(), PAD_CROSS);
    Tick(150, 0, 0);
    Tick(0, 0, 1);
    EXPECT_EQ(MenuMouseSyntheticDown(), 0);
}

TEST_F(ItemMouse, PadInputAndLockedMenusKeepTheirSelection) {
    GamePad__2.pad[0].button = PAD_RIGHT;
    Tick(80, 0, 1);
    EXPECT_EQ(keys.cursor, 0);
    EXPECT_EQ(MenuMouseSyntheticDown(), 0);
    EXPECT_TRUE(GamePad__2.Down(PAD_RIGHT));
    GamePad__2.pad[0].button = 0;
    keys.key_enable = false;
    Tick(80, 0);
    EXPECT_EQ(keys.cursor, 0);
}

TEST_F(ItemMouse, TakingOverAnActiveSlotStartsAtTheRetailHandTip) {
    class_menu_item_info.key_arg_no = ITEM_MENU_PANEL_ACTIVE_ITEMS;
    keys.key_arg = &layouts[ITEM_MENU_PANEL_ACTIVE_ITEMS];
    hand.x = 40;
    hand.y = 60;
    Tick(41, 0);
    EXPECT_EQ(class_menu_item_info.key_arg_no, ITEM_MENU_PANEL_ACTIVE_ITEMS);
    EXPECT_EQ(keys.cursor, 1);
}

TEST_F(ItemMouse, LeavingWhilePressedQuarantinesTheButtonUntilRelease) {
    Tick(1, 0, 1);
    keys.now_mode = MENU_MODE_MAIN_TOWN;
    GamePad__2.MenuModeOff();
    mgEndFrame(nullptr);
    MenuMouseTake();
    MenuMousePadTick();
    InputKeyboardMouse mouse{.mouse_buttons = 1};
    EXPECT_EQ(InputApplyKeyboardMouse({}, mouse).buttons, 0);
    InputSetScriptedDevices({});
    mgEndFrame(nullptr);
    MenuMouseTake();
    MenuMousePadTick();
    EXPECT_NE(InputApplyKeyboardMouse({}, mouse).buttons, 0);
    EXPECT_EQ(MenuMouseSyntheticDown(), 0);
}

TEST_F(ItemMouse, ConfirmationChoicesUseRetailYesNoState) {
    auto &menu = class_menu_item_info;
    menu.mode = MENU_ASK_MODE_TRUSH;
    menu.ask_para.mes_no = 5;
    command.draw_flag = true;
    message.window_mode = MES_WIN_YESNO;
    message.choice_pos[0][0] = 250;
    message.choice_pos[0][1] = 100;
    message.choice_pos[1][0] = 350;
    message.choice_pos[1][1] = 100;
    message.cursor = 1;
    Tick(0, 30);
    EXPECT_EQ(message.GetMsgCursor(), 0);
    EXPECT_EQ(MenuMouseSyntheticDown(), 0);
    Tick(0, 0, 1);
    EXPECT_EQ(MenuMouseSyntheticDown(), PAD_CROSS);
    Tick(100, 0);
    EXPECT_EQ(message.GetMsgCursor(), 1);
    command.draw_flag = false;
    Tick(-100, 0, 1);
    EXPECT_EQ(MenuMouseSyntheticDown(), 0);
}

TEST_F(ItemMouse, QuantityBoardWheelAndArrowsUseRetailDirectionKeys) {
    class_menu_item_info.mode = MENU_ASK_MODE_HOW_MUCH;
    keys.how_much_form = &command;
    command.draw_flag = true;
    command.parts = parts;
    command.parts_num = 2;
    parts[0].name = const_cast<char *>("up");
    parts[0].x = 250;
    parts[0].y = 70;
    parts[1].name = const_cast<char *>("down");
    parts[1].x = 250;
    parts[1].y = 110;
    Tick(1, 0, 0, -1);
    EXPECT_EQ(MenuMouseSyntheticDown(), PAD_DOWN);
    Tick(0, 0, 1);
    EXPECT_EQ(MenuMouseSyntheticDown(), PAD_UP);
    Tick(0, 40);
    Tick(0, 0, 1);
    EXPECT_EQ(MenuMouseSyntheticDown(), PAD_DOWN);
}

TEST_F(ItemMouse, ControllerOnlyItemPageDoesNotMoveTheSharedPointerOrigin) {
    Tick();
    EXPECT_FALSE(MenuMouseTake().pointing);
    EXPECT_FLOAT_EQ(MenuMouseTake().x, 256);
    EXPECT_FLOAT_EQ(MenuMouseTake().y, 240);
    keys.now_mode = MENU_MODE_MAIN_TOWN;
    Tick(10, 0);
    EXPECT_FLOAT_EQ(MenuMouseTake().x, 266);
    EXPECT_FLOAT_EQ(MenuMouseTake().y, 240);
}
