#include "common.h"

#include "gamedata.hpp"
#include "menucls1.hpp"

PORT_OVERRIDE int CMenuItemUse::UseItem(CGameDataUsed *item, int kind, void *ptr) {
    CItemUseTarget target{};
    target.target.data = nullptr;
    target.SetPtr(kind, ptr);
    MenuUsedTarget.SetPtr(kind, ptr);
    return UseItem(item, &target);
}
