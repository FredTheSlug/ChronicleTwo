// nlohmann must precede the game headers, whose macros it would otherwise see.
// clang-format off
#include "platform/native_config.hpp"
// clang-format on

#include "common.h"

#include <cstdio>
#include <cstring>
#include <print>
#include <string>
#include <vector>

#include "dataread.hpp"
#include "gamedata.hpp"
#include "mainloop.hpp"
#include "maintex.hpp"
#include "menucommon.hpp"
#include "mg_memory.hpp"
#include "mg_texture.hpp"
#include "mglib.hpp"
#include "monster.hpp"
#include "platform/model_assets.hpp"
#include "snd_mngr.hpp"
#include "sound_port.hpp"
#include "swordeffect.hpp"

PORT_OVERRIDE int CMonsterMan::LoadReferMonsterFile(int id, BASE_MONSTER_TBL *table, mgCMemory *memory) {
    const int slot = SearchReferBlock();
    if (slot < 0 || !table || !memory) {
        return false;
    }
    auto             &entry = refer[slot];
    const std::string model_path = "dungeon/monster/" + std::string(table->model) + ".gltf";
    const int         texture_block = slot + MONSTER_TEXB_REFER;
    model_assets::DeleteCharacterTextures(entry.chara, 0, CHARA_IMAGE_MAX - 1);
    mgTexManager.DeleteBlock(texture_block);
    std::strcpy(mgTexManager.name_suffix, "_m");
    entry.chara.Initialize();
    model_assets::CharacterLoadOptions options{.model_memory = memory, .motion_memory = memory, .image_memory = memory, .textures = &mgTexManager, .parent = nullptr, .texture_block = texture_block};
    options.outline = std::strcmp(table->model, "b11a") != 0 && std::strcmp(table->model, "b05a") != 0 && std::strcmp(table->model, "b07a") != 0 && std::strcmp(table->model, "b02a") != 0;
    std::string error;
    const bool  loaded = model_assets::LoadCharacter(entry.chara, model_path, table->model, options, error);
    mgTexManager.name_suffix[0] = 0;
    if (!loaded) {
        std::println(stderr, "monster: {}", error);
        return false;
    }
    entry.chara.tbl = table;
    int sound = -1;
    if (table->sound_no > 0) {
        char path[64];
        std::snprintf(path, sizeof(path), "snd2/mon/en_%03d.sound.json", table->sound_no);
        sound = static_cast<int>(SoundLoadBundlePath(SND_PORT_ENEMY, path, memory));
    }
    entry.chara.sound_info.se_bank = sound;
    for (int index = 0; index < table->sw_effect_num; ++index) {
        entry.chara.sword_effect[index] = new (memory->Alloc(0xC)) CSWordAfterEffect;
        entry.chara.sword_effect[index]->Initialize(memory, 0xC, 8);
        entry.chara.sword_effect[index]->SetTexture(0x4A, TEX_SystemEffectSw, 0, 0x20, 0x40, 0x20);
    }
    std::string script_path = "dungeon/monster/" + std::string(table->script) + ".stb";
    int         size = 0;
    if (!LoadFile2(script_path.data(), nullptr, &size, LOAD_FILE_SIZE) || size <= 0) {
        return false;
    }
    const int sectors = size / 2048 + (size % 2048 != 0);
    entry.script = reinterpret_cast<char *>(memory->stAlloc64(sectors * 128));
    if (!entry.script || !LoadFile2(script_path.data(), entry.script, nullptr, LOAD_FILE_READ)) {
        return false;
    }
    entry.id = id;
    return true;
}

namespace {

/**
 * One monster name of a monster_names document.
 */
struct MonsterName {
    int                        id;   /**< Monster kind the name belongs to. */
    std::optional<std::string> name; /**< CP932 name, or nothing for an unreadable operand. */
};

/**
 * Size of the buffer retail converts a monster name into.
 */
constexpr std::size_t kMonsterNameBuffer = 0x80;

/**
 * Parses a monster_names document.
 */
std::vector<MonsterName> ParseMonsterNames(const native_config::Json &document) {
    using namespace native_config;
    std::vector<MonsterName> names;
    for (const auto &entry : Array(document, "names")) {
        auto name = OptionalText(entry, "name");
        // Retail converts the name into a 0x80-byte buffer; a longer one would overrun it.
        if (name && name->size() >= kMonsterNameBuffer) {
            throw std::runtime_error("monster name is too long");
        }
        names.push_back({.id = Int(entry, "id"), .name = std::move(name)});
    }
    return names;
}

/**
 * Stores one monster name in its monster's table entry, as the NAME tag does.
 */
void ApplyMonsterName(const MonsterName &entry) {
    BASE_MONSTER_TBL *monster = GetMonsterTable(entry.id);
    // Retail reads through a missing string operand; the port skips the entry.
    if (monster == nullptr || !entry.name) {
        return;
    }

    std::string text = *entry.name;
    char        name[kMonsterNameBuffer] = {};
    if (LanguageCode >= LANG_FRENCH && LanguageCode <= LANG_SPANISH) {
        ConvertFontCode(text.data(), name);
    } else {
        std::strcpy(name, text.c_str());
    }

    if (std::strlen(name) > sizeof(monster->name) - 1) {
        return;
    }
    std::strcpy(monster->name, name);
}

} // namespace

PORT_OVERRIDE void LoadMonsterLanguage(int language) {
    const std::string path = "dungeon/cfg_file/mosdata" + std::to_string(language) + ".cfg.json";
    if (const auto names = native_config::Read<std::vector<MonsterName> >(path, "monster_names", ParseMonsterNames)) {
        for (const auto &entry : *names) {
            ApplyMonsterName(entry);
        }
    }
}
