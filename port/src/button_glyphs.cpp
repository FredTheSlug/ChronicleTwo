#include "button_glyphs.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <vector>

#include <nlohmann/json.hpp>

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

#include "platform/input.hpp"
#include "platform/paths.hpp"
#include "platform/settings.hpp"

namespace {

namespace fs = std::filesystem;

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

using Cells = std::map<std::string, Rect, std::less<>>;

// Where the retail art keeps its PS2 symbols, in texels of the logical texture. Every language's
// gaiji sheet (meswin/<language>/gaiji.img, the message font's symbols) shares one layout; the skip
// prompt (img/<language>/skip.img) keeps its triangle in one place. glyphs.json's "targets" adds or
// moves cells without a rebuild.
const std::map<std::string, Cells, std::less<>> &BuiltinTargets() {
    static const std::map<std::string, Cells, std::less<>> targets = {
        {"gaiji",
         {
             {"select_label", {2, 2, 52, 17}},
             {"start_label", {62, 2, 44, 17}},
             {"circle", {1, 23, 20, 20}},
             {"triangle", {23, 23, 20, 20}},
             {"square", {45, 23, 20, 20}},
             {"cross", {67, 23, 20, 20}},
             {"dpad", {2, 68, 18, 18}},
             {"dpad_ud", {24, 68, 18, 18}},
             {"dpad_lr", {46, 68, 18, 18}},
             {"l2", {5, 134, 22, 17}},
             {"l1", {37, 134, 22, 17}},
             {"r1", {69, 134, 22, 17}},
             {"r2", {101, 134, 22, 17}},
             {"start", {14, 157, 24, 15}},
             {"start_label@pill", {44, 156, 45, 19}},
             {"select", {14, 179, 24, 16}},
             {"select_label@pill", {44, 177, 52, 19}},
             {"lstick", {137, 202, 26, 27}},
             {"rstick", {165, 201, 26, 28}},
         }},
        {"skip", {{"triangle", {8, 26, 20, 19}}}},
    };
    return targets;
}

struct Style {
    fs::path                   file;
    Cells                      glyphs;
    std::vector<unsigned char> pixels;
    int                        width = 0;
    int                        height = 0;
    bool                       tried = false;
};

struct State {
    bool                                              indexed = false;
    std::map<std::string, Style, std::less<>>         styles;
    std::map<std::string, Cells, std::less<>>         targets;
};

State &Get() {
    static State state;
    return state;
}

bool ReadFile(const fs::path &file, std::vector<unsigned char> &bytes) {
    std::error_code error;
    const auto      size = fs::file_size(file, error);
    if (error || size > 64ull * 1024 * 1024) {
        return false;
    }
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return true;
}

bool ReadRect(const nlohmann::json &value, Rect &rect) {
    if (!value.is_array() || value.size() != 4) {
        return false;
    }
    for (const auto &number : value) {
        if (!number.is_number_integer()) {
            return false;
        }
    }
    rect = {value[0].get<int>(), value[1].get<int>(), value[2].get<int>(), value[3].get<int>()};
    return rect.x >= 0 && rect.y >= 0 && rect.w > 0 && rect.h > 0;
}

// glyphs/ beside the executable, then in the save folder, as Kazarov's glyphs module reads it.
void Index() {
    State &state = Get();
    if (state.indexed) {
        return;
    }
    state.indexed = true;
    state.targets.clear();
    for (const auto &[name, cells] : BuiltinTargets()) {
        state.targets.emplace(name, cells);
    }
    std::vector<fs::path> directories;
    if (const fs::path exe = PathsExecutable(); !exe.empty()) {
        directories.push_back(exe.parent_path() / "glyphs");
    }
    directories.push_back(PathsSaveRoot() / "glyphs");
    for (const fs::path &directory : directories) {
        std::vector<unsigned char> bytes;
        if (!ReadFile(directory / "glyphs.json", bytes)) {
            continue;
        }
        const auto root = nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, false);
        if (!root.is_object() || !root.contains("styles") || !root["styles"].is_object()) {
            std::fprintf(stderr, "glyphs: %s is not a glyph index\n", PathsDisplay(directory / "glyphs.json").c_str());
            continue;
        }
        for (const auto &[name, value] : root["styles"].items()) {
            if (!value.is_object() || !value.contains("atlas") || !value["atlas"].is_string() ||
                !value.contains("glyphs") || !value["glyphs"].is_object()) {
                continue;
            }
            Style style;
            style.file = directory / fs::path(value["atlas"].get<std::string>());
            for (const auto &[glyph, rect] : value["glyphs"].items()) {
                Rect cell;
                if (ReadRect(rect, cell)) {
                    style.glyphs[glyph] = cell;
                }
            }
            state.styles.emplace(name, std::move(style));
        }
        if (root.contains("targets") && root["targets"].is_object()) {
            for (const auto &[texture, cells] : root["targets"].items()) {
                if (!cells.is_object()) {
                    continue;
                }
                for (const auto &[glyph, rect] : cells.items()) {
                    Rect cell;
                    if (ReadRect(rect, cell)) {
                        state.targets[texture][glyph] = cell;
                    }
                }
            }
        }
        std::fprintf(stderr, "glyphs: button symbols from %s\n", PathsDisplay(directory).c_str());
        return;
    }
}

