#include "platform/datapacks.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

namespace datapacks {
namespace {

/**
 * Stores one pack's root and case-insensitive asset index.
 */
struct Pack {
    std::filesystem::path                                  root;  /**< Directory containing the pack. */
    std::unordered_map<std::string, std::filesystem::path> files; /**< Relative asset keys mapped to on-disk paths. */
};

/** Canonical converted-data directory used to locate PNG overlays. */
std::filesystem::path retail_root;
/** Enabled packs in increasing priority order. */
std::vector<Pack> packs;
/** Largest accepted physical texture extent on either axis. */
constexpr uint32_t kMaxDimension = 16384;
/** Maximum RGBA storage occupied by one decoded mip chain. */
constexpr uint64_t kMaxDecodedBytes = 256ull * 1024 * 1024;
/** Maximum compressed PNG size accepted before reading its contents. */
constexpr uint64_t kMaxFileBytes = 256ull * 1024 * 1024;
/** RGBA storage the decode cache keeps, least recently used first out. */
constexpr std::size_t kDecodeCacheBytes = 512ull * 1024 * 1024;

/** Largest number of Prefetch worker threads. */
constexpr unsigned kMaxPrefetchWorkers = 8;

/**
 * One decoded catalog variant, kept so entering the same texture again copies it instead of re-reading
 * and re-decoding its PNGs. Upscaled packs make that decode slow enough to show: the dungeon's quick
 * character change re-enters the whole item icon sheet in a single frame.
 */
struct CachedDecode {
    std::shared_ptr<const DecodedTexture> texture;   /**< Immutable decoded pixels shared with readers. */
    std::size_t                           bytes = 0; /**< RGBA bytes counted against the cache budget. */
    std::list<std::string>::iterator      use;       /**< Position of this key in the recency list. */
};

/** Guards the decode cache, its recency list and byte total. */
std::mutex decode_mutex;
/** Cached decodes keyed by identity. */
std::unordered_map<std::string, CachedDecode> decode_cache;
/** Cache keys, most recently used first. */
std::list<std::string> decode_uses;
/** RGBA bytes currently held by the decode cache. */
std::size_t decode_cache_bytes = 0;

/**
 * Size and write time of one source file as ImageDecodeKey names it.
 */
struct Stamp {
    std::uintmax_t size = 0; /**< File size, or zero when unreadable. */
    long long      time = 0; /**< Write-time ticks, or zero when unreadable. */
};

/** Guards the path memos and the stamp memo. */
std::mutex memo_mutex;
/** Candidates results per relative path for the current pack configuration. */
std::unordered_map<std::string, std::vector<std::filesystem::path> > candidates_memo;
/** DecodeCandidates results per mip path for the current pack configuration. */
std::unordered_map<std::string, std::vector<std::filesystem::path> > decode_candidates_memo;
/** Source-file stamps per path while a StampScope is alive. */
std::unordered_map<std::string, Stamp> stamp_memo;
/** Number of live StampScope objects. */
unsigned stamp_scopes = 0;

/**
 * Accumulates named load counters and prints them to stderr at exit.
 */
struct TraceCounters {
    std::mutex                                         mutex;  /**< Guards totals. */
    std::map<std::string, std::uint64_t, std::less<> > totals; /**< Counter totals sorted by name. */

