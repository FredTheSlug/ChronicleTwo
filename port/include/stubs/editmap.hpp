#pragma once

class CEditMap;
class mgCMemory;

/** Applies a georama map's native editor settings in place of the editor-parts script's run. */
void PortApplyEditMapConfig(CEditMap *map, char *script, int size, mgCMemory *stack);
