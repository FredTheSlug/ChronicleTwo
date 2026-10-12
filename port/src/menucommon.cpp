#include "common.h"

#include "dataread.hpp"
#include "menucommon.hpp"
#include "mg_memory.hpp"

PORT_OVERRIDE int MenuCommonReadData(mgCMemory *memory, char **names, int mode) {
    StartReadBG();
    memory->Align64();
    int total = 0;
    for (char **entry = names; *entry != nullptr; ++entry) {
        const unsigned int size = LoadFileMenu(*entry, memory->stGetTop(), mode);
        memory->Alloc(size / 16u + (size % 16u != 0));
        total += size;
        memory->Align64();
    }
    return total;
}