    ~TraceCounters() {
        const std::lock_guard lock(mutex);
        for (const auto &[name, total] : totals) {
            std::fprintf(stderr, "texture-trace: %s = %llu\n", name.c_str(), static_cast<unsigned long long>(total));
        }
    }
};

/**
 * Returns the process-wide trace counters, constructed on first use.
 */
TraceCounters &Counters() {
    static TraceCounters counters;
    return counters;
}

void ClearDecodeCache() {
    const std::lock_guard lock(decode_mutex);
    decode_cache.clear();
    decode_uses.clear();
    decode_cache_bytes = 0;
}

/**
 * Forgets every path memo so the next lookups observe the current pack configuration.
 */
void ClearMemos() {
    const std::lock_guard lock(memo_mutex);
    candidates_memo.clear();
    decode_candidates_memo.clear();
    stamp_memo.clear();
}

/**
 * Returns a cached decode and marks it most recently used, or null when it is not cached.
 */
std::shared_ptr<const DecodedTexture> CacheFind(const std::string &key) {
    const std::lock_guard lock(decode_mutex);
    const auto            found = decode_cache.find(key);
    if (found == decode_cache.end()) {
        return nullptr;
    }
    decode_uses.splice(decode_uses.begin(), decode_uses, found->second.use);
    return found->second.texture;
}

/**
 * Reports whether a decode is cached without changing its recency.
 */
bool CacheContains(const std::string &key) {
    const std::lock_guard lock(decode_mutex);
    return decode_cache.contains(key);
}

/**
 * Returns the RGBA bytes a decode would occupy in the cache, or zero when it is empty or too large to keep.
 */
std::size_t CacheBytes(const DecodedTexture &texture) {
    std::size_t bytes = 0;
    for (const auto &level : texture.levels) {
        bytes += level.size();
    }
    return bytes > kDecodeCacheBytes / 4 ? 0 : bytes;
}

/**
 * Stores a decode under its identity, evicting least recently used decodes to stay within the budget.
 */
void CacheInsert(const std::string &key, std::shared_ptr<const DecodedTexture> texture) {
    const std::size_t bytes = CacheBytes(*texture);
    if (bytes == 0) {
        return;
    }
    std::uint64_t         evictions = 0;
    const std::lock_guard lock(decode_mutex);
    if (const auto found = decode_cache.find(key); found != decode_cache.end()) {
        decode_uses.splice(decode_uses.begin(), decode_uses, found->second.use);
        return;
    }
    while (decode_cache_bytes + bytes > kDecodeCacheBytes && !decode_uses.empty()) {
        const auto oldest = decode_cache.find(decode_uses.back());
        decode_cache_bytes -= oldest->second.bytes;
        decode_cache.erase(oldest);
        decode_uses.pop_back();
        ++evictions;
    }
    decode_uses.push_front(key);
    decode_cache.emplace(key, CachedDecode{std::move(texture), bytes, decode_uses.begin()});
    decode_cache_bytes += bytes;
    Trace("decode cache inserts");
    if (evictions) {
        Trace("decode cache evictions", evictions);
    }
}

/**
 * Returns a path's size and write time with one status query, memoised while a StampScope is alive.
 */
Stamp FileStamp(const std::string &name, const std::filesystem::path &path) {
    {
        const std::lock_guard lock(memo_mutex);
        if (stamp_scopes) {
            if (const auto found = stamp_memo.find(name); found != stamp_memo.end()) {
                Trace("stamp memo hits");
                return found->second;
            }
        }
    }
    Stamp                                  stamp;
    std::error_code                        error;
    const std::filesystem::directory_entry entry(path, error);
    if (!error) {
        const auto size = entry.file_size(error);
        const auto time = error ? std::filesystem::file_time_type{} : entry.last_write_time(error);
        if (!error) {
            stamp = {size, static_cast<long long>(time.time_since_epoch().count())};
        }
    }
    const std::lock_guard lock(memo_mutex);
    if (stamp_scopes) {
        Trace("stamp memo misses");
        stamp_memo.insert_or_assign(name, stamp);
    }
    return stamp;
}

/**
 * Converts a filesystem path to UTF-8 for keys and diagnostics.
 */
std::string Display(const std::filesystem::path &path) {
    const auto utf8 = path.generic_u8string();
    return std::string(utf8.begin(), utf8.end());
}

/**
 * Builds an ASCII case-insensitive asset lookup key.
 */
std::string Key(const std::filesystem::path &path) {
    std::string key = Display(path);
    for (char &character : key) {
        if (character >= 'A' && character <= 'Z') {
            character += 'a' - 'A';
        }
    }
    return key;
}

/**
 * Accepts relative asset paths without traversal or alternate-root syntax.
 */
bool Relative(const std::filesystem::path &path) {
    const auto text = Display(path);
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory() ||
        text.find('\\') != std::string::npos || text.find(':') != std::string::npos ||
        text.find('\0') != std::string::npos) {
        return false;
    }
    for (const auto &part : path) {
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
    }
    return true;
}

/**
 * Checks that a canonical path is a strict descendant of a root.
 */
bool Within(const std::filesystem::path &root, const std::filesystem::path &path) {
    auto base = root.begin();
    auto candidate = path.begin();
    for (; base != root.end(); ++base, ++candidate) {
        if (candidate == path.end()) {
            return false;
        }
#ifdef _WIN32
        if (Key(*base) != Key(*candidate)) {
            return false;
        }
#else
        if (*base != *candidate) {
            return false;
        }
#endif
    }
    return candidate != path.end();
}

/**
 * Reports whether a path names an existing regular file.
 */
bool RegularFile(const std::filesystem::path &path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

/**
 * Looks up a pack asset in the index built when the pack was loaded.
 */
std::optional<std::filesystem::path> PackFile(const Pack &pack, const std::filesystem::path &relative) {
    const auto found = pack.files.find(Key(relative));
    if (found == pack.files.end()) {
        return std::nullopt;
    }
    return found->second;
}

/**
 * Reports a rejected configuration or asset with its path.
 */
void Diagnostic(const std::filesystem::path &path, const std::string &message) {
    std::fprintf(stderr, "datapacks: %s: %s\n", Display(path).c_str(), message.c_str());
}

/**
 * Rounds a validated logical extent up to the retail GS canvas size.
 */
uint32_t PowerOfTwo(uint32_t value) {
    uint32_t result = 1;
    while (result < value) {
        result <<= 1;
    }
    return result;
}

/**
 * Checks physical texture dimensions and cumulative RGBA mip storage.
 */
bool ChainFits(uint32_t width, uint32_t height, std::size_t count) {
    if (!width || !height || width > kMaxDimension || height > kMaxDimension) {
        return false;
    }
    uint64_t total = 0;
    for (std::size_t mip = 0; mip < count; ++mip) {
        total += uint64_t{std::max(1u, width >> mip)} * std::max(1u, height >> mip) * 4;
    }
    return total <= kMaxDecodedBytes && total <= std::numeric_limits<std::size_t>::max();
}

/**
 * Preflights PNG dimensions and file size before decoding any PNG format to RGBA.
 */
bool ReadPng(const std::filesystem::path &path, uint32_t &width, uint32_t &height,
             std::vector<uint8_t> &rgba, std::string &error) {
    std::vector<uint8_t> bytes;
    if (!ReadFileBytes(path, bytes, kMaxFileBytes) || bytes.size() < 33) {
        error = "PNG is unreadable or exceeds the file-size limit";
        return false;
    }
    Trace("png files read");
    Trace("png bytes read", bytes.size());
    const std::span<const uint8_t, 33> header(bytes.data(), 33);
    constexpr std::array<unsigned char, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    if (!std::equal(signature.begin(), signature.end(), header.begin()) ||
        header[8] || header[9] || header[10] || header[11] != 13 ||
        !std::equal(header.begin() + 12, header.begin() + 16, "IHDR") ||
        header[26] || header[27] || header[28] > 1) {
        error = "expected a PNG image";
        return false;
    }
    const auto word = [&](std::size_t offset) {
        return (uint32_t{header[offset]} << 24) | (uint32_t{header[offset + 1]} << 16) |
               (uint32_t{header[offset + 2]} << 8) | header[offset + 3];
    };
    width = word(16);
    height = word(20);
    if (!ChainFits(width, height, 1)) {
        error = "PNG dimensions exceed the texture or memory limit";
        return false;
    }
    int            decoded_width = 0, decoded_height = 0, channels = 0;
    unsigned char *pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                                  &decoded_width, &decoded_height, &channels, 4);
    if (!pixels || decoded_width != static_cast<int>(width) || decoded_height != static_cast<int>(height)) {
        stbi_image_free(pixels);
        error = "cannot decode PNG";
        return false;
    }
    rgba.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4);
    stbi_image_free(pixels);
    Trace("stbi decodes");
    Trace("stbi decoded bytes", rgba.size());
    return true;
}

