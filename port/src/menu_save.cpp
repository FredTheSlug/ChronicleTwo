#include "menuop.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

#include "dngmenu.hpp"
#include "mainloop.hpp"
#include "mapselect.hpp"
#include "menuaqua.hpp"
#include "menucls1.hpp"
#include "menucommon.hpp"
#include "menumain.hpp"
#include "mg_drawprim.hpp"
#include "mg_math.hpp"
#include "mglib.hpp"
#include "save_slots.hpp"
#include "sound.hpp"

extern mgCMemory SaveMenuStack;

namespace {
CSaveMenuClass *active_menu;
int input_wait;

void Text(CDC2Mes *message, const char *text) {
    // CDC2Mes's string path rebuilds and resets the fade every StepMsg. Build
    // once through ClsMes, then let its -2 direct-message state keep animating.
    message->MakeMsg(-1);
    message->ClsMes::MakeMesWin(const_cast<char *>(text), 1, 1);
    message->msg_change = false;
}

void ShowList(CSaveMenuClass &menu) {
    menu.page = SAVE_MENU_PAGE_FILE_LIST;
    menu.phase = SAVE_LIST_PHASE_SELECT;
    menu.ExeScript(const_cast<char *>("MES2_CNL"));
    MenuMesForm[2]->draw_flag = false;
    menu.list_form->draw_flag = true;
    menu.cursor_form->draw_flag = SaveSlotRows(SaveSlots, menu.mode == SAVE_MENU_MODE_SAVE) > 0;
    menu.scrlbar_form->draw_flag = SaveSlotRows(SaveSlots, menu.mode == SAVE_MENU_MODE_SAVE) > 3;
    SetMenuReturnMsgCtrl(true);
    input_wait = 4;
}

void Notice(CSaveMenuClass &menu, const char *text) {
    menu.phase = SAVE_LIST_PHASE_NOTICE;
    auto *message = MenuDCMsg[2];
    message->MsgPreset(MENU_SCRIPT_MES_DEFAULT, LanguageCode);
    message->SetAbsPos(5);
    Text(message, text);
    message->push_button = 1;
    MenuMesForm[2]->draw_flag = true;
    menu.cursor_form->draw_flag = false;
    input_wait = 4;
}

void Close(CSaveMenuClass &menu) {
    menu.FadeOutMenu(40, 0);
    menu.CBaseMenuClass::mode = MENU_ASK_MODE_CLOSE;
    SaveSlotNew = false;
}

void Position(CSaveMenuClass &menu) {
    const int rows = SaveSlotRows(SaveSlots, menu.mode == SAVE_MENU_MODE_SAVE);
    menu.select = std::clamp(menu.select, 0, std::max(0, rows - 1));
    menu.top = std::clamp(menu.top, std::max(0, menu.select - 2), menu.select);
    menu.top = std::min(menu.top, std::max(0, rows - 3));
    static char actions[3][6] = {"list0", "list1", "list2"};
    if (menu.cursor_form) menu.cursor_form->SetAction(actions[menu.select - menu.top]);
    if (menu.list_form) CalcMenu1(164.0f - 80.0f * menu.top, &menu.list_form->y, 4, 4, menu.list_jump);
    MenuPosData->FormStep();
    // Neither opening, cancelling, nor completing an operation exposes a card.
    for (int i = 0; i < 2; ++i) {
        if (menu.slot_form[i]) menu.slot_form[i]->draw_flag = false;
        if (MenuMesForm[4 + i]) MenuMesForm[4 + i]->draw_flag = false;
    }
    if (MenuMesForm[7]) MenuMesForm[7]->draw_flag = false;
    if (menu.title_form) {
        int x = 0, y = 0;
        MenuDCMsg[3]->StepMsg();
        menu.title_form->GetPutPosXY(const_cast<char *>("msgpos"), x, y);
        MenuDCMsg[3]->SetMovePosCenteringGyou(0, x, y);
        menu.title_form->GetPutPosXY(const_cast<char *>("msgpos_sub0"), x, y);
        MenuDCMsg[6]->StepMsg();
        MenuDCMsg[6]->SetMovePosCenteringGyou(0, x, y);
    }
    int range[2] = {6, 250};
    if (menu.scrlbar_parts[0] && menu.scrlbar_parts[1] && menu.scrlbar_parts[2]) {
        LocalFunc_AdjustScrlBar(menu.scrlbar_parts, menu.scrlbar_pos, range, menu.top,
                               static_cast<float>(std::max(3, rows)), 3, menu.list_jump);
    }
    menu.list_jump = false;
}
} // namespace

