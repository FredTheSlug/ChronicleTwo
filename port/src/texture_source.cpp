#include "texture_source.hpp"

#include <cstdint>
#include <unordered_map>

namespace {
struct SourceRange {
    std::string source;
    std::size_t size = 0;
};
std::unordered_map<const void *, SourceRange> sources;
}

void TextureSourceRegister(const void *buffer, std::string source, std::size_t size) {
    if (buffer != nullptr) {
        sources.insert_or_assign(buffer, SourceRange{std::move(source), size});
    }
}

void TextureSourceForget(const void *buffer) { sources.erase(buffer); }

void TextureSourceClear() { sources.clear(); }

std::optional<std::string> TextureSourceLookup(const void *buffer) {
    const auto found = sources.find(buffer);
    if (found != sources.end()) {
        return found->second.source;
    }
    // Some retail wrappers hold a texture at a byte offset instead of a PACK entry. The source
    // remains associated with the owning buffer, so offset handles need no legacy header bytes.
    const auto address = reinterpret_cast<std::uintptr_t>(buffer);
    const SourceRange *best = nullptr;
    std::size_t best_offset = 0;
    for (const auto &[start, range] : sources) {
        const auto base = reinterpret_cast<std::uintptr_t>(start);
        if (range.size && address >= base && address - base < range.size &&
            (!best || address - base < best_offset)) {
            best = &range;
            best_offset = address - base;
        }
    }
    if (!best) {
        return std::nullopt;
    }
    return best->source + "#offset:" + std::to_string(best_offset);
}

void TextureSourceCopy(const void *from, const void *to, std::size_t size) {
    if (auto source = TextureSourceLookup(from)) {
        TextureSourceRegister(to, std::move(*source), size);
    } else {
        TextureSourceForget(to);
    }
}
