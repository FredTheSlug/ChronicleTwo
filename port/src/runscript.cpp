#include "common.h"

#include "runscript.hpp"

PORT_OVERRIDE int CRunScript::check_program(int no) {
    // Town characters can be reset without an action program, as run() also allows.
    if (prog == nullptr) {
        return 0;
    }
    const auto *entries = reinterpret_cast<const RS_PROGDATA *>(reinterpret_cast<const char *>(prog) + prog->prog);
    for (int i = 0; i < prog->prog_num; ++i) {
        if (entries[i].no == no) {
            return 1;
        }
    }
    return 0;
}
