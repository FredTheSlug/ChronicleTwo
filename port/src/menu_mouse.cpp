#include "menu_mouse.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "gamepad.hpp"
#include "mainloop.hpp"
#include "menu_mouse_item.hpp"
#include "mglib.hpp"
#include "mglib_port.hpp"
#include "platform/input.hpp"
#include "platform/window.hpp"

namespace {
bool            enabled = false, closing = false;
int             frame = -1, buttons = 0, rows_frame = -1;
std::uint32_t   held = 0;
MenuMouseSample sample;
bool            navigation = false;

struct Choices {
    const void                *owner = nullptr;
    int                        message = -1, drawn = -100, selected = -1, target = -1, ready = 0;
    bool                       horizontal = false, confirm = false;
    std::vector<MenuMouseRect> rows;
} choices;

struct ListClick {
    const void *owner = nullptr;
    int         seen = -100, ready = 0, target = -1;
    bool        pending = false;
} list_click;

void SetCapture(bool on);

} // namespace

bool MenuMouseRect::Contains(float px, float py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
}

float MenuMouseUnits(float scale, float pixels, float window) {
    return scale > 0 && pixels > 0 && window > 0 ? pixels / (scale * window) : 1;
}

int MenuMouseHit(std::span<const MenuMouseRect> rows, float x, float y) {
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        if (rows[i].Contains(x, y)) {
            return i;
        }
    }
    return -1;
}

int MenuMouseChoose(const MenuMouseSample &mouse, std::span<const MenuMouseRect> rows, int selected, int wheel_step) {
    if (!mouse.pointing || rows.empty()) {
        return selected;
    }
    const int hit = MenuMouseHit(rows, mouse.x, mouse.y);
    if (hit >= 0 && (mouse.moved || (mouse.clicked & 1))) {
        selected = hit;
    }
    if (mouse.wheel != 0 && std::isfinite(mouse.wheel)) {
        selected = std::clamp(selected + (mouse.wheel < 0 ? wheel_step : -wheel_step), 0, static_cast<int>(rows.size()) - 1);
    }
    return selected;
}