bool Load(Style &style) {
    if (!style.pixels.empty()) {
        return true;
    }
    if (style.tried) {
        return false;
    }
    style.tried = true;
    std::vector<unsigned char> bytes;
    if (!ReadFile(style.file, bytes)) {
        std::fprintf(stderr, "glyphs: cannot read %s\n", PathsDisplay(style.file).c_str());
        return false;
    }
    int            channels = 0;
    unsigned char *pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &style.width,
                                                  &style.height, &channels, 4);
    if (pixels == nullptr || style.width <= 0 || style.height <= 0 || style.width > 8192 || style.height > 8192) {
        stbi_image_free(pixels);
        std::fprintf(stderr, "glyphs: cannot decode %s\n", PathsDisplay(style.file).c_str());
        return false;
    }
    style.pixels.assign(pixels, pixels + static_cast<std::size_t>(style.width) * style.height * 4);
    stbi_image_free(pixels);
    return true;
}

// The pad button whose binding names a keycap of the keyboard style.
const char *Action(std::string_view glyph) {
    static constexpr std::array<const char *, 12> kActions = {"cross", "circle", "square", "triangle", "l1",
                                                              "r1", "l2", "r2", "start", "select", "l3", "r3"};
    for (const char *action : kActions) {
        if (glyph == action) {
            return action;
        }
    }
    if (glyph == "start_label") {
        return "start";
    }
    if (glyph == "select_label") {
        return "select";
    }
    return nullptr;
}

// The style's symbol for a target cell, or nullptr. A target named glyph@place is a second cell of
// the same glyph. A label cell standing alone falls back to its button's symbol; one beside its
// button (@pill) does not, so the symbol is not drawn twice.
const Rect *Symbol(Style &style, std::string_view target) {
    const auto        at = target.find('@');
    const std::string_view glyph = target.substr(0, at);
    if (const auto found = style.glyphs.find(glyph); found != style.glyphs.end()) {
        return &found->second;
    }
    if (at == std::string_view::npos && (glyph == "start_label" || glyph == "select_label")) {
        if (const auto found = style.glyphs.find(glyph.substr(0, glyph.find('_'))); found != style.glyphs.end()) {
            return &found->second;
        }
    }
    return nullptr;
}

// Box-filters the symbol into the cell's physical texels, aspect kept and centred, over a cleared cell.
void Paste(const Style &style, const Rect &source, const Rect &cell, uint32_t scale,
           datapacks::DecodedTexture &decoded, std::size_t mip) {
    const uint32_t canvas_width = std::max(1u, decoded.width >> mip);
    const uint32_t canvas_height = std::max(1u, decoded.height >> mip);
    const double   unit = static_cast<double>(scale) / static_cast<double>(1u << mip);
    const int      x0 = static_cast<int>(cell.x * unit);
    const int      y0 = static_cast<int>(cell.y * unit);
    const int      x1 = std::min(static_cast<int>(canvas_width), static_cast<int>((cell.x + cell.w) * unit));
    const int      y1 = std::min(static_cast<int>(canvas_height), static_cast<int>((cell.y + cell.h) * unit));
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    const int src_x = std::clamp(source.x, 0, style.width - 1);
    const int src_y = std::clamp(source.y, 0, style.height - 1);
    const int src_w = std::min(source.w, style.width - src_x);
    const int src_h = std::min(source.h, style.height - src_y);
    if (src_w <= 0 || src_h <= 0) {
        return;
    }
    const double fit = std::min(static_cast<double>(x1 - x0) / src_w, static_cast<double>(y1 - y0) / src_h);
    const double draw_w = src_w * fit;
    const double draw_h = src_h * fit;
    const double left = x0 + (x1 - x0 - draw_w) / 2.0;
    const double top = y0 + (y1 - y0 - draw_h) / 2.0;
    auto        &level = decoded.levels[mip];
    constexpr int kTaps = 4;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            double sum[4] = {};
            for (int ty = 0; ty < kTaps; ++ty) {
                for (int tx = 0; tx < kTaps; ++tx) {
                    const double u = (x + (tx + 0.5) / kTaps - left) / fit;
                    const double v = (y + (ty + 0.5) / kTaps - top) / fit;
                    if (u < 0.0 || v < 0.0 || u >= src_w || v >= src_h) {
                        continue;
                    }
                    const auto *texel = &style.pixels[(static_cast<std::size_t>(src_y + static_cast<int>(v)) * style.width +
                                                       src_x + static_cast<int>(u)) * 4];
                    const double alpha = texel[3] / 255.0;
                    sum[0] += texel[0] * alpha;
                    sum[1] += texel[1] * alpha;
                    sum[2] += texel[2] * alpha;
                    sum[3] += alpha;
                }
            }
            auto *out = &level[(static_cast<std::size_t>(y) * canvas_width + x) * 4];
            if (sum[3] <= 0.0) {
                out[0] = out[1] = out[2] = out[3] = 0;
                continue;
            }
            out[0] = static_cast<uint8_t>(std::clamp(sum[0] / sum[3] + 0.5, 0.0, 255.0));
            out[1] = static_cast<uint8_t>(std::clamp(sum[1] / sum[3] + 0.5, 0.0, 255.0));
            out[2] = static_cast<uint8_t>(std::clamp(sum[2] / sum[3] + 0.5, 0.0, 255.0));
            out[3] = static_cast<uint8_t>(std::clamp(sum[3] / (kTaps * kTaps) * 255.0 + 0.5, 0.0, 255.0));
        }
    }
}

} // namespace

