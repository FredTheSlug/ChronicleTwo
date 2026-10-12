#include "common.h"

#include <algorithm>
#include <limits>

#include "inventmn.hpp"
#include "mg_memory.hpp"
#include "scriptinterpreter.hpp"
#include "stubs/inventmn.hpp"
#include "platform/arena.hpp"

namespace {
struct RecipeLayout {
    std::size_t quadwords = 0;
    int capacity = 0;
    int rows = 0;
    bool valid = true;
};
thread_local RecipeLayout *layout;

std::size_t Quadwords(std::size_t bytes) { return (bytes + 15) / 16; }

int CountTable(SPI_STACK *args, int) {
    layout->capacity = spiGetStackInt(args);
    layout->rows = 0;
    if (layout->capacity < 0) {
        layout->valid = false;
        return 0;
    }
    layout->quadwords += Quadwords(std::size_t(layout->capacity) * sizeof(INVENT_DATA_INFO));
    return 1;
}

int CountMaterials(SPI_STACK *, int argc) {
    const int count = (argc - 9) / 2;
    if (layout->rows >= layout->capacity || count <= 0) return 0;
    layout->quadwords += Quadwords(std::size_t(count) * sizeof(INVENT_MATERIAL));
    ++layout->rows;
    return 1;
}
}

PORT_OVERRIDE int CInventDataManage::LoadAnalyzeInventFile(char *script, int size) {
    if (script == nullptr) return false;

    // Recipe rows contain native material pointers. Measure both the table and each
    // separately rounded material list before loading, rather than using the PS2 menu slot.
    RecipeLayout measured;
    RecipeLayout *previous = layout;
    layout = &measured;
    SPI_TAG_PARAM count_tags[] = {{"DATATABLESET", CountTable}, {"DATASET", CountMaterials}, {nullptr, nullptr}};
    CScriptInterpreter counter;
    counter.SetTag(count_tags);
    counter.SetScript(script, size);
    counter.Run();
    layout = previous;
    if (!measured.valid || measured.quadwords >= std::size_t(std::numeric_limits<int>::max())) return false;

    // mgCMemory retains a spare quadword at the end of every allocation run.
    const int count = static_cast<int>(measured.quadwords + 1);
    auto *storage = reinterpret_cast<u_long128 *>(ArenaBlock(this, std::size_t(count) * sizeof(u_long128)));
    InventTeigiStack.stSetBuffer(storage, count);
    InventManagePt = this;
    InventTeigiStack.lock = 0;
    CScriptInterpreter reader;
    reader.SetTag(invent_teigi_func);
    reader.SetScript(script, size);
    reader.Run();
    return true;
}