namespace {
void SetCapture(bool on) {
    if (enabled == on) {
        return;
    }
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

} // namespace

void MenuMouseEnable(bool on) {
    navigation = on;
    choices = {};
    list_click = {};
    SetCapture(on);
}

void MenuMouseNoteChoices(const void *owner, int message, std::span<const MenuMouseRect> rows, int selected, bool horizontal) {
    if (choices.owner != owner || choices.message != message || choices.drawn < mgPortFrameCount() - 1) {
        choices.target = selected;
        choices.confirm = false;
    }
    if (choices.selected != selected) {
        choices.ready = mgPortFrameCount() + 6;
    }
    choices.owner = owner;
    choices.message = message;
    choices.drawn = mgPortFrameCount();
    choices.selected = selected;
    choices.horizontal = horizontal;
    choices.rows.assign(rows.begin(), rows.end());
    if (!enabled) {
        SetCapture(true);
    }
}

const MenuMouseSample &MenuMouseTake() {
    if (!navigation && enabled && choices.drawn < mgPortFrameCount() - 1) {
        SetCapture(false);
    }
    if ((!enabled && !closing) || frame == mgPortFrameCount()) {
        return sample;
    }
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
    sample.released = held & ~mouse.buttons;
    sample.held = mouse.buttons;
    held = mouse.buttons;
    sample.wheel = mouse.wheel;
    sample.moved = mouse.dx != 0 || mouse.dy != 0;
    const auto &pad = InputGetPad(0);
    WindowMode  mode;
    const bool  unfocused = WindowCurrentMode(mode) && !(SDL_GetWindowFlags(WindowHandle()) & SDL_WINDOW_INPUT_FOCUS);
    const bool  pad_active = pad.buttons || GamePad__2.pad[0].button ||
                            std::abs(int(pad.left_x) - 128) > 30 || std::abs(int(pad.left_y) - 128) > 30;
    if (pad_active || unfocused) {
        sample.pointing = false;
        sample.clicked = sample.released = 0;
        sample.wheel = 0;
        buttons = 0;
        choices.confirm = false;
        choices.target = choices.selected;
        return sample;
    }
    if (!sample.pointing) {
        choices.confirm = false;
        choices.target = choices.selected;
        if (sample.moved || sample.clicked || sample.wheel != 0) {
            MenuMouseItemOrigin(sample.x, sample.y);
        }
    }
    if (sample.moved || sample.clicked || sample.wheel != 0) {
        sample.pointing = true;
    }
    const auto extent = mgPortVisibleExtent(gfx::kMainTarget);
    float      left = extent.left, top = extent.top, right = extent.right, bottom = extent.bottom;
    float      units_x = 1, units_y = 1;
    if (mgPortRendererReady()) {
        const auto mapping = gfx::GetUiMapping(gfx::kMainTarget);
        const auto scene = gfx::GetLogicalMapping(gfx::kMainTarget);
        int        width = static_cast<int>(mapping.pixel_width), height = static_cast<int>(mapping.pixel_height);
        WindowSize(width, height);
        units_x = MenuMouseUnits(mapping.scale_x, mapping.pixel_width, width);
        units_y = MenuMouseUnits(mapping.scale_y, mapping.pixel_height, height);
        left = (extent.left * scene.scale_x + scene.offset_x - mapping.offset_x) / mapping.scale_x;
        right = (extent.right * scene.scale_x + scene.offset_x - mapping.offset_x) / mapping.scale_x;
        top = (extent.top * scene.scale_y + scene.offset_y - mapping.offset_y) / mapping.scale_y;
        bottom = (extent.bottom * scene.scale_y + scene.offset_y - mapping.offset_y) / mapping.scale_y;
    }
    if (std::isfinite(mouse.dx)) {
        sample.x += mouse.dx * units_x;
    }
    if (std::isfinite(mouse.dy)) {
        sample.y += mouse.dy * units_y;
    }
    sample.x = std::clamp(sample.x, left, right - 1);
    sample.y = std::clamp(sample.y, top, bottom - 1);
    if (MenuMouseItemActive()) {
        buttons = 0;
        choices.confirm = false;
        return sample;
    }
    buttons = (sample.clicked & 1 ? PAD_CROSS : 0) | (sample.clicked & 2 ? PAD_CIRCLE : 0);
    if (choices.drawn >= frame - 1 && !choices.rows.empty()) {
        buttons &= ~PAD_CROSS;
        if (sample.pointing) {
            const int hit = MenuMouseHit(choices.rows, sample.x, sample.y);
            if (hit >= 0 && (sample.moved || (sample.clicked & 1))) {
                choices.target = hit;
            }
            if (sample.wheel != 0 && std::isfinite(sample.wheel)) {
                int target = choices.selected + (sample.wheel < 0 ? 1 : -1);
                if (target >= 0 && target < static_cast<int>(choices.rows.size()) && choices.rows[target].w > 0) {
                    choices.target = target;
                }
            }
            if ((sample.clicked & 1) && hit >= 0) {
                choices.confirm = true;
            }
            if (sample.clicked & 2) {
                choices.confirm = false;
            }
            if (choices.target != choices.selected) {
                buttons |= choices.horizontal ? (choices.target < choices.selected ? PAD_LEFT : PAD_RIGHT) : (choices.target < choices.selected ? PAD_UP : PAD_DOWN);
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
    if (mask & PAD_START) {
        buttons &= ~PAD_CROSS;
    }
    buttons |= mask;
}

int MenuMouseRows(std::span<const MenuMouseRect> rows, int selected, int wheel_step) {
    const auto &mouse = MenuMouseTake();
    if (rows_frame == frame) {
        return selected;
    }
    rows_frame = frame;
    if (mouse.clicked & 1) {
        buttons &= ~PAD_CROSS;
        if (mouse.pointing && MenuMouseHit(rows, mouse.x, mouse.y) >= 0) {
            buttons |= PAD_CROSS;
        }
    }
    return MenuMouseChoose(mouse, rows, selected, wheel_step);
}

void MenuMouseDelayConfirm(const void *owner, int selected, int target, int ticks) {
    if (list_click.owner != owner || list_click.seen < frame - 1) {
        list_click = {owner, frame, frame, selected, false};
    }
    list_click.seen = frame;
    if (target != list_click.target) {
        list_click.target = target;
        list_click.pending = false;
    }
    if (target != selected) {
        list_click.ready = frame + ticks;
    }
    if (buttons & PAD_CROSS) {
        list_click.pending = true;
    }
    buttons &= ~PAD_CROSS;
    if ((buttons & PAD_CIRCLE) || !sample.pointing) {
        list_click.pending = false;
    }
    if (list_click.pending && target == selected && frame >= list_click.ready) {
        buttons |= PAD_CROSS;
        list_click.pending = false;
    }
}

void MenuMouseDraw() {
    if (!enabled || !sample.pointing || MenuMouseItemActive()) {
        return;
    }
    gfx::DrawState state{};
    state.blend = true;
    const float         x = sample.x, y = sample.y;
    const gfx::Vertex2D vertices[] = {
        {x,     y,      1, 0, 0, {255, 255, 255, 255}},
        {x + 3, y + 14, 1, 0, 0, {255, 255, 255, 255}},
        {x + 6, y + 9,  1, 0, 0, {255, 255, 255, 255}},
    };
    gfx::Draw2D(gfx::Primitive::Triangles, vertices, {}, state);
}

void MenuMouseSetButtons(int mask) { buttons = mask; }

bool MenuMouseActive() { return enabled; }
