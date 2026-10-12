#pragma once

void DebugModeBoot(bool debug_mode);

// Pad 1 is not locked by the debug lock, so this works while pad 2 is locked.
bool DebugModeToggleChord(int pad1_held, int pad1_pressed);