/**
 * Copies logical image pixels into a transparent physical canvas.
 */
std::vector<uint8_t> Pad(std::vector<uint8_t> pixels, uint32_t width, uint32_t height,
                         uint32_t canvas_width, uint32_t canvas_height) {
    if (width == canvas_width && height == canvas_height) {
        return pixels;
    }
    std::vector<uint8_t> canvas(static_cast<std::size_t>(canvas_width) * canvas_height * 4);
    for (uint32_t row = 0; row < height; ++row) {
        std::copy_n(pixels.begin() + static_cast<std::size_t>(row) * width * 4, width * 4,
                    canvas.begin() + static_cast<std::size_t>(row) * canvas_width * 4);
    }
    return canvas;
}

/**
 * Builds the next physical mip canvas with a two-by-two box filter.
 */
std::vector<uint8_t> Downsample(const std::vector<uint8_t> &pixels, uint32_t width, uint32_t height) {
    const uint32_t       next_width = std::max(1u, width / 2), next_height = std::max(1u, height / 2);
    std::vector<uint8_t> next(static_cast<std::size_t>(next_width) * next_height * 4);
    for (uint32_t y = 0; y < next_height; ++y) {
        for (uint32_t x = 0; x < next_width; ++x) {
            for (uint32_t channel = 0; channel < 4; ++channel) {
                unsigned sum = 0;
                for (uint32_t dy = 0; dy < 2; ++dy) {
                    for (uint32_t dx = 0; dx < 2; ++dx) {
                        const auto offset = (static_cast<std::size_t>(std::min(height - 1, y * 2 + dy)) * width +
                                             std::min(width - 1, x * 2 + dx)) *
                                                4 +
                                            channel;
                        sum += pixels[offset];
                    }
                }
                next[(static_cast<std::size_t>(y) * next_width + x) * 4 + channel] = (sum + 2) / 4;
            }
        }
    }
    return next;
}

} // namespace

