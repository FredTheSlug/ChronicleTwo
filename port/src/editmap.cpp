#include "common.h"

#include <cstring>
#include <string>

#include "editinfo.hpp"
#include "editmap.hpp"
#include "editparts.hpp"
#include "mapparts.hpp"
#include "mdslist.hpp"
#include "scene_documents.hpp"
#include "stubs/editmap.hpp"

PORT_OVERRIDE int CEditMap::GetePlaceIDList(int *output, int capacity) {
    int count = 0;
    for (int index = 0; index < edit_parts_max && count < capacity; ++index) {
        if (edit_parts[index].name[0] != '\0') {
            output[count++] = index;
        }
    }
    return count;
}

namespace {

/**
 * Builds an editor part table as FIX_EPARTS_START or INIT_EPARTS_START and their rows do.
 */
ePlaceData *PlaceTable(const SceneEditTable &table, mgCMemory *stack) {
    if (table.count <= 0) {
        return nullptr;
    }
    const u32 size = table.count * sizeof(ePlaceData);
    const int quadwords = static_cast<int>((size & 0xF) != 0 ? (size >> 4) + 1 : size >> 4);
    auto     *places = new (stack->Alloc(quadwords + 2)) ePlaceData[table.count];
    if (places == nullptr) {
        return nullptr;
    }
    int index = 0;
    for (const auto &part : table.parts) {
        if (index >= table.count) {
            break;
        }
        ePlaceData *entry = &places[index++];
        entry->id = part.id;
        std::memcpy(entry->position, part.position.data(), sizeof(entry->position));
        entry->angle = part.angle;
    }
    return places;
}

/**
 * Makes a hidden river, mask or water piece of a model, as the river tags do.
 */
CMapPiece *RiverPiece(CEditMap &map, const SceneText &model, mgCMemory *stack, int attributes, const mgCFrameAttr &attr) {
    auto       *piece = new (stack->Alloc(0xD)) CMapPiece;
    std::string name = model.value_or(std::string{});
    CMdsInfo   *mds = map.SearchMDS(model ? name.data() : nullptr);
    if (mds != nullptr) {
        piece->AssignMds(mds);
        if (piece->frame != nullptr) {
            mgCFrameAttr copy = attr;
            piece->frame->SetAttrParam(copy, 1, attributes);
        }
    }
    return piece;
}

} // namespace

void PortApplyEditMapConfig(CEditMap *map, char *script, int /*size*/, mgCMemory *stack) {
    const auto config = ReadSceneConfig(script);
    if (!config || !config->georama) {
        return;
    }
    const auto &georama = *config->georama;
    if (georama.fixed_parts) {
        if (ePlaceData *table = PlaceTable(*georama.fixed_parts, stack)) {
            map->info_mngr.SeteFixPartsTable(table, georama.fixed_parts->count);
        }
    }
    mgCFrameAttr hidden;
    hidden.obj_alpha = 0.0f;
    hidden.clip_enable = 1;
    hidden.alpha_ref = 0;
    hidden.z_write = MG_ZBUF_NO_WRITE;
    const int hidden_attributes = MG_FRAME_ATTR_ALPHA_REF | MG_FRAME_ATTR_Z_WRITE | MG_FRAME_ATTR_CLIP | MG_FRAME_ATTR_OBJ_ALPHA;
    for (const auto &[index, parts, model] : georama.river_parts) {
        if (index < 0 || index >= EDIT_MAP_RIVER_PARTS_MAX) {
            continue;
        }
        std::string parts_name = parts.value_or(std::string{});
        map->river_parts[index] = map->GetParts(parts ? parts_name.data() : nullptr);
        map->river_piece[index] = RiverPiece(*map, model, stack, hidden_attributes, hidden);
    }
    for (const auto &model : georama.water_models) {
        mgCFrameAttr water;
        water.z_write = MG_ZBUF_NO_WRITE;
        water.clip_enable = 1;
        map->water_piece = RiverPiece(*map, model, stack, MG_FRAME_ATTR_Z_WRITE | MG_FRAME_ATTR_CLIP, water);
    }
    for (const auto &[index, model] : georama.mask_parts) {
        if (index < 0 || index >= EDIT_MAP_MASK_PIECE_MAX) {
            continue;
        }
        map->mask_piece[index] = RiverPiece(*map, model, stack, hidden_attributes, hidden);
    }
    if (georama.initial_parts) {
        if (ePlaceData *table = PlaceTable(*georama.initial_parts, stack)) {
            map->info_mngr.init_parts = table;
            map->info_mngr.init_parts_num = georama.initial_parts->count;
        }
    }
}
