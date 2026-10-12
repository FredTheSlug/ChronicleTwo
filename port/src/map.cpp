#include "common.h"

#include "map.hpp"
#include "mapparts.hpp"
#include "mg_memory.hpp"

PORT_OVERRIDE void CMap::SetPlacePartsBuff(mgCMemory *memory, int capacity) {
    const std::size_t parts_bytes = sizeof(CMapParts) * capacity;
    place_parts = new (memory->Alloc((parts_bytes + 15) / 16 + 2)) CMapParts[capacity];
    const std::size_t pointers_bytes = sizeof(CMapParts *) * capacity;
    draw_parts = new (memory->Alloc((pointers_bytes + 15) / 16 + 2)) CMapParts *[capacity];
    place_parts_max = capacity;
    ClearPlaceParts();
}