/**
 * Clears all configured pack roots and their asset indexes.
 */
void Clear() {
    ClearDecodeCache();
    ClearMemos();
    packs.clear();
    retail_root.clear();
}

/**
 * Loads and validates ordered pack folders beside the converted-data root.
 */
void Load(const std::filesystem::path &port_data_root) {
    Clear();
    std::error_code error;
    retail_root = std::filesystem::weakly_canonical(port_data_root, error);
    if (error) {
        Diagnostic(port_data_root, "cannot establish the converted-data root; packs disabled");
        return;
    }
    const auto directory = retail_root.parent_path() / "datapacks";
    const auto config = directory / "datapacks.json";
    if (!std::filesystem::exists(config, error) && !error) {
        return;
    }
    try {
        std::vector<uint8_t> text;
        if (error || !std::filesystem::is_regular_file(config) || !ReadFileBytes(config, text, 1024 * 1024)) {
            throw std::runtime_error("configuration is unreadable or exceeds 1 MiB");
        }
        const auto root = nlohmann::json::parse(text);
        if (!root.is_object() || !root.contains("schema_version") || !root["schema_version"].is_number_integer() ||
            root["schema_version"] != 1 || !root.contains("load_order") || !root["load_order"].is_array()) {
            throw std::runtime_error("expected schema_version 1 and a load_order array");
        }
        std::vector<Pack> configured;
        for (const auto &item : root["load_order"]) {
            if (!item.is_string()) {
                throw std::runtime_error("load_order entries must be folder names");
            }
            const auto                  folder = item.get<std::string>();
            const std::filesystem::path name(std::u8string(folder.begin(), folder.end()));
            if (!Relative(name) || std::distance(name.begin(), name.end()) != 1) {
                throw std::runtime_error("load_order contains an unsafe folder name");
            }
            Pack pack;
            pack.root = directory / name;
            if (!std::filesystem::is_directory(pack.root)) {
                Diagnostic(directory / name, "pack folder is missing; skipped");
                continue;
            }
            for (const auto &file : std::filesystem::recursive_directory_iterator(pack.root)) {
                if (!file.is_regular_file()) {
                    continue;
                }
                const auto relative = file.path().lexically_relative(pack.root);
                const auto key = Key(relative);
                if (!pack.files.emplace(key, file.path()).second) {
                    throw std::runtime_error("pack contains paths differing only by ASCII case");
                }
            }
            configured.push_back(std::move(pack));
        }
        packs = std::move(configured);
    } catch (const std::exception &exception) {
        packs.clear();
        Diagnostic(config, std::string(exception.what()) + "; packs disabled");
    }
}

