#include "common.h"

#include "dng_effect.hpp"
#include "menu_world.hpp"

// The player's sword glow and pull wire are tagged while the dungeon pause frame is recorded.
PORT_OVERRIDE void CSwordLuminous::Draw() {
    const menuworld::PlayerEffectScope scope(true);
    PortRetailDraw();
}

PORT_OVERRIDE void CAfterWire::DrawWire(float (*smooth)[4]) {
    const menuworld::PlayerEffectScope scope(true);
    PortRetailDrawWire(smooth);
}
