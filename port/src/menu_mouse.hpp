#pragma once

#include <cstdint>
#include <span>

struct MenuMouseRect {
    float x, y, w, h;
    bool Contains(float px, float py) const;
};

struct MenuMouseSample {
    float x = 256, y = 224, wheel = 0;
    bool pointing = false, moved = false;
    std::uint32_t clicked = 0;
};

// Relative window pixels map through the same logical extent as the menu art.
float MenuMouseUnits(float scale, float pixels, float window);
int MenuMouseHit(std::span<const MenuMouseRect> rows, float x, float y);
int MenuMouseChoose(const MenuMouseSample &mouse, std::span<const MenuMouseRect> rows, int selected, int wheel_step = 1);

void MenuMouseEnable(bool enabled);
const MenuMouseSample &MenuMouseTake();
int MenuMouseButtons();
void MenuMousePress(int buttons);
void MenuMouseMove(int *cursor, int selected);
// Drawn choice geometry is copied, never a retained pointer to game storage.
void MenuMouseNoteChoices(const void *owner, int message, std::span<const MenuMouseRect> rows,
                         int selected, bool horizontal);
// Claims this frame's clicks. A left click outside every row cannot confirm.
int MenuMouseRows(std::span<const MenuMouseRect> rows, int selected, int wheel_step = 1);
// Keep a click through a retail list's cursor-settling delay.
void MenuMouseDelayConfirm(const void *owner, int selected, int target, int ticks);
void MenuMouseDraw();
