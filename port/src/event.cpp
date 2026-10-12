#include "common.h"

#include <array>

#include "event.hpp"
#include "event_func.hpp"
#include "runscript.hpp"
#include "stubs/event.hpp"

PORT_OVERRIDE void SetEventScript(char *program, char *, mgCMemory *memory) {
    // The one scene interpreter owns its host runtime arrays. The file arena keeps
    // the unchanged disc bytes, whose retail budget does not include wider pointers.
    static std::array<RS_STACKDATA, 128> values;
    static std::array<RS_CALLDATA, 512> calls;
    if (program == nullptr || memory == nullptr) {
        EventScript.DeleteProgram();
        return;
    }
    EventScript.load(reinterpret_cast<RS_PROG_HEADER *>(program), values.data(), values.size(),
                              calls.data(), calls.size());
    SetEventFunc(&EventScript);
}
