#include "common.h"
#include "menu_mouse.hpp"

#include <algorithm>
#include <vector>

#include "gamepad.hpp"
#include "mainloop.hpp"
#include "menucls1.hpp"
#include "menucommon.hpp"
#include "menudraw.hpp"
#include "menumain.hpp"
#include "menuop.hpp"
#include "menusys.hpp"
#include "mglib.hpp"
#include "title.hpp"

extern CItemSelect *MouseItemSelect asm("MenuMouse_ItemSelect");
extern CMenuInter *MouseInter asm("MenuMouse_Inter");
extern CSaveMenuClass *MouseSave asm("MenuMouse_Save");
extern TITLE_INFO *MouseTitle asm("MenuMouse_Title");
extern short MouseTitlePhase asm("MenuMouse_TitlePhase");
extern unsigned char MouseTitleHDD asm("MenuMouse_TitleHDD");

bool MenuMouseFloor();

namespace {
void RootMouse() {
    if (!MouseInter || !MenuPosData || MouseInter->step != MENU_INTER_STEP_SELECT || MouseInter->next_mode >= 0) return;
    std::vector<MenuMouseRect> rows;
    for (int i = 0; i < MouseInter->select_num; ++i) {
        auto *form = MenuPosData->GetFormInfo(GetMenuMainIconChar(MouseInter->mode_list[i]));
        rows.push_back(form ? MenuMouseRect{form->x - 20, form->y - 4, 190, 30} : MenuMouseRect{});
    }
    const int selected = MenuMouseRows(rows, MouseInter->select_no);
    if (selected != MouseInter->select_no) MenuMouseMove(&MouseInter->select_no, selected);
}

void SaveMouse() {
    if (!MouseSave || MouseSave->CBaseMenuClass::mode != MENU_ASK_MODE_NONE) return;
    auto &save = *MouseSave;
    if (save.page == SAVE_MENU_PAGE_SLOT_SELECT && save.step == SAVE_MENU_STEP_READY) {
        MenuMouseRect rows[2]{};
        for (int i = 0; i < 2; ++i) if (auto *form = save.slot_form[i])
            rows[i] = {form->x, form->y, 360, 64};
        const int selected = MenuMouseRows(rows, save.slot);
        if (selected != save.slot) { save.slot = selected; MenuSePlay(SYSTEM_SE_CURSOR); }
    } else if (save.page == SAVE_MENU_PAGE_FILE_LIST && save.phase == SAVE_LIST_PHASE_SELECT && save.list_form) {
        MenuMouseRect rows[13]{};
        for (int i = save.top; i < std::min(save.top + 3, 13); ++i)
            rows[i] = {save.list_form->x, save.list_form->y + 80 * i, 458, 76};
        const int selected = MenuMouseRows(rows, save.select);
        MenuMouseDelayConfirm(&save, save.select, selected, 4);
        if (selected != save.select) MenuMouseMove(&save.select, selected);
    }
}

void ItemPopupMouse() {
    if (!MouseItemSelect || MouseItemSelect->mode != MENU_ASK_MODE_NONE) return;
    auto &items = *MouseItemSelect;
    std::vector<MenuMouseRect> cells(items.item_num);
    for (int i = 0; i < items.item_num; ++i) {
        int row = i / ITEM_SELECT_COLUMNS;
        if (row >= items.top_line && row < items.top_line + 2)
            cells[i] = {items.item_rect.left + 44 * (i % ITEM_SELECT_COLUMNS), items.item_rect.top + 55 * row, 44, 55};
    }
    int selected = MenuMouseRows(cells, items.cursor, ITEM_SELECT_COLUMNS);
    if (selected != items.cursor) {
        items.cursor = selected;
        MenuCheckLine(&items.top_line, selected / ITEM_SELECT_COLUMNS, 2);
        MenuSePlay(SYSTEM_SE_CURSOR);
    }
}

void TitleMouse() {
    if (GetNowLoopNo() != LOOP_TITLE || !MouseTitle || MouseTitle->mode != TITLE_MODE_TITLE) return;
    if (MouseTitlePhase == TITLE_PHASE_MENU) {
        const int count = MouseTitleHDD ? 5 : 4;
        MenuMouseRect rows[5]{};
        for (int i = 0; i < count; ++i)
            rows[i] = {(mgScreenWidth - 210) * 0.5f, float((MouseTitleHDD ? 76 : 100) + 58 * i), 210, 54};
        const int selected = MenuMouseRows({rows, static_cast<std::size_t>(count)}, MouseTitle->select);
        if (selected != MouseTitle->select) {
            MouseTitle->select = selected;
            MouseTitle->idle_count = 0;
            MenuSePlay(SYSTEM_SE_CURSOR);
        }
    } else if (MouseTitlePhase <= TITLE_PHASE_PUSH_START) {
        if (MenuMouseTake().clicked & 1) MenuMousePress(PAD_START);
    }
}
}

PORT_OVERRIDE int CMenuKeyFunc::CheckSelectKey() {
    if (key_enable) {
        if (now_mode == MENU_MODE_MAIN_TOWN || now_mode == MENU_MODE_MAIN_DUNGEON) RootMouse();
        if (now_mode == MENU_MODE_SAVE || now_mode == MENU_MODE_TITLE_SAVE) SaveMouse();
        if (now_mode == MENU_MODE_DNG_TREE_MAP && !MenuMouseFloor()) SaveMouse();
        if (now_mode == MENU_MODE_ITEM_SELECT) ItemPopupMouse();
    }
    if (GamePad__2.Down(PAD_UP)) select_key |= MENU_SELECT_KEY_UP;
    else if (GamePad__2.Down(PAD_DOWN)) select_key |= MENU_SELECT_KEY_DOWN;
    if (GamePad__2.Down(PAD_LEFT)) select_key |= MENU_SELECT_KEY_LEFT;
    else if (GamePad__2.Down(PAD_RIGHT)) select_key |= MENU_SELECT_KEY_RIGHT;
    if (!key_enable) select_key = 0;
    return select_key;
}

PORT_OVERRIDE int MenuCheckPushButton() {
    TitleMouse();
    const int confirm = LanguageCode > 0 ? PAD_CROSS : PAD_CIRCLE;
    const int cancel = LanguageCode > 0 ? PAD_CIRCLE : PAD_CROSS;
    // Preserve retail priority when several physical buttons are pressed.
    if (GamePad__2.Down(PAD_CIRCLE)) return PAD_CIRCLE == confirm ? MENU_PUSH_BUTTON_DECIDE : MENU_PUSH_BUTTON_CANCEL;
    if (GamePad__2.Down(PAD_CROSS)) return PAD_CROSS == cancel ? MENU_PUSH_BUTTON_CANCEL : MENU_PUSH_BUTTON_DECIDE;
    constexpr int pads[] = {PAD_TRIANGLE, PAD_SQUARE, PAD_SELECT, PAD_START, PAD_L3, PAD_R3};
    constexpr int actions[] = {MENU_PUSH_BUTTON_TRIANGLE, MENU_PUSH_BUTTON_SQUARE, MENU_PUSH_BUTTON_SELECT,
                               MENU_PUSH_BUTTON_START, MENU_PUSH_BUTTON_L3, MENU_PUSH_BUTTON_R3};
    for (int i = 0; i < 6; ++i) if (GamePad__2.Down(pads[i])) return actions[i];
    return 0;
}