/**
 * Resolves a relative game-data asset from the highest-priority valid pack.
 */
std::optional<std::filesystem::path> Resolve(const std::filesystem::path &relative) {
    if (!Relative(relative)) {
        return std::nullopt;
    }
    for (auto pack = packs.rbegin(); pack != packs.rend(); ++pack) {
        if (auto path = PackFile(*pack, relative)) {
            return path;
        }
    }
    return std::nullopt;
}

const std::filesystem::path &Root() {
    return retail_root;
}

std::vector<std::filesystem::path> Candidates(const std::filesystem::path &relative) {
    std::vector<std::filesystem::path> candidates;
    if (!Relative(relative)) {
        return candidates;
    }
    const auto name = Display(relative);
    {
        const std::lock_guard lock(memo_mutex);
        if (const auto found = candidates_memo.find(name); found != candidates_memo.end()) {
            Trace("candidates memo hits");
            return found->second;
        }
    }
    Trace("candidates memo misses");
    for (auto pack = packs.rbegin(); pack != packs.rend(); ++pack) {
        if (auto path = PackFile(*pack, relative)) {
            candidates.push_back(*path);
        }
    }
    if (!retail_root.empty()) {
        const auto path = retail_root / relative;
        if (RegularFile(path)) {
            candidates.push_back(path);
        }
    }
    const std::lock_guard lock(memo_mutex);
    candidates_memo.insert_or_assign(name, candidates);
    return candidates;
}

std::optional<std::filesystem::path> Asset(const std::filesystem::path &relative) {
    auto candidates = Candidates(relative);
    if (candidates.empty()) {
        return std::nullopt;
    }
    return candidates.front();
}

std::vector<std::filesystem::path> DirectoryMembers(const std::filesystem::path &relative) {
    if (!Relative(relative)) {
        return {};
    }
    std::unordered_map<std::string, std::filesystem::path> members;
    auto                                                   collect = [&](const std::filesystem::path &root) {
        const auto      directory = root / relative;
        std::error_code error;
        if (!std::filesystem::is_directory(directory, error)) {
            return;
        }
        for (const auto &entry : std::filesystem::directory_iterator(directory, error)) {
            if (error) {
                break;
            }
            auto filename = relative / entry.path().filename();
            if (entry.is_regular_file(error)) {
                members.insert_or_assign(Key(entry.path().filename()), filename);
            } else if (entry.is_directory(error) && RegularFile(entry.path() / "pack.json")) {
                members.insert_or_assign(Key(entry.path().filename()), filename);
            }
        }
    };
    if (!retail_root.empty()) {
        collect(retail_root);
    }
    for (const auto &pack : packs) {
        collect(pack.root);
    }
    std::vector<std::filesystem::path> result;
    for (const auto &[key, path] : members) {
        result.push_back(path);
    }
    std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) { return Key(left) < Key(right); });
    return result;
}

