#include "menu_mouse.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "gamepad.hpp"
#include "mainloop.hpp"
#include "mglib.hpp"
#include "mglib_port.hpp"
#include "platform/input.hpp"
#include "platform/window.hpp"

namespace {
bool enabled = false, closing = false;
int frame = -1, buttons = 0, rows_frame = -1;
std::uint32_t held = 0;
MenuMouseSample sample;
bool navigation = false;
struct Choices {
    const void *owner = nullptr;
    int message = -1, drawn = -100, selected = -1, target = -1, ready = 0;
    bool horizontal = false, confirm = false;
    std::vector<MenuMouseRect> rows;
} choices;
void SetCapture(bool on);

}

bool MenuMouseRect::Contains(float px, float py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
}

float MenuMouseUnits(float scale, float pixels, float window) {
    return scale > 0 && pixels > 0 && window > 0 ? pixels / (scale * window) : 1;
}

int MenuMouseHit(std::span<const MenuMouseRect> rows, float x, float y) {
    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        if (rows[i].Contains(x, y)) return i;
    return -1;
}

int MenuMouseChoose(const MenuMouseSample &mouse, std::span<const MenuMouseRect> rows, int selected, int wheel_step) {
    if (!mouse.pointing || rows.empty()) return selected;
    const int hit = MenuMouseHit(rows, mouse.x, mouse.y);
    if (hit >= 0 && (mouse.moved || (mouse.clicked & 1))) selected = hit;
    if (mouse.wheel != 0 && std::isfinite(mouse.wheel))
        selected = std::clamp(selected + (mouse.wheel < 0 ? wheel_step : -wheel_step), 0, static_cast<int>(rows.size()) - 1);
    return selected;
}

namespace {
void SetCapture(bool on) {
    if (enabled == on) return;
    enabled = on;
    if (on) {
        closing = false;
        InputSetMenuMouse(true);
        held = InputTakeMenuMouse().buttons;
    } else {
        closing = true;
    }
    sample = {};
    sample.x = mgScreenWidth * 0.5f;
    sample.y = mgScreenHeight * 0.5f;
    frame = rows_frame = -1;
    buttons = 0;
}

}

void MenuMouseEnable(bool on) {
    navigation = on;
    choices = {};
    SetCapture(on);
}

void MenuMouseNoteChoices(const void *owner, int message, std::span<const MenuMouseRect> rows, int selected, bool horizontal) {
    if (choices.owner != owner || choices.message != message || choices.drawn < mgPortFrameCount() - 1) {
        choices.target = selected;
        choices.confirm = false;
    }
    if (choices.selected != selected) choices.ready = mgPortFrameCount() + 6;
    choices.owner = owner;
    choices.message = message;
    choices.drawn = mgPortFrameCount();
    choices.selected = selected;
    choices.horizontal = horizontal;
    choices.rows.assign(rows.begin(), rows.end());
    if (!enabled) SetCapture(true);
}

const MenuMouseSample &MenuMouseTake() {
    if (!navigation && enabled && choices.drawn < mgPortFrameCount() - 1) SetCapture(false);
    if ((!enabled && !closing) || frame == mgPortFrameCount()) return sample;
    frame = mgPortFrameCount();
    const auto mouse = InputTakeMenuMouse();
    if (closing) {
        buttons = 0;
        if (mouse.buttons == 0) {
            closing = false;
            InputSetMenuMouse(false);
        }
        return sample;
    }
    sample.clicked = mouse.buttons & ~held;
    held = mouse.buttons;
    sample.wheel = mouse.wheel;
    sample.moved = mouse.dx != 0 || mouse.dy != 0;
    const auto &pad = InputGetPad(0);
    if (pad.buttons || std::abs(int(pad.left_x) - 128) > 30 || std::abs(int(pad.left_y) - 128) > 30)
        sample.pointing = false;
    if (!sample.pointing) choices.confirm = false;
    if (sample.moved || sample.clicked || sample.wheel != 0) sample.pointing = true;
    const auto mapping = gfx::GetUiMapping(gfx::kMainTarget);
    int width = static_cast<int>(mapping.pixel_width), height = static_cast<int>(mapping.pixel_height);
    WindowSize(width, height);
    const auto extent = mgPortVisibleExtent(gfx::kMainTarget);
    if (std::isfinite(mouse.dx)) sample.x += mouse.dx * MenuMouseUnits(mapping.scale_x, mapping.pixel_width, width);
    if (std::isfinite(mouse.dy)) sample.y += mouse.dy * MenuMouseUnits(mapping.scale_y, mapping.pixel_height, height);
    sample.x = std::clamp(sample.x, extent.left, extent.right - 1);
    sample.y = std::clamp(sample.y, extent.top, extent.bottom - 1);
    buttons = (sample.clicked & 1 ? PAD_CROSS : 0) | (sample.clicked & 2 ? PAD_CIRCLE : 0);
    if (choices.drawn >= frame - 1 && !choices.rows.empty()) {
        buttons &= ~PAD_CROSS;
        if (sample.pointing) {
            const int hit = MenuMouseHit(choices.rows, sample.x, sample.y);
            if (hit >= 0 && (sample.moved || (sample.clicked & 1))) choices.target = hit;
            if (sample.wheel != 0 && std::isfinite(sample.wheel)) {
                int target = choices.selected + (sample.wheel < 0 ? 1 : -1);
                if (target >= 0 && target < static_cast<int>(choices.rows.size()) && choices.rows[target].w > 0)
                    choices.target = target;
            }
            if ((sample.clicked & 1) && hit >= 0) choices.confirm = true;
            if (sample.clicked & 2) choices.confirm = false;
            if (choices.target != choices.selected) {
                buttons |= choices.horizontal ? (choices.target < choices.selected ? PAD_LEFT : PAD_RIGHT) :
                                                (choices.target < choices.selected ? PAD_UP : PAD_DOWN);
            } else if (choices.confirm && frame >= choices.ready) {
                buttons |= PAD_CROSS;
                choices.confirm = false;
            }
        }
    }
    return sample;
}

int MenuMouseButtons() {
    MenuMouseTake();
    return enabled ? buttons : 0;
}

void MenuMousePress(int mask) {
    if (mask & PAD_START) buttons &= ~PAD_CROSS;
    buttons |= mask;
}

int MenuMouseRows(std::span<const MenuMouseRect> rows, int selected, int wheel_step) {
    const auto &mouse = MenuMouseTake();
    if (rows_frame == frame) return selected;
    rows_frame = frame;
    if (mouse.clicked & 1) {
        buttons &= ~PAD_CROSS;
        if (mouse.pointing && MenuMouseHit(rows, mouse.x, mouse.y) >= 0) buttons |= PAD_CROSS;
    }
    return MenuMouseChoose(mouse, rows, selected, wheel_step);
}

void MenuMouseDraw() {
    if (!enabled || !sample.pointing) return;
    gfx::DrawState state{};
    state.blend = true;
    const float x = sample.x, y = sample.y;
    const gfx::Vertex2D vertices[] = {
        {x, y, 1, 0, 0, {255, 255, 255, 255}},
        {x + 3, y + 14, 1, 0, 0, {255, 255, 255, 255}},
        {x + 6, y + 9, 1, 0, 0, {255, 255, 255, 255}},
    };
    gfx::Draw2D(gfx::Primitive::Triangles, vertices, {}, state);
}