PORT_OVERRIDE void CSaveMenuClass::SetDlInfoMsg(int, int) {
    if (MenuMesForm[7]) MenuMesForm[7]->draw_flag = false;
}

PORT_OVERRIDE void CSaveMenuClass::EnvSetSave(int kind) {
    auto *manager = SaveSlotManager();
    SaveSlotNew = kind == SAVE_FILE_NEW;
    manager->file_no = select;
    manager->SetFuncNo(MC_FUNC_SAVE);
    phase = SAVE_LIST_PHASE_SAVING;
    MenuMesForm[2]->draw_flag = false;
    cursor_form->draw_flag = false;
    SetMenuReturnMsgCtrl(false);
}

PORT_OVERRIDE int CSaveMenuClass::KeyStep() {
    active_menu = this;
    auto *manager = SaveSlotManager();
    auto *message = MenuDCMsg[2];
    const bool saving = mode == SAVE_MENU_MODE_SAVE;
    if (first_step) {
        first_step = false;
        slot = 0;
        step = SAVE_MENU_STEP_READY;
        manager->GetAllSaveFileInfo();
        select = std::max(0, manager->GetUpdateFile());
        top = std::max(0, select - 2);
        list_jump = true;
        chapter8_start = saving ? CheckStartChapter8(GetSaveData()) : 0;
        ShowList(*this);
        MenuDCMsg[3]->MakeMsg(saving ? 0xC1D : 0xC1E);
        if (SaveSlots.read_failed) Notice(*this, "Could not read the save folder. Return and try again.");
        else if (!saving && SaveSlots.saves.empty()) Notice(*this, "No saves found.");
    }
    if (CBaseMenuClass::mode == MENU_ASK_MODE_OPEN) {
        if (FadeCheckMenu()) {
            MenuCommonInfo->key_enable = true;
            CBaseMenuClass::mode = MENU_ASK_MODE_NONE;
            if (chapter8_start) {
                ExeScript(const_cast<char *>("CLR_SV_INITMSG"));
                step = SAVE_MENU_STEP_NOTICE;
            }
        }
    } else if (CBaseMenuClass::mode == MENU_ASK_MODE_CLOSE) {
        if (FadeCheckMenu()) {
            manager->FinishForMC();
            if (saving) {
                ResetMapInfo();
                ReStartEnvSoundMenu();
                MenuMainScene->StopBGM(0);
                MenuMainScene->LoadBGM(bgm_status.load_no, SaveMenuStack.stGetTop());
                MenuMainScene->SetActiveBgmStatus(&bgm_status);
                MenuMainScene->StepSnd();
            }
            return 1;
        }
    } else if (step == SAVE_MENU_STEP_NOTICE) {
        if (MenuCommonInfo->CheckPushButton()) {
            ExeScript(const_cast<char *>("CLR_SV_ENDMSG"));
            step = SAVE_MENU_STEP_READY;
            input_wait = 4;
        }
    } else {
        int pushed = ConvertCheckPushButton(MenuCommonInfo->CheckPushButton());
        int keys = MenuCommonInfo->CheckSelectKey();
        int lr = MenuCommonInfo->CheckLRKey();
        input_wait = std::max(0, input_wait - 1);
        switch (phase) {
            case SAVE_LIST_PHASE_SELECT: {
                int move = !!(keys & MENU_SELECT_KEY_DOWN) - !!(keys & MENU_SELECT_KEY_UP);
                if (lr & (MENU_SELECT_KEY_L1 | MENU_SELECT_KEY_L2)) move -= 2;
                else if (lr & (MENU_SELECT_KEY_R1 | MENU_SELECT_KEY_R2)) move += 2;
                int rows = SaveSlotRows(SaveSlots, saving);
                if (rows && MenuKeySelectCheck(move, &select, &top, 0, rows, 3, MENU_CURSOR_LIMIT_CLAMP)) {
                    MenuSePlay(SYSTEM_SE_CURSOR);
                    input_wait = 4;
                }
                if (input_wait) break;
                if (pushed == MENU_PUSH_BUTTON_CANCEL) {
                    if (chapter8_start && save_count == 0) {
                        ExeScript(const_cast<char *>("CLR_SV_CONFIRM"));
                        phase = SAVE_LIST_PHASE_LOAD_NOTICE;
                    } else Close(*this);
                    MenuSePlay(SYSTEM_SE_CANCEL);
                    break;
                }
                if (pushed != MENU_PUSH_BUTTON_DECIDE || rows == 0) break;
                if (SaveSlots.read_failed) {
                    Notice(*this, "Could not read the save folder. Return and try again.");
                    break;
                }
                bool is_new = SaveSlotIsNew(SaveSlots, select, saving);
                const SaveSlot *entry = is_new ? nullptr : &SaveSlots.saves[select];
                if (!saving && entry->state != SaveSlotState::Ready) {
                    Notice(*this, SaveSlotStatus(entry->state));
                    break;
                }
                if (mode == SAVE_MENU_MODE_GYORACE_LOAD && entry->info.fish_num <= 0) {
                    Notice(*this, "There are no fish in this save.");
                    break;
                }
                ExeScript(const_cast<char *>("FILE_OKBTN"));
                message->MsgPreset(MENU_SCRIPT_MES_YESNO, LanguageCode);
                message->SetAbsPos(5);
                if (saving) {
                    save_kind = is_new ? SAVE_FILE_NEW : SAVE_FILE_OVERWRITE;
                    if (is_new) {
                        // No occupied entry can reach this prompt.
                        Text(message, "Create a New Save?");
                    } else {
                        message->SetMsgVolumeNoOne(entry->number + 1);
                        message->MakeMsg(chapter8_start ? 0xC44 : 0xBC4);
                    }
                    phase = SAVE_LIST_PHASE_CONFIRM_SAVE;
                } else {
                    message->SetMsgVolumeNoOne(entry->number + 1);
                    message->MakeMsg(0xBE0);
                    phase = SAVE_LIST_PHASE_CONFIRM_LOAD;
                }
                message->SetMsgCursor(1);
                MenuMesForm[2]->draw_flag = true;
                MenuSePlay(SYSTEM_SE_DECIDE);
                input_wait = 4;
                break;
            }
            case SAVE_LIST_PHASE_CONFIRM_SAVE:
            case SAVE_LIST_PHASE_CONFIRM_LOAD: {
                int before = message->GetMsgCursor();
                int answer = message->YesNoCursor();
                if (before != answer) input_wait = 6;
                if (input_wait) break;
                if (pushed == MENU_PUSH_BUTTON_CANCEL || (pushed == MENU_PUSH_BUTTON_DECIDE && answer != 0)) {
                    ShowList(*this);
                    MenuSePlay(SYSTEM_SE_CANCEL);
                } else if (pushed == MENU_PUSH_BUTTON_DECIDE) {
                    if (phase == SAVE_LIST_PHASE_CONFIRM_SAVE) EnvSetSave(save_kind);
                    else {
                        manager->file_no = select;
                        manager->SetFuncNo(MC_FUNC_LOAD);
                        phase = SAVE_LIST_PHASE_LOADING;
                        MenuMesForm[2]->draw_flag = false;
                        cursor_form->draw_flag = false;
                    }
                    MenuSePlay(SYSTEM_SE_DECIDE);
                }
                break;
            }
            case SAVE_LIST_PHASE_SAVING:
            case SAVE_LIST_PHASE_LOADING: {
                bool writing = phase == SAVE_LIST_PHASE_SAVING;
                if (!manager->Step()) break;
                if (manager->error.code != MC_ERROR_NONE) {
                    Notice(*this, writing ? "Saving failed. Check your save folder and try again." : "Loading failed. The save has been kept.");
                    break;
                }
                message->MsgPreset(MENU_SCRIPT_MES_DEFAULT, LanguageCode);
                message->SetAbsPos(5);
                message->push_button = 1;
                message->MakeMsg(writing ? (chapter8_start ? 0xC45 : 0xBC0) : 0xBE2);
                MenuMesForm[2]->draw_flag = true;
                phase = writing ? SAVE_LIST_PHASE_SAVE_DONE : SAVE_LIST_PHASE_LOAD_DONE;
                if (writing) {
                    ++save_count;
                    ++TreeMapSaveNum;
                    select = manager->file_no;
                }
                input_wait = 4;
                MenuSePlay(SYSTEM_SE_COMPLETE);
                break;
            }
            case SAVE_LIST_PHASE_SAVE_DONE:
            case SAVE_LIST_PHASE_NOTICE:
                if (pushed && !input_wait) ShowList(*this);
                break;
            case SAVE_LIST_PHASE_LOAD_DONE:
                if (pushed && !input_wait) {
                    MenuArg.end_code = MENU_END_LOAD;
                    MenuArg.result[0] = manager->load_program_loop_no;
                    MenuArg.result[1] = manager->load_map_no;
                    MenuArg.result[2] = manager->load_dungeon_no;
                    MenuArg.result[3] = manager->load_floor_id;
                    MenuArg.result[4] = manager->load_dng_tree_flag;
                    Close(*this);
                }
                break;
            case SAVE_LIST_PHASE_LOAD_NOTICE: {
                int answer = message->YesNoCursor2(0);
                if (answer == MES_YESNO_YES) Close(*this);
                else if (answer == MES_YESNO_NO) ShowList(*this);
                break;
            }
        }
    }
    Position(*this);
    return 0;
}

