#include "common.h"

#include "editmap.hpp"
#include "editparts.hpp"

PORT_OVERRIDE int CEditMap::GetePlaceIDList(int *output, int capacity) {
    int count = 0;
    for (int index = 0; index < edit_parts_max && count < capacity; ++index) {
        if (edit_parts[index].name[0] != '\0') {
            output[count++] = index;
        }
    }
    return count;
}
