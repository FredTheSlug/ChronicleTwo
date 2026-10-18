#include "common.h"

#include "runscript.hpp"
#include "script_guards.hpp"

// Retail's printf diagnostics, logged as the game's own (docs/LOGGING.md).
#include "ps2_printf.hpp"

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

// Externals that index fixed tables or copy into fixed fields with script values are checked first, so
// out-of-range arguments are refused like an illegal call instead of reaching past the table.
PORT_OVERRIDE void CRunScript::ext(RS_STACKDATA *command, int arg_count) {
    const int index = command->val.i;
    if (index < 0 || index >= ext_func_num || ext_func_table[index] == nullptr) {
        CtPs2Printf("not found ext %d\n", index);
        return;
    }

    const auto function = ext_func_table[index];
    if (!script_guards::AllowExternal(function, command + 1, arg_count - 1, index)) {
        return;
    }
    if (function(command + 1, arg_count - 1) == 0) {
        CtPs2Printf("illegal function call ext %d\n", command->val.i);
    }
}