/**
 * Decodes a catalog texture through PNG overlays into padded physical RGBA mip canvases.
 */
namespace {

/**
 * Lists the files that can supply one mip, highest-priority pack first and the converted original last.
 */
std::vector<std::filesystem::path> DecodeCandidates(const std::filesystem::path &level) {
    const auto name = Display(level);
    {
        const std::lock_guard lock(memo_mutex);
        if (const auto found = decode_candidates_memo.find(name); found != decode_candidates_memo.end()) {
            Trace("decode candidates memo hits");
            return found->second;
        }
    }
    Trace("decode candidates memo misses");
    std::vector<std::filesystem::path> result;
    std::error_code                    path_error;
    const auto                         canonical = std::filesystem::weakly_canonical(level, path_error);
    if (!path_error && !retail_root.empty() && Within(retail_root, canonical)) {
        const auto relative = canonical.lexically_relative(retail_root);
        for (auto pack = packs.rbegin(); pack != packs.rend(); ++pack) {
            if (auto path = PackFile(*pack, relative)) {
                result.push_back(*path);
            }
        }
    }
    result.push_back(level);
    const std::lock_guard lock(memo_mutex);
    decode_candidates_memo.insert_or_assign(name, result);
    return result;
}

/**
 * Names a variant's decode by every file that can supply it, with each file's size and write time, so a
 * replaced or edited PNG makes a new key.
 */
std::string DecodeKey(const texture_catalog::Entry &entry, std::size_t variant_index) {
    std::vector<std::filesystem::path> sources;
    for (const auto &level : entry.variants[variant_index].levels) {
        const auto candidates = DecodeCandidates(level);
        sources.insert(sources.end(), candidates.begin(), candidates.end());
    }
    return ImageDecodeKey("texture|" + std::to_string(variant_index) + '|' + std::to_string(entry.width) + 'x' + std::to_string(entry.height), sources);
}

bool DecodeUncached(const texture_catalog::Entry &entry, std::size_t variant_index, DecodedTexture &decoded,
                    std::string &error) {
    decoded = {};
    if (entry.type != "texture" || entry.bpp != 32 || entry.width <= 0 || entry.height <= 0 ||
        entry.width > static_cast<int>(kMaxDimension) || entry.height > static_cast<int>(kMaxDimension) ||
        variant_index >= entry.variants.size()) {
        error = "invalid texture entry or RGBA variant";
        return false;
    }
    const auto &variant = entry.variants[variant_index];
    if (variant.levels.size() > 4 || (!entry.no_image && variant.levels.empty())) {
        error = "missing or invalid PNG mip chain";
        return false;
    }
    const uint32_t retail_width = PowerOfTwo(entry.width), retail_height = PowerOfTwo(entry.height);
    decoded.width = retail_width;
    decoded.height = retail_height;
    if (variant.levels.empty()) {
        error.clear();
        return true;
    }
    const auto candidates = [&](std::size_t mip) { return DecodeCandidates(variant.levels[mip]); };
    uint32_t   scale = 0;
    for (std::size_t mip = 0; mip < variant.levels.size(); ++mip) {
        bool selected = false;
        for (const auto &path : candidates(mip)) {
            uint32_t             width = 0, height = 0;
            std::vector<uint8_t> pixels;
            std::string          reason;
            if (!ReadPng(path, width, height, pixels, reason)) {
                Diagnostic(path, reason + "; trying lower-priority asset");
                continue;
            }
            if (mip == 0) {
                const auto     logical_width = static_cast<uint32_t>(entry.width);
                const auto     logical_height = static_cast<uint32_t>(entry.height);
                const uint32_t candidate_scale = width / logical_width;
                if (width % logical_width || height % logical_height || candidate_scale == 0 ||
                    candidate_scale != height / logical_height || (candidate_scale & (candidate_scale - 1)) ||
                    !ChainFits(retail_width * uint64_t{candidate_scale}, retail_height * uint64_t{candidate_scale},
                               variant.levels.size())) {
                    Diagnostic(path, "expected original dimensions or uniform power-of-two upscale; trying lower-priority asset");
                    continue;
                }
                scale = candidate_scale;
                decoded.width = retail_width * scale;
                decoded.height = retail_height * scale;
            } else if (width != std::max(1u, (static_cast<uint32_t>(entry.width) * scale) >> mip) ||
                       height != std::max(1u, (static_cast<uint32_t>(entry.height) * scale) >> mip)) {
                Diagnostic(path, "mip dimensions do not match the selected base; trying lower-priority asset");
                continue;
            }
            decoded.levels.push_back(Pad(std::move(pixels), width, height,
                                         std::max(1u, decoded.width >> mip), std::max(1u, decoded.height >> mip)));
            selected = true;
            break;
        }
        if (!selected) {
            if (mip == 0) {
                decoded = {};
                error = "cannot load a valid base texture PNG";
                return false;
            }
            decoded.levels.push_back(Downsample(decoded.levels.back(), std::max(1u, decoded.width >> (mip - 1)),
                                                std::max(1u, decoded.height >> (mip - 1))));
        }
    }
    error.clear();
    return true;
}

} // namespace

