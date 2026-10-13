#include "common.h"
#include "menu_mouse.hpp"
#include "nd_meswin.hpp"

#include <algorithm>
#include <vector>

PORT_OVERRIDE void ClsMes::StepSelectCursor(int steps) {
    if (select < 0) { cursor_time = 0; return; }
    SetGoalCursorXY();
    if (cursor_time <= 0) {
        cursor_x = goal_cursor_x;
        cursor_y = goal_cursor_y;
        cursor_time = 1;
    } else {
        for (int i = 0; i < steps; ++i) {
            cursor_x = (cursor_x + goal_cursor_x) / 2;
            cursor_y = (cursor_y + goal_cursor_y) / 2;
            ++cursor_time;
        }
    }
    if (fade < 1 || alpha <= 0 || !buff || font_h <= 0) return;
    std::vector<MenuMouseRect> choices;
    if (window_mode == MES_WIN_YESNO) {
        for (int i = 0; i < 2; ++i) {
            const auto &choice = choice_pos[i];
            const int spacing = choice_pos[1 - i][0] - choice[0];
            const float width = float(spacing > 0 ? std::min(72, spacing) : 72);
            choices.push_back(choice[0] >= 0 && choice[1] >= 0 ?
                MenuMouseRect{float(choice[0] - 4), float(choice[1] - 4), width, 32} : MenuMouseRect{});
        }
    } else {
        const int count = std::clamp((text_h + font_h - 1) / font_h, 0, int(MES_LINE_MAX));
        int dx = 0, dy = 0;
        CalcCenteringXY(&dx, &dy);
        for (int i = 0; i < count; ++i) {
            const float x = line_pos_on[i] ? float(line_pos[i][0]) : text_x + dx + draw_off_x;
            const float y = line_pos_on[i] ? float(line_pos[i][1]) : text_y + dy + draw_off_y + font_h * i;
            choices.push_back(i >= select_top ? MenuMouseRect{x, y, float(std::max(text_w, 60)), float(font_h)} : MenuMouseRect{});
        }
    }
    if (select < static_cast<int>(choices.size()))
        MenuMouseNoteChoices(this, mes_no, choices, select, window_mode == MES_WIN_YESNO);
}
