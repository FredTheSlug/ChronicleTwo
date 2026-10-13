#include "menu_mouse_item.hpp"

#include <algorithm>
#include <cmath>
#include <SDL3/SDL.h>

#include "gamepad.hpp"
#include "gfx/gfx.hpp"
#include "mainloop.hpp"
#include "menuaqua.hpp"
#include "menucls1.hpp"
#include "menuchr.hpp"
#include "menucommon.hpp"
#include "menudraw.hpp"
#include "menumain.hpp"
#include "menusys.hpp"
#include "mglib.hpp"
#include "mglib_port.hpp"
#include "platform/input.hpp"
#include "platform/window.hpp"
#include "snd_mngr.hpp"

namespace menumouse {
bool Rect::Contains(Point p) const {
    return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
}

Target GridHit(Point p, Point origin, Rect clip, int count) {
    if (!clip.Contains(p) || p.x < origin.x || p.x >= origin.x + 240 || p.y < origin.y) return {};
    const int column = static_cast<int>((p.x - origin.x) / 40);
    const int row = static_cast<int>((p.y - origin.y) / 50);
    const int index = row * 6 + column;
    return index < count ? Target{ITEM_MENU_PANEL_BAG, index} : Target{};
}

int CommandHit(Point p, Rect text, int count, float line_height) {
    if (line_height <= 0 || !text.Contains(p)) return -1;
    int row = static_cast<int>((p.y - text.y) / line_height);
    return row < count ? row : -1;
}

int ScrollTop(int top, int count, int visible_rows, float wheel) {
    if (!std::isfinite(wheel) || wheel == 0) return top;
    int step = static_cast<int>(std::clamp(std::ceil(std::abs(wheel)), 1.0f, 24.0f));
    return std::clamp(top + (wheel > 0 ? -step : step), 0, std::max(0, (count + 5) / 6 - visible_rows));
}

float UnitsPerCount(float scale, float target_pixels, float window_pixels) {
    return scale > 0 && target_pixels > 0 && window_pixels > 0 ? target_pixels / window_pixels / scale : 1;
}

Take Pointer::Update(const InputMenuMouse &mouse, Point start, Point units, Rect bounds, bool pad) {
    Take take;
    take.pressed = mouse.buttons & ~buttons;
    take.released = buttons & ~mouse.buttons;
    take.held = mouse.buttons;
    take.wheel = mouse.wheel;
    buttons = mouse.buttons;
    take.moved = mouse.dx != 0 || mouse.dy != 0 || take.pressed != 0 || mouse.wheel != 0;
    if (pad) {
        pointing = false;
        return {};
    }
    if (take.moved) {
        if (!pointing) pos = start;
        pointing = true;
        pos.x = std::clamp(pos.x + mouse.dx * units.x, bounds.x, bounds.x + bounds.w);
        pos.y = std::clamp(pos.y + mouse.dy * units.y, bounds.y, bounds.y + bounds.h);
    }
    take.pointing = pointing;
    return take;
}

void Gesture::Reset() { *this = {}; }

Action Gesture::Update(const Take &take, Point p, Target hit, bool can_pick_up, bool holding) {
    if (!take.pointing) { Reset(); return Action::None; }
    if (take.pressed & 2) { Reset(); return Action::Cancel; }
    if (release_pending) {
        Reset();
        return holding ? (hit ? Action::Drop : Action::Cancel) : Action::None;
    }
    if (take.pressed & 1) {
        if (holding) return hit ? Action::Drop : Action::None;
        if (hit && can_pick_up) {
            pending = true;
            source = hit;
            origin = p;
        } else {
            return hit ? Action::Click : Action::None;
        }
    }
    if (pending) {
        const float dx = p.x - origin.x, dy = p.y - origin.y;
        if (dx * dx + dy * dy >= 16) {
            pending = false;
            dragging = true;
            release_pending = (take.released & 1) != 0;
            return Action::PickUp;
        }
        if (take.released & 1) {
            const bool click = hit == source;
            Reset();
            return click ? Action::Click : Action::None;
        }
    }
    if (dragging && (take.released & 1)) {
        Reset();
        return holding ? (hit ? Action::Drop : Action::Cancel) : Action::None;
    }
    return Action::None;
}
} // namespace menumouse

