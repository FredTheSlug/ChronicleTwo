#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "platform/texture_catalog.hpp"

namespace datapacks {

/**
 * Holds physical RGBA canvases while the catalog retains the game's logical dimensions.
 */
struct DecodedTexture {
    std::vector<std::vector<uint8_t>> levels; /**< RGBA mip canvases, largest first. */
    uint32_t width = 0; /**< Padded physical width of the base level. */
    uint32_t height = 0; /**< Padded physical height of the base level. */
};

/**
 * Loads the sibling datapacks/datapacks.json; later load_order folders take precedence.
 */
void Load(const std::filesystem::path &port_data_root);

/**
 * Clears all configured pack roots and their file indexes.
 */
void Clear();

/**
 * Finds a regular replacement for a relative game-data path, ignoring ASCII case.
 */
std::optional<std::filesystem::path> Resolve(const std::filesystem::path &relative);

/**
 * Decodes a catalog variant through PNG overlays, falling back to valid lower-priority assets.
 */
bool Decode(const texture_catalog::Entry &entry, std::size_t variant, DecodedTexture &decoded, std::string &error);

} // namespace datapacks
