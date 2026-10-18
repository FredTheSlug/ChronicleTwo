#include "common.h"

#include "charasetup.hpp"
#include "menu_world.hpp"
#include "mg_memory.hpp"

// dng_main.hpp declares BaseCharacter with retail's six elements.
void DngMainDraw();
void PortRetailDngMainDraw() asm("DngMain_DngMainDraw");

// The seventh element is retail's BuffTempData, which SetupMainUnit reaches as stacks[6] by link adjacency on the PS2.
PORT_OVERRIDE mgCMemory BaseCharacter[CHARA_MEM_STACK_NUM];

PORT_OVERRIDE void DngMainDraw() {
    PortRetailDngMainDraw();
    menuworld::AfterWorldDraw();
}