std::string ButtonGlyphsStyle() {
    const HostSettings &config = SettingsGet();
    if (!config.glyphs_new) {
        return {};
    }
    InputGlyphFamily family = InputGlyphFamily::Ps4;
    switch (config.glyph_device) {
        case ConfigGlyphDevice::Auto: family = InputActiveGlyphFamily(); break;
        case ConfigGlyphDevice::Ps3: return "ps3";
        case ConfigGlyphDevice::Ps4: return "ps4";
        case ConfigGlyphDevice::Ps5: return "ps5";
        case ConfigGlyphDevice::Ps5Color: return "ps5color";
        case ConfigGlyphDevice::Xbox: return "xbox";
        case ConfigGlyphDevice::Switch: return "switch";
        case ConfigGlyphDevice::SteamDeck: return "steamdeck";
        case ConfigGlyphDevice::SteamController: return "steamcontroller";
        case ConfigGlyphDevice::Keyboard: return "keyboard";
    }
    switch (family) {
        case InputGlyphFamily::Ps3: return "ps3";
        case InputGlyphFamily::Ps5: return "ps5";
        case InputGlyphFamily::Xbox: return "xbox";
        case InputGlyphFamily::Switch: return "switch";
        case InputGlyphFamily::SteamDeck: return "steamdeck";
        case InputGlyphFamily::SteamController: return "steamcontroller";
        case InputGlyphFamily::Keyboard: return "keyboard";
        default: return "ps4";
    }
}

bool ButtonGlyphsTarget(std::string_view texture_name) {
    Index();
    return Get().targets.contains(texture_name);
}

void ButtonGlyphsApply(std::string_view texture_name, int logical_width, int logical_height,
                       datapacks::DecodedTexture &decoded) {
    Index();
    State     &state = Get();
    const auto target = state.targets.find(texture_name);
    if (target == state.targets.end() || decoded.levels.empty() || logical_width <= 0 || logical_height <= 0) {
        return;
    }
    const std::string name = ButtonGlyphsStyle();
    if (name.empty()) {
        return;
    }
    const auto style = state.styles.find(name);
    if (style == state.styles.end() || !Load(style->second)) {
        return;
    }
    uint32_t retail_width = 1;
    while (retail_width < static_cast<uint32_t>(logical_width)) retail_width <<= 1;
    const uint32_t scale = std::max(1u, decoded.width / retail_width);
    for (const auto &[glyph, cell] : target->second) {
        if (cell.x + cell.w > logical_width || cell.y + cell.h > logical_height) {
            continue;
        }
        Style      *from = &style->second;
        const Rect *symbol = nullptr;
        if (name == "keyboard") {
            // The key the player bound to the pad button, as Kazarov's glyphs::Find draws it.
            if (const char *action = Action(std::string_view(glyph).substr(0, glyph.find('@')))) {
                const std::string key = InputPrimaryBindingName(action);
                if (const auto found = from->glyphs.find(key); !key.empty() && found != from->glyphs.end()) {
                    symbol = &found->second;
                }
            }
        }
        if (symbol == nullptr) {
            symbol = Symbol(*from, glyph);
        }
        if (symbol == nullptr) {
            continue;
        }
        for (std::size_t mip = 0; mip < decoded.levels.size(); ++mip) {
            Paste(*from, *symbol, cell, scale, decoded, mip);
        }
    }
}

void ButtonGlyphsReset() {
    Get() = State{};
}