namespace {
using namespace menumouse;
Pointer pointer;
Gesture gesture;
bool menu_open = false, finish_drop = false;
bool wheel_jump = false;
int saved_scroll_manner = MENU_ITEM_BRD_CALC_EASE;
int synthetic = 0, bag_top = 0;
CMenuKeyFunc *owner = nullptr;
int view = -1;

bool ItemPage() {
    return MenuPosData && MenuCommonInfo && MenuCommonInfo->now_mode == MENU_MODE_ITEM &&
           class_menu_item_info.sub_menu == MENU_ITEM_SUB_MENU_NONE &&
           class_menu_item_info.next_sub_menu == MENU_ITEM_SUB_MENU_NONE;
}

bool PadActive() {
    // These are the real pad/keyboard values: synthetic presses are independent.
    return GamePad__2.pad[0].button != 0;
}

bool Unfocused() {
    WindowMode mode;
    // WindowCurrentMode is false for headless replays and tests without a window.
    return WindowCurrentMode(mode) && !(SDL_GetWindowFlags(WindowHandle()) & SDL_WINDOW_INPUT_FOCUS);
}

Point FormPoint(CMenuPosDataForm *form, const char *part = nullptr) {
    Point p;
    form->GetPutPosXY(const_cast<char *>(part), p.x, p.y);
    return p;
}

Rect BoardClip(CMenuItemInfo &menu) {
    Point p = FormPoint(menu.item_board_form);
    return {p.x + 16, p.y + 20, 240, 250};
}

Target Hit(CMenuItemInfo &menu, Point p, int count) {
    Target target = GridHit(p, {MenuItemBrdUnderBrdPosXY[0], MenuItemBrdUnderBrdPosXY[1]}, BoardClip(menu), count);
    if (target) return target;
    if (menu.view_mode != MENU_ITEM_VIEW_MAX && menu.view_mode != MENU_ITEM_VIEW_MONICA) return {};
    auto *form = menu.view_form[menu.sub_view];
    if (!form || !form->draw_flag) return {};
    // The icon parts are the artwork's upper left. c_i/c_w instead place the
    // much larger, animated retail selection frame.
    for (int i = 0; i < 3; ++i) {
        auto *icon = menu.item_parts[menu.sub_view][i];
        if (!icon) continue;
        Point q = FormPoint(form, icon->name);
        if (Rect{q.x - 4, q.y - 4, 40, 40}.Contains(p)) return {ITEM_MENU_PANEL_ACTIVE_ITEMS, i};
    }
    for (int i = 0; i < 2; ++i) {
        auto *icon = menu.wep_parts[menu.sub_view][8 + i * 6];
        if (!icon) continue;
        Point q = FormPoint(form, icon->name);
        if (Rect{q.x - 4, q.y - 4, 40, 40}.Contains(p)) return {ITEM_MENU_PANEL_EQUIPMENT, i};
    }
    // The model occupies the space between the active items and the weapons.
    Point active = FormPoint(form, "c_i0"), weapon = FormPoint(form, "c_w0");
    if (Rect{active.x + 20, active.y + 48, 180, weapon.y - active.y - 62}.Contains(p))
        return {ITEM_MENU_PANEL_CHARA, 0};
    return {};
}

void Select(CMenuItemInfo &menu, CMenuKeyFunc &keys, Target target, MENU_INPUTKEY_ARG *layouts) {
    if (!target || (menu.key_arg_no == target.panel && keys.cursor == target.cursor)) return;
    if (menu.key_arg_no == ITEM_MENU_PANEL_BAG) bag_top = keys.top_line;
    menu.key_arg_no = target.panel;
    keys.key_arg = &layouts[target.panel];
    keys.cursor = target.cursor;
    keys.top_line = target.panel == ITEM_MENU_PANEL_BAG ? bag_top : 0;
    keys.key_input = true;
}

Point Start(CMenuItemInfo &menu, CMenuKeyFunc &keys) {
    if (menu.mode == MENU_ASK_MODE_ITEM_COMMAND && menu.ask_para.mes_no >= 0 && menu.ask_para.mes_no < 9) {
        auto *message = MenuDCMsg[menu.ask_para.mes_no];
        if (message && message->GetMsgCursor() >= 0) {
            int dx = 0, dy = 0;
            message->CalcCenteringXY(&dx, &dy);
            return {message->text_x + message->draw_off_x + dx + message->text_w * 0.5f,
                    message->text_y + message->draw_off_y + dy + (message->GetMsgCursor() + 0.5f) * message->font_h};
        }
    }
    if (menu.key_arg_no == ITEM_MENU_PANEL_BAG) {
        return {MenuItemBrdUnderBrdPosXY[0] + (keys.cursor % 6) * 40 + 20,
                MenuItemBrdUnderBrdPosXY[1] + (keys.cursor / 6) * 50 + 25};
    }
    Point p{256, 240};
    if (keys.cursor_form) {
        p = FormPoint(keys.cursor_form, "cursor0");
        p.x += 26;
        p.y += 14;
    }
    return p;
}

Take TakePointer(Point) {
    const auto &sample = MenuMouseTake();
    pointer.pos = {sample.x, sample.y};
    pointer.pointing = sample.pointing;
    return {sample.pointing, sample.moved || sample.clicked != 0 || sample.wheel != 0,
            sample.clicked, sample.released, sample.held, sample.wheel};
}

void Commands(CMenuItemInfo &menu, const Take &take) {
    gesture.Reset();
    if (take.pressed & 2) { synthetic = PAD_CIRCLE; return; }
    if (!take.pointing || menu.step != 0 || !menu.ask_para.form) return;
    auto *message = MenuDCMsg[menu.ask_para.mes_no];
    if (!message || !menu.ask_para.form->draw_flag) return;
    int dx = 0, dy = 0;
    message->CalcCenteringXY(&dx, &dy);
    Rect rect{message->text_x + message->draw_off_x + dx,
              message->text_y + message->draw_off_y + dy,
              static_cast<float>(message->text_w), static_cast<float>(menu.ask_para.cmd_num * message->font_h)};
    int row = CommandHit(pointer.pos, rect, menu.ask_para.cmd_num, message->font_h);
    if (row >= 0 && take.moved && row != message->GetMsgCursor()) {
        message->SetMsgCursor(row);
        MenuSePlay(SYSTEM_SE_CURSOR);
    }
    if (row >= 0 && (take.pressed & 1)) synthetic = PAD_CROSS;
}

void Question(CMenuItemInfo &menu, CMenuKeyFunc &keys, const Take &take) {
    gesture.Reset();
    if (take.pressed & 2) { synthetic = PAD_CIRCLE; return; }
    if (!take.pointing) return;
    auto *quantity = keys.how_much_form;
    if (quantity && quantity->draw_flag) {
        // The quantity board repositions its parts above or below the hand.
        // Use those parts rather than assuming which side has room.
        Point up = FormPoint(quantity, "up"), down = FormPoint(quantity, "down");
        Rect area{std::min(up.x, down.x) - 12, std::min(up.y, down.y) - 12,
                  std::abs(up.x - down.x) + 48, std::abs(up.y - down.y) + 48};
        if (area.Contains(pointer.pos)) {
            if (take.wheel != 0) synthetic = take.wheel > 0 ? PAD_UP : PAD_DOWN;
            else if (take.pressed & 1) {
                if (Rect{up.x - 6, up.y - 6, 32, 24}.Contains(pointer.pos)) synthetic = PAD_UP;
                else if (Rect{down.x - 6, down.y - 6, 32, 24}.Contains(pointer.pos)) synthetic = PAD_DOWN;
                else synthetic = PAD_CROSS;
            }
        }
        return;
    }
    const int id = menu.ask_para.mes_no;
    if (id < 0 || id >= 9) return;
    auto *message = MenuDCMsg[id];
    auto *form = MenuMesForm[id];
    if (!message || !form || !form->draw_flag || message->window_mode != MES_WIN_YESNO) return;
    for (int choice = 0; choice < 2; ++choice) {
        // These are the same absolute positions DrawYesNo and the retail hand use.
        Point p{static_cast<float>(message->choice_pos[choice][0]), static_cast<float>(message->choice_pos[choice][1])};
        if (p.x < 0 || p.y < 0 || !Rect{p.x - 8, p.y - 4, 56, 28}.Contains(pointer.pos)) continue;
        if (take.moved && message->GetMsgCursor() != choice) {
            message->SetMsgCursor(choice);
            MenuSePlay(SYSTEM_SE_CURSOR);
        }
        if (take.pressed & 1) synthetic = PAD_CROSS;
        break;
    }
}
} // namespace

