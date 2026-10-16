#pragma once

void GameSetIdentity();

// Swaps the two moons the build ships for Max's wrench, the memory card icon (dc2_ic.ico in
// menu/<lang>/save.pac), drawn from the extracted data so no build carries a copy of it. On Windows the
// first launch with the data also writes chronicletwo.ico beside data/ and a "Dark Chronicle" shortcut
// beside the executable that carries it.
void GameSetDataIcon();
