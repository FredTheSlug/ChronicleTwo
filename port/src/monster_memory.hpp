#pragma once

#include "monster.hpp"

void PrepareMonsterMemory(CMonsterMan &manager, mgCMemory &memory);

// Reads game.more_monsters for the floor being set up, and returns the priority limit: how many of the nearest
// monsters show and act.
int PortMonsterLatchLimits();
// Monster slots the current floor uses: retail's 24, or MONSTER_ACTIVE_MAX with game.more_monsters.
int PortMonsterActiveLimit();