PORT_OVERRIDE void SaveFileListDraw(int &tex_block, float *pos, int alpha) {
    if (!active_menu || alpha <= 0) return;
    auto &menu = *active_menu;
    auto *texture = mgTexManager.GetTexture(const_cast<char *>("save"), -1);
    if (!texture) return;
    const int count = SaveSlotRows(SaveSlots, menu.mode == SAVE_MENU_MODE_SAVE);
    // Draw only the visible window of the catalog, including one row during a
    // scroll. Neither memory nor submitted geometry grows with the save count.
    int first = std::max(0, menu.top - 1), last = std::min(count, menu.top + 4);
    mgRect<int> panel;
    panel.Set(0, 0x42, LanguageCode > 0 ? 0x1CA : 0x17E, 0x4A);
    SetMenuScissor({0, 150, mgScreenWidth - 1, 404});
    MenuReloadTexture(tex_block, menu.tex_block[1]);
    auto *prim = GetMenuPrim();
    SetSpriteEnv(prim, MENU_SPRITE_ENV_TEXTURE);
    prim->Begin(MG_PRIM_SPRITE);
    prim->Texture(texture);
    for (int row = first; row < last; ++row) {
        float y = pos[1] + 80.0f * row;
        prim->Color(0, 0, 0, alpha / 3);
        PrimQuad(prim, pos[0] + 3, y + 3, panel);
        prim->Color(128, 128, 128, alpha);
        PrimQuad(prim, pos[0], y, panel);
    }
    prim->End();
    MenuReloadTexture(tex_block, MenuArg.mes_tex_block);
    CMenuFont font;
    static std::array<CDC2Mes, 5> chapter_messages;
    font.SetClearance(14, 20);
    font.SetColor(128, 128, 128, alpha);
    for (int row = first; row < last; ++row) {
        int x = static_cast<int>(pos[0]) + 18, y = static_cast<int>(pos[1] + 80.0f * row);
        if (y + 18 < 100 || y + 18 > mgScreenHeight) continue;
        if (SaveSlotIsNew(SaveSlots, row, menu.mode == SAVE_MENU_MODE_SAVE)) {
            font.DrawDirect(const_cast<char *>("New Save"), x, y + 24);
            continue;
        }
        const auto &slot = SaveSlots.saves[row];
        char label[96];
        std::snprintf(label, sizeof(label), "File %d%s", slot.number + 1, slot.second_card ? " (import 2)" : "");
        font.DrawDirect(label, x, y + 12);
        if (slot.state != SaveSlotState::Ready) {
            font.DrawDirect(const_cast<char *>(SaveSlotStatus(slot.state)), x, y + 38);
            continue;
        }
        const auto &info = slot.info;
        unsigned long long minutes = std::min(info.play_time / (50 * 60), 999ULL * 60 + 59);
        std::snprintf(label, sizeof(label), "%3llu:%02llu", minutes / 60, minutes % 60);
        font.DrawDirect(label, x, y + 40);
        int map = info.map_no;
        static const int dungeon_maps[] = {55, 57, 58, 64, 79, 97, 160};
        if (info.program_loop_no == LOOP_DUNGEON && info.dungeon_no >= 0 && info.dungeon_no < 7) map = dungeon_maps[info.dungeon_no];
        if (map == 41) map = 11;
        char *title = map >= 0 && map < 256 ? GetMapTitle(map) : nullptr;
        if (title) font.DrawDirect(title, x + 135, y + 12);
        int chapter = info.progress == 100 ? 11 : info.progress >= 4 ? info.progress - 2 : info.progress >= 2 ? 1 : 0;
        auto &chapter_message = chapter_messages[row - first];
        auto *messages = GetMenuMainMessageBuffer();
        chapter_message.SetMessData(messages, messages);
        chapter_message.MsgPreset(0x10);
        chapter_message.MakeMsg(0xC7F + std::clamp(chapter, 0, 11));
        chapter_message.line_pos[0][0] = x + 135;
        chapter_message.line_pos[0][1] = y + 40;
        chapter_message.line_pos_on[0] = true;
        chapter_message.SetMsgAlpha(alpha);
        chapter_message.StepMsg();
        chapter_message.DrawMsg();
    }
    ResetMenuScissor();
}
