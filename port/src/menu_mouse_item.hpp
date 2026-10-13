#pragma once

#include <cstdint>
#include "menu_mouse.hpp"

class CMenuKeyFunc;
class CMenuPosDataForm;
struct InputMenuMouse;

namespace menumouse {

struct Point { float x = 0, y = 0; };
struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool Contains(Point p) const;
};
struct Target {
    int panel = -1, cursor = -1;
    bool operator==(const Target &) const = default;
    explicit operator bool() const { return panel >= 0 && cursor >= 0; }
};

// Grid icons are 40 by 50 logical units. The origin follows the animated board;
// clip is its visible aperture, so rows sliding out cannot be selected.
Target GridHit(Point p, Point origin, Rect clip, int count);
int CommandHit(Point p, Rect text, int count, float line_height);
int ScrollTop(int top, int count, int visible_rows, float wheel);
float UnitsPerCount(float scale, float target_pixels, float window_pixels);

struct Take {
    bool pointing = false, moved = false;
    std::uint32_t pressed = 0, released = 0, held = 0;
    float wheel = 0;
};
struct Pointer {
    Point pos;
    bool pointing = false;
    std::uint32_t buttons = 0;
    Take Update(const InputMenuMouse &mouse, Point start, Point units, Rect bounds, bool pad);
};

enum class Action { None, Click, PickUp, Drop, Cancel };
struct Gesture {
    bool pending = false, dragging = false, release_pending = false;
    Target source;
    Point origin;
    void Reset();
    Action Update(const Take &take, Point p, Target hit, bool can_pick_up, bool holding);
};
}

// Menu input is sampled at the retail menu's SelDataInit, once per menu tick.
// The pad latch clears synthetic presses and releases the channel after exit.
void MenuMouseUpdate(CMenuKeyFunc &keys);
void MenuMousePadTick();
void MenuMouseClose();
int MenuMouseSyntheticDown();
bool MenuMouseDrawPosition(const CMenuPosDataForm *form, int &x, int &y);

bool MenuMouseItemActive();
void MenuMouseItemOrigin(float &x, float &y);
