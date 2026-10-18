#pragma once

#include "monster.hpp"
#include "runscript.hpp"

/** Quadwords reserved for a monster's two native interpreter stacks and the strict arena end. */
inline constexpr int kMonsterScriptQuadwords = (128 * sizeof(RS_STACKDATA) + 15) / 16 +
                                               (512 * sizeof(RS_CALLDATA) + 15) / 16 + 1;

/**
 *
 * Gives every active monster slot its own native arena for model and script copies.
 */
void PrepareMonsterMemory(CMonsterMan &manager);