void MenuMouseClose() {
    synthetic = 0;
    pointer.pointing = false;
    gesture.Reset();
    finish_drop = false;
    menu_open = false;
    owner = nullptr;
}

void MenuMousePadTick() {
    synthetic = 0;
    if (wheel_jump) {
        MenuItemBrdCalcManner = saved_scroll_manner;
        wheel_jump = false;
    }
    if (menu_open && !ItemPage()) MenuMouseClose();

}

int MenuMouseSyntheticDown() { return synthetic; }

bool MenuMouseItemActive() { return ItemPage(); }
void MenuMouseItemOrigin(float &x, float &y) {
    if (!ItemPage()) return;
    const Point start = Start(class_menu_item_info, *MenuCommonInfo);
    x = start.x;
    y = start.y;
}

void MenuMouseUpdate(CMenuKeyFunc &keys) {
    synthetic = 0;
    // Commit even an empty result: an inventory press may be a pending drag.
    struct CommitButtons { ~CommitButtons() { MenuMouseSetButtons(synthetic); } } commit;
    if (!ItemPage() || &keys != MenuCommonInfo) { MenuMouseClose(); return; }
    auto &menu = class_menu_item_info;
    if (!menu_open || owner != &keys) {
        pointer = {};
        gesture.Reset();
        menu_open = true;
        owner = &keys;
        view = menu.view_mode;
        bag_top = keys.top_line;
        return;
    }
    Take take = TakePointer(Start(menu, keys));
    if (view != menu.view_mode || PadActive()) {
        gesture.Reset();
        view = menu.view_mode;
    }
    if (!take.pointing || !keys.key_enable || GamePad__2.key_lock || menu_debug_flag ||
        menu.mode == MENU_ASK_MODE_OPEN || menu.mode == MENU_ASK_MODE_CLOSE ||
        (MenuMoveItemPtr && MenuMoveItemPtr->move_on) || MenuLoadFileCheck(MenuCharaBuild2)) { gesture.Reset(); return; }
    if (finish_drop) {
        finish_drop = false;
        if (menu.mode == MENU_ASK_MODE_NONE && keys.have_item.item_no > 0) {
            // A swap leaves the destination item on the cursor. Retail's return
            // operation puts it in the original slot, or retains it if refused.
            menu.IsCancelNoneLoadItem();
        }
    }
    if (menu.mode == MENU_ASK_MODE_ITEM_COMMAND) { Commands(menu, take); return; }
    if (menu.mode != MENU_ASK_MODE_NONE) {
        Question(menu, keys, take);
        return;
    }
    if (menu.key_arg_no < 0 || menu.key_arg_no >= ITEM_MENU_PANEL_NUM || !keys.key_arg || !menu.item_board_form) return;
    // key_arg always points into this retail table while the item page is idle.
    auto *layouts = keys.key_arg - menu.key_arg_no;
    int count = layouts[ITEM_MENU_PANEL_BAG].max;
    if (menu.key_arg_no == ITEM_MENU_PANEL_BAG) bag_top = keys.top_line;
    if (take.wheel && BoardClip(menu).Contains(pointer.pos)) {
        int top = ScrollTop(bag_top, count, layouts[ITEM_MENU_PANEL_BAG].disp_lines, take.wheel);
        if (top != bag_top) {
            bag_top = top;
            Select(menu, keys, {ITEM_MENU_PANEL_BAG, std::clamp(keys.cursor, top * 6, std::min(count - 1, (top + 5) * 6 - 1))}, layouts);
            keys.top_line = top;
            keys.key_input = true;
            // Wheel steps are discrete; match hit-testing and drawing this tick.
            saved_scroll_manner = MenuItemBrdCalcManner;
            wheel_jump = true;
            MenuItemBrdCalcManner = MENU_ITEM_BRD_CALC_JUMP;
            Point p = FormPoint(menu.item_board_form);
            MenuItemBrdUnderBrdPosXY[1] = p.y + 26 - top * 50;
        }
    }
    Target hit = Hit(menu, pointer.pos, count);
    if (hit && (take.moved || gesture.dragging)) Select(menu, keys, hit, layouts);
    bool can_pick_up = false;
    if (hit && (hit.panel == ITEM_MENU_PANEL_BAG || hit.panel == ITEM_MENU_PANEL_ACTIVE_ITEMS)) {
        auto *item = hit.panel == ITEM_MENU_PANEL_BAG ? &MenuUserParam.used_data[hit.cursor] :
                     &MenuUserParam.chara[menu.sub_view]->active_item[hit.cursor];
        can_pick_up = item->item_no > 0;
    }
    Action action = gesture.Update(take, pointer.pos, hit, can_pick_up, keys.have_item.item_no > 0);
    switch (action) {
        case Action::PickUp:
            Select(menu, keys, gesture.source, layouts);
            synthetic = PAD_SQUARE;
            break;
        case Action::Drop:
            Select(menu, keys, hit, layouts);
            synthetic = PAD_CROSS;
            finish_drop = true;
            break;
        case Action::Click: synthetic = PAD_CROSS; break;
        case Action::Cancel: synthetic = PAD_CIRCLE; break;
        case Action::None: break;
    }
}

bool MenuMouseDrawPosition(const CMenuPosDataForm *form, int &x, int &y) {
    if (!menu_open || !MenuMouseActive() || !pointer.pointing || !ItemPage() || !owner || form != owner->cursor_form) return false;
    // Keep the retail cell frame at the selected target; move the hand and its
    // carried icon together. The form's cursor0 part locates the hand's base.
    Point hand = FormPoint(owner->cursor_form, "cursor0");
    x += static_cast<int>(pointer.pos.x - hand.x - 26);
    y += static_cast<int>(pointer.pos.y - hand.y - 14);
    return true;
}

PORT_OVERRIDE void CMenuKeyFunc::SelDataInit() {
    select_key = 0;
    push_button = 0;
    save_cursor = cursor;
    save_top_line = top_line;
    key_input = false;
    if (MenuMouseItemActive()) MenuMouseUpdate(*this);
}
