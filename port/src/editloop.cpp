#include "common.h"

#include "bounded_text.hpp"
#include "editloop.hpp"
#include "mainloop.hpp"
#include "map_port.hpp"
#include "mapselect.hpp"
#include "menu_world.hpp"
#include "scene.hpp"

void UpdateTrBoxFlag(int map_no);
void PortRetailUpdateTrBoxFlag(int map_no) asm("EditLoop_UpdateTrBoxFlag");
int  PortRetailEditDraw() asm("EditLoop_EditDraw");

// Retail's update, after chests that share another chest's opened flag get their own.
PORT_OVERRIDE void UpdateTrBoxFlag(int map_no) {
    CScene *scene = GetMainScene();
    PortSplitTreasureBoxFlags(*scene->GetMap(scene->active_map), bounded_text::View(GetMapName(map_no, nullptr)));
    PortRetailUpdateTrBoxFlag(map_no);
}

PORT_OVERRIDE int EditDraw() {
    const int drawn = PortRetailEditDraw();
    menuworld::AfterWorldDraw();
    return drawn;
}
