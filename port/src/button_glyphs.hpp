#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "platform/datapacks.hpp"

// Redraws the PS2 button symbols inside the game's own textures (the message font's gaiji sheet, the
// skip prompt...) with the symbols of glyphs/ for the style that input.glyphs and input.glyph_device
// select. The symbols are pasted into the decoded texture, so every menu and message that samples it
// shows them. See docs/GLYPHS.md.

// Whether a texture of this catalog name holds button symbols that a style can replace.
bool ButtonGlyphsTarget(std::string_view texture_name);

// Pastes the current style's symbols into a decoded texture of the given logical size. Leaves the
// texture alone when the setting is "original", when glyphs/ has no symbol for a cell, or for
// textures that hold no symbols.
void ButtonGlyphsApply(std::string_view texture_name, int logical_width, int logical_height,
                       datapacks::DecodedTexture &decoded);

// The style the symbols are drawn in now ("" for the game's own art). Changes when the setting does
// or, on Auto, when the player switches device.
std::string ButtonGlyphsStyle();

// Forgets the decoded glyph atlases, so the next Apply reads glyphs/ again.
void ButtonGlyphsReset();