/**
 * Decodes a catalog texture, from the decode cache when the same variant was decoded before.
 */
bool Decode(const texture_catalog::Entry &entry, std::size_t variant_index, DecodedTexture &decoded,
            std::string &error) {
    if (entry.type != "texture" || entry.bpp != 32 || entry.width <= 0 || entry.height <= 0 ||
        entry.width > static_cast<int>(kMaxDimension) || entry.height > static_cast<int>(kMaxDimension) ||
        variant_index >= entry.variants.size() || entry.variants[variant_index].levels.empty()) {
        return DecodeUncached(entry, variant_index, decoded, error);
    }
    return DecodeCached(DecodeKey(entry, variant_index), [&](DecodedTexture &out, std::string &failure) { return DecodeUncached(entry, variant_index, out, failure); }, decoded, error);
}

std::string ImageDecodeKey(std::string_view identity, std::span<const std::filesystem::path> sources) {
    std::string key = std::to_string(identity.size()) + ':' + std::string(identity);
    for (const auto &path : sources) {
        const auto name = Display(path);
        const auto stamp = FileStamp(name, path);
        key += '|' + std::to_string(name.size()) + ':' + name + '|' + std::to_string(stamp.size) + '|' +
               std::to_string(static_cast<unsigned long long>(stamp.time));
    }
    return key;
}

bool DecodeCached(std::string_view identity, const std::function<bool(DecodedTexture &, std::string &)> &decoder,
                  DecodedTexture &decoded, std::string &error) {
    if (identity.empty()) {
        return decoder(decoded, error);
    }
    const std::string key(identity);
    if (const auto found = CacheFind(key)) {
        Trace("decode cache hits");
        decoded = *found;
        error.clear();
        return true;
    }
    Trace("decode cache misses");
    DecodedTexture fresh;
    if (!decoder(fresh, error)) {
        decoded = std::move(fresh);
        return false;
    }
    if (CacheBytes(fresh) == 0) {
        decoded = std::move(fresh);
        return true;
    }
    auto shared = std::make_shared<const DecodedTexture>(std::move(fresh));
    CacheInsert(key, shared);
    decoded = *shared;
    return true;
}

