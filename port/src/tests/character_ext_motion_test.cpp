#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "character.hpp"
#include "mg_texture.hpp"

namespace {

constexpr std::size_t kImagesOffset = 0x2C4;
constexpr unsigned char kPoison = 0xA5;

}

TEST(CharacterExt, DeleteExtMotionClearsImageSlotsAndIgnoresStrideArithmetic) {
    CCharacter2 *character = new CCharacter2();

    mgIMG_FILE_HEADER archives[CHARA_IMAGE_MAX] = {};
    for (int j = 0; j < CHARA_IMAGE_MAX; j++) {
        std::memcpy(archives[j].tag, "IM3", 3);
        archives[j].num3 = 0;
    }

    for (int j = 0; j < CHARA_IMAGE_MAX; j++) {
        character->images[j] = nullptr;
    }
    for (int j = 1; j < CHARA_IMAGE_MAX; j++) {
        character->images[j] = &archives[j];
    }

    unsigned char *object = reinterpret_cast<unsigned char *>(character);
    const std::uintptr_t images_begin = reinterpret_cast<std::uintptr_t>(&character->images[0]);
    const std::uintptr_t images_end = reinterpret_cast<std::uintptr_t>(&character->images[CHARA_IMAGE_MAX]);

    std::vector<unsigned char *> decoys;
    for (int j = 1; j < CHARA_IMAGE_MAX; j++) {
        unsigned char *slot = object + kImagesOffset + 4 * j;
        for (std::size_t b = 0; b < sizeof(void *); b++) {
            unsigned char *byte = slot + b;
            const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(byte);
            EXPECT_TRUE(address < images_begin || address >= images_end);
            *byte = kPoison;
            decoys.push_back(byte);
        }
    }

    character->DeleteExtMotion();

    for (int j = 0; j < CHARA_IMAGE_MAX; j++) {
        EXPECT_EQ(character->images[j], nullptr) << "images[" << j << "] was not cleared";
    }
    for (unsigned char *byte : decoys) {
        EXPECT_EQ(*byte, kPoison);
    }

    delete character;
}
