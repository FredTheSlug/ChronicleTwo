#pragma once

#include <optional>
#include <cstddef>
#include <string>

// Associates a game-visible file or PACK member pointer with its converted texture source.
void TextureSourceRegister(const void *buffer, std::string source, std::size_t size = 0);
void TextureSourceForget(const void *buffer);
void TextureSourceClear();
std::optional<std::string> TextureSourceLookup(const void *buffer);
void TextureSourceCopy(const void *from, const void *to, std::size_t size);
