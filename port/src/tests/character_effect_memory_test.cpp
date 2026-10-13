#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "character.hpp"
#include "dataread.hpp"
#include "effect.hpp"
#include "mg_memory.hpp"

namespace {
std::vector<u32> Pack(const char *name, const void *data, std::size_t size,
                      const char *second = nullptr, const void *data2 = nullptr, std::size_t size2 = 0) {
    const std::size_t next = (sizeof(PACK_ENTRY) + size + 15) & ~std::size_t(15);
    const std::size_t last = second ? next + ((sizeof(PACK_ENTRY) + size2 + 15) & ~std::size_t(15)) : next;
    std::vector<u32>  bytes((last + sizeof(PACK_ENTRY) + 3) / 4);
    auto             *entry = reinterpret_cast<PACK_ENTRY *>(bytes.data());
    std::strcpy(entry->name, name);
    entry->offset = sizeof(PACK_ENTRY);
    entry->size = size;
    entry->next = next;
    std::memcpy(reinterpret_cast<char *>(entry) + entry->offset, data, size);
    if (second) {
        entry = reinterpret_cast<PACK_ENTRY *>(reinterpret_cast<char *>(entry) + next);
        std::strcpy(entry->name, second);
        entry->offset = sizeof(PACK_ENTRY);
        entry->size = size2;
        entry->next = last - next;
        std::memcpy(reinterpret_cast<char *>(entry) + entry->offset, data2, size2);
    }
    return bytes;
}
} // namespace

TEST(CharacterEffectMemory, ManagerOffsetDoesNotOverlapParticleStorage) {
    const char            effect[] = "";
    auto                  effects = Pack("effect.txt", effect, sizeof(effect));
    const char            info[] = "EFFECT_START \"effects.pac\";\nEFFECT \"effect.txt\", \"test\", 0, \"\", 2.0, 3.0, 4.0;\n";
    auto                  archive = Pack("info.chr", info, sizeof(info), "effects.pac", effects.data(), effects.size() * 4);
    alignas(64) u_long128 storage[2048]{};
    mgCMemory             memory;
    memory.stSetBuffer(storage, 2048);
    CCharacter2 character;
    character.Initialize();
    character.effect_image_load = 0;
    char name[] = "info.chr";
    character.LoadChrFile(archive.data(), name, &memory, &memory, &memory, 0, nullptr, 0);
    ASSERT_NE(character.effect_list, nullptr);
    auto *manager = character.effect_list->effect;
    ASSERT_NE(manager, nullptr);
    EXPECT_GE(reinterpret_cast<char *>(manager->particle_pool) - reinterpret_cast<char *>(manager),
              static_cast<std::ptrdiff_t>(sizeof(*manager)));
    // The zero-particle pool still owns one quadword; writing it must not touch offset.
    std::memset(manager->particle_pool, 0xA5, 16);
    EXPECT_FLOAT_EQ(manager->offset[0], 2.0f);
    EXPECT_FLOAT_EQ(manager->offset[1], 3.0f);
    EXPECT_FLOAT_EQ(manager->offset[2], 4.0f);
    EXPECT_FLOAT_EQ(manager->offset[3], 1.0f);
}
