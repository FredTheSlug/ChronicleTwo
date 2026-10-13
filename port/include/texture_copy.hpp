#pragma once

#include <cstddef>
#include <cstring>

#include "../src/texture_source.hpp"

inline void *PortTextureSourceCopy(void *destination, const void *source, std::size_t size) {
    void *result = std::memcpy(destination, source, size);
    TextureSourceCopy(source, destination, size);
    return result;
}

// Retail units often move an IMG file from a PACK member into an arena before using it. The port
// carries the catalog source association across these copies; host library units use plain memcpy.
#define memcpy(destination, source, size) PortTextureSourceCopy(destination, source, size)