std::shared_ptr<const DecodedTexture> DecodeShared(std::string_view                                            identity,
                                                   const std::function<bool(DecodedTexture &, std::string &)> &decoder,
                                                   std::string                                                &error) {
    const std::string key(identity);
    if (!key.empty()) {
        if (auto found = CacheFind(key)) {
            Trace("decode cache hits");
            error.clear();
            return found;
        }
        Trace("decode cache misses");
    }
    DecodedTexture fresh;
    if (!decoder(fresh, error)) {
        return nullptr;
    }
    auto shared = std::make_shared<const DecodedTexture>(std::move(fresh));
    if (!key.empty()) {
        CacheInsert(key, shared);
    }
    return shared;
}

bool ReadFileBytes(const std::filesystem::path &path, std::vector<uint8_t> &bytes, std::uint64_t max_bytes) {
    bytes.clear();
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    const auto end = static_cast<std::streamoff>(file.tellg());
    if (end < 0 || static_cast<std::uint64_t>(end) > max_bytes ||
        static_cast<std::uint64_t>(end) > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    file.seekg(0);
    bytes.resize(static_cast<std::size_t>(end));
    if (!bytes.empty() && !file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        bytes.clear();
        return false;
    }
    Trace("files read");
    Trace("file bytes read", bytes.size());
    return true;
}

void Prefetch(std::span<DecodeJob> jobs) {
    const TraceTimer timer("prefetch wall us");
    // Each identity is decoded at most once per batch, and only when the cache does not hold it already.
    std::vector<DecodeJob *>        pending;
    std::unordered_set<std::string> seen;
    for (auto &job : jobs) {
        if (job.identity.empty() || !job.decoder || !seen.insert(job.identity).second || CacheContains(job.identity)) {
            continue;
        }
        pending.push_back(&job);
    }
    Trace("prefetch jobs", pending.size());
    if (pending.empty()) {
        return;
    }
    std::atomic<std::size_t> next = 0;
    std::atomic<std::size_t> decoded = 0;
    const auto               work = [&] {
        for (std::size_t index = next++; index < pending.size(); index = next++) {
            const auto    &job = *pending[index];
            DecodedTexture fresh;
            std::string    error;
            bool           succeeded = false;
            try {
                succeeded = job.decoder(fresh, error);
            } catch (...) {
                succeeded = false;
            }
            if (succeeded) {
                CacheInsert(job.identity, std::make_shared<const DecodedTexture>(std::move(fresh)));
                ++decoded;
            }
        }
    };
    const auto workers = std::min<std::size_t>(std::clamp(std::thread::hardware_concurrency(), 1u, kMaxPrefetchWorkers),
                                               pending.size());
    {
        std::vector<std::jthread> threads;
        threads.reserve(workers - 1);
        for (std::size_t worker = 1; worker < workers; ++worker) {
            threads.emplace_back(work);
        }
        work();
    }
    Trace("prefetch decoded", decoded);
}

StampScope::StampScope() {
    const std::lock_guard lock(memo_mutex);
    ++stamp_scopes;
}

StampScope::~StampScope() {
    const std::lock_guard lock(memo_mutex);
    if (--stamp_scopes == 0) {
        stamp_memo.clear();
    }
}

bool Tracing() {
    static const bool enabled = [] {
        const char *value = std::getenv("KZ_TEXTURE_TRACE");
        return value && std::string_view(value) == "1";
    }();
    return enabled;
}

void Trace(std::string_view counter, std::uint64_t value) {
    if (!Tracing()) {
        return;
    }
    auto                 &counters = Counters();
    const std::lock_guard lock(counters.mutex);
    if (const auto found = counters.totals.find(counter); found != counters.totals.end()) {
        found->second += value;
    } else {
        counters.totals.emplace(std::string(counter), value);
    }
}

TraceTimer::TraceTimer(std::string_view counter) : counter(counter), start(std::chrono::steady_clock::now()) {}

TraceTimer::~TraceTimer() {
    if (Tracing()) {
        Trace(counter, static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                                      std::chrono::steady_clock::now() - start)
                                                      .count()));
    }
}

} // namespace datapacks
