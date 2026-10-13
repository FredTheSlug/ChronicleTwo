#include "common.h"

#include <cstdio>
#include <cstring>

#include "arena_root.hpp"
#include "dataread.hpp"
#include "gamedata.hpp"
#include "mainloop.hpp"
#include "menucommon.hpp"
#include "mg_memory.hpp"
#include "platform/arena.hpp"
#include "scriptinterpreter.hpp"

int _DATACOMINIT(SPI_STACK *, int);
int _DATACOM(SPI_STACK *, int);
int _DATAWEPNUM(SPI_STACK *, int);
int _DATAWEP(SPI_STACK *, int);
int _DATAWEP_ST(SPI_STACK *, int);
int _DATAWEP_ST_L(SPI_STACK *, int);
int _DATAWEP2_ST(SPI_STACK *, int);
int _DATAWEP2_ST_L(SPI_STACK *, int);
int _DATAWEP_SPE(SPI_STACK *, int);
int _DATAWEP_BUILDUP(SPI_STACK *, int);
int _DATAITEMINIT(SPI_STACK *, int);
int _DATAITEM(SPI_STACK *, int);
int _DATAATTACHINIT(SPI_STACK *, int);
int _DATAATTACH_ST(SPI_STACK *, int);
int _DATAATTACH_ST2(SPI_STACK *, int);
int _DATAATTACH_ST_SP(SPI_STACK *, int);
int _DATAROBOINIT(SPI_STACK *, int);
int _DATAROBO_ANALYZE(SPI_STACK *, int);
int _DATAGAURDNUM(SPI_STACK *, int);
int _DATAGAURD(SPI_STACK *, int);
int _DATAFISHINIT(SPI_STACK *, int);
int _DATAFISH(SPI_STACK *, int);
int _MES_SYS(SPI_STACK *, int);
int _MES_SYS_SPECTOL(SPI_STACK *, int);

namespace {
mgCMemory *message_memory;
char message_root;
SPI_TAG_PARAM message_tags[] = {
    {const_cast<char *>("COMINIT"),    _DATACOMINIT     },
    {const_cast<char *>("COM"),        _DATACOM         },
    {const_cast<char *>("WEPNUM"),     _DATAWEPNUM      },
    {const_cast<char *>("WEP"),        _DATAWEP         },
    {const_cast<char *>("WEP_ST"),     _DATAWEP_ST      },
    {const_cast<char *>("WEP_ST_L"),   _DATAWEP_ST_L    },
    {const_cast<char *>("WEP_ST2"),    _DATAWEP2_ST     },
    {const_cast<char *>("WEP_ST2_L"),  _DATAWEP2_ST_L   },
    {const_cast<char *>("WEP_SPE"),    _DATAWEP_SPE     },
    {const_cast<char *>("WEP_BUILD"),  _DATAWEP_BUILDUP },
    {const_cast<char *>("ITEMINIT"),   _DATAITEMINIT    },
    {const_cast<char *>("ITEM"),       _DATAITEM        },
    {const_cast<char *>("AT_INIT"),    _DATAATTACHINIT  },
    {const_cast<char *>("AT_ST"),      _DATAATTACH_ST   },
    {const_cast<char *>("AT_ST2"),     _DATAATTACH_ST2  },
    {const_cast<char *>("AT_ST_SP"),   _DATAATTACH_ST_SP},
    {const_cast<char *>("ROBOINIT"),   _DATAROBOINIT    },
    {const_cast<char *>("RB_PARTS"),   _DATAROBO_ANALYZE},
    {const_cast<char *>("GRDNUM"),     _DATAGAURDNUM    },
    {const_cast<char *>("GRD"),        _DATAGAURD       },
    {const_cast<char *>("FISHINIT"),   _DATAFISHINIT    },
    {const_cast<char *>("FISH"),       _DATAFISH        },
    {const_cast<char *>("MES_SYS"),    _MES_SYS         },
    {const_cast<char *>("MES_SYSSPE"), _MES_SYS_SPECTOL },
    {NULL,         NULL             },
};
}

PORT_OVERRIDE int CGameData::LoadItemSystemMes(int language) {
    // This root is reinitialized on every language load. Cursor rebinding elsewhere
    // must preserve live sound records, so clearing belongs to this owner.
    auto *root = ArenaRoot(&message_root, 0x280, "ItemMessages");
    std::memset(root, 0, 0x2800 * kArenaHeadroom);
    mgCMemory memory;
    memory.stSetBuffer(root, 0x280 * kArenaHeadroom);
    message_memory = &memory;
    alignas(64) u_long128 script_buffer[0x780];
    char path[0x40];
    std::snprintf(path, sizeof(path), "menu/cfg7/comdatmes%d.cfg", language);
    int size = 0;
    if (LoadFile2(path, script_buffer, &size, 0)) {
        CScriptInterpreter interpreter;
        interpreter.SetTag(message_tags);
        interpreter.SetScript(reinterpret_cast<char *>(script_buffer), size);
        interpreter.Run();
    }
    message_memory = nullptr;
    return 1;
}

PORT_OVERRIDE int _MES_SYS(SPI_STACK *args, int) {
    const int item = spiGetStackInt(args++);
    char *text = spiGetStackString(args);
    auto *record = GameItemDataManage.GetCommonData(item);
    if (record != nullptr && message_memory != nullptr) {
        char converted[0x100]{};
        if (LanguageCode >= 2 && LanguageCode < 6) {
            ConvertFontCode(text, converted);
            text = converted;
        }
        record->name = mgCopyString(text, message_memory);
    }
    return 1;
}
