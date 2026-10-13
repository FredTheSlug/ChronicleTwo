#pragma once
#include <menuop.hpp>
extern CSaveMenuClass *SaveMenuPtr asm("MenuMouse_Save");
// Let the PC page wrap the original Options step without replacing its state machine.
extern CMenuOption *CMenuOptionPtr asm("MenuOption_Current");
