#include "common.h"

#include <sifdev.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "dataread.hpp"
#include "dataread_port.hpp"
#include "platform/paths.hpp"

namespace fs = std::filesystem;

// Replacements own the state they touch: a port unit cannot see ps2/src's statics, so the current
// directory, the background queue and the file cache live here.

namespace {

constexpr std::size_t kSector = DATAREAD_SECTOR_SIZE;
constexpr std::size_t kExpectedFiles = 10240;

struct IndexedFile {
    fs::path path;
};

std::unordered_map<std::string, IndexedFile> file_index;
bool                                         index_built;

std::string current_dir;

struct CacheSlot {
    u_long128  *address = nullptr;
    int         size = 0;
    int         ref_count = 0;
    std::string key;
};

CacheSlot    cache_slots[FILE_CACHE_MAX];
u_long128   *cache_base;
u_long128   *cache_next;
int          cache_type = FILE_CACHE_NONE;

BG_READ_INFO bg_slots[BG_READ_MAX];

char FoldChar(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}

std::size_t PaddedSize(std::size_t size) {
    return (size + kSector - 1) / kSector * kSector;
}

// Root-anchored names skip the current directory: retail joined it in and produced "dir//name", which
// the disc never matched.
std::string IndexKey(std::string_view name) {
    std::string text;
    std::size_t colon = name.find(':');
    std::size_t separator = name.find_first_of("/\\");

    if (name.size() >= 2 && name[1] == ':') {
        text = name;
    } else if (colon != std::string_view::npos && colon < separator) {
        text = name.substr(colon + 1);
    } else if (!name.empty() && (name[0] == '/' || name[0] == '\\')) {
        text = name;
    } else {
        text = current_dir + std::string(name);
    }

    std::replace(text.begin(), text.end(), '\\', '/');

    if (std::size_t semicolon = text.rfind(';'); semicolon != std::string::npos) {
        std::string_view version = std::string_view(text).substr(semicolon + 1);
        if (!version.empty() && std::all_of(version.begin(), version.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            text.erase(semicolon);
        }
    }

    // Same encoding as dc2data's DiskName, in lower case because the index keys are folded.
    std::string key;
    key.reserve(text.size());
    for (unsigned char c : text) {
        if (c == '/' && (key.empty() || key.back() == '/')) {
            continue;
        }
        if (c >= 0x80 || c == '%') {
            char hex[4];
            std::snprintf(hex, sizeof hex, "%%%02x", static_cast<unsigned>(c));
            key += hex;
        } else {
            key.push_back(FoldChar(static_cast<char>(c)));
        }
    }
    return key;
}

const IndexedFile *Lookup(const std::string &key) {
    if (!index_built) {
        InitCDFile();
    }
    auto found = file_index.find(key);
    return found == file_index.end() ? nullptr : &found->second;
}

// Sized on demand: stat-ing every file while indexing costs about a millisecond per file on the 9p
// mounts the host data lives on, and only the files a game opens need a size.
std::optional<std::uintmax_t> FileSize(const IndexedFile &file) {
    std::error_code error;
    std::uintmax_t  size = fs::file_size(file.path, error);
    if (error) {
        return std::nullopt;
    }
    return size;
}

// The disc fills the tail of its last sector with whatever follows the file, so the tail is zeroed
// here: the result must not depend on the caller's buffer.
int ReadWhole(const IndexedFile &file, void *buffer) {
    std::optional<std::uintmax_t> sized = FileSize(file);
    if (!sized || *sized > static_cast<std::uintmax_t>(INT32_MAX)) {
        return -1;
    }
    std::ifstream stream(file.path, std::ios::binary);
    if (!stream) {
        return -1;
    }
    stream.read(static_cast<char *>(buffer), static_cast<std::streamsize>(*sized));
    if (static_cast<std::uintmax_t>(stream.gcount()) != *sized) {
        return -1;
    }
    std::size_t size = static_cast<std::size_t>(*sized);
    std::memset(static_cast<char *>(buffer) + size, 0, PaddedSize(size) - size);
    return static_cast<int>(size);
}

CacheSlot *CacheFind(const std::string &key) {
    if (!cache_base) {
        return nullptr;
    }
    for (CacheSlot &slot : cache_slots) {
        if (slot.address && slot.key == key) {
            return &slot;
        }
    }
    return nullptr;
}

int CacheRecord(const std::string &key, u_long128 *address, int size) {
    for (CacheSlot &slot : cache_slots) {
        if (!slot.address) {
            slot.address = address;
            slot.size = size;
            slot.ref_count = 1;
            slot.key = key;
            return 1;
        }
    }
    return 0;
}

// Retail rounds the cache base to a 64-byte boundary; the address is kept as a pointer, not truncated.
u_long128 *AlignCacheAddress(u_long128 *address) {
    std::uintptr_t value = reinterpret_cast<std::uintptr_t>(address);
    value = (value + 63) & ~static_cast<std::uintptr_t>(63);
    return reinterpret_cast<u_long128 *>(value);
}

BG_READ_INFO *BgFreeSlot() {
    for (BG_READ_INFO &info : bg_slots) {
        if (!info.busy) {
            return &info;
        }
    }
    return nullptr;
}

void BuildIndex(const fs::path &root) {
    file_index.clear();
    index_built = true;

    std::error_code error;
    if (!fs::is_directory(root, error)) {
        std::fprintf(stderr, "dataread: indexed 0 files under %s (not a directory)\n", PathsDisplay(root).c_str());
        return;
    }

    // The disc holds about 8.8k files; reserving up front avoids rehashing while the index fills.
    file_index.reserve(kExpectedFiles);

    // A trailing separator would make lexically_relative climb out of the root.
    fs::path base = root.has_filename() ? root : root.parent_path();
    fs::recursive_directory_iterator it(root,
                                        fs::directory_options::follow_directory_symlink |
                                            fs::directory_options::skip_permission_denied,
                                        error);
    for (const fs::recursive_directory_iterator end; !error && it != end; it.increment(error)) {
        const fs::directory_entry &entry = *it;
        std::error_code            entry_error;
        if (!entry.is_regular_file(entry_error) || entry_error) {
            continue;
        }
        std::string relative = entry.path().lexically_relative(base).generic_string();
        std::string key;
        key.reserve(relative.size());
        for (char c : relative) {
            key.push_back(FoldChar(c));
        }
        file_index.insert_or_assign(std::move(key), IndexedFile{entry.path()});
    }

    if (error) {
        std::fprintf(stderr, "dataread: walking %s stopped: %s\n", PathsDisplay(root).c_str(), error.message().c_str());
    }
    std::fprintf(stderr, "dataread: indexed %zu files under %s\n", file_index.size(), PathsDisplay(root).c_str());
}

} // namespace

void DataReadIndex(const fs::path &root) {
    BuildIndex(root);
}

std::optional<fs::path> DataReadFind(const char *game_name) {
    if (!game_name) {
        return std::nullopt;
    }
    const IndexedFile *file = Lookup(IndexKey(game_name));
    if (!file) {
        return std::nullopt;
    }
    return file->path;
}

PORT_OVERRIDE void InitCDFile() {
    DataReadIndex(PathsDataRoot());
}

PORT_OVERRIDE int ChangeHddFile() {
    // The extracted data is the only device on the host; there is no hard-disk installation to mount.
    return 0;
}

PORT_OVERRIDE void SetCurrentDir(char *dir) {
    if (!dir) {
        current_dir.clear();
        return;
    }
    if (*dir == '/') {
        dir++;
    }
    current_dir = dir;
}

PORT_OVERRIDE void GetCurrentDir(char *out_dir) {
    std::memcpy(out_dir, current_dir.c_str(), current_dir.size() + 1);
}

PORT_OVERRIDE void ChangeDir(char *dir) {
    current_dir.clear();
    if (dir) {
        if (*dir == '/') {
            dir++;
        }
        current_dir += dir;
    }
}

PORT_OVERRIDE void InitReadBG() {
    for (BG_READ_INFO &info : bg_slots) {
        info.busy = 0;
    }
}

PORT_OVERRIDE void StartReadBG() {
    InitReadBG();
}

// The read is done when LoadFileBG returns, so the slot it hands out is already finished, as retail's
// ReadBG leaves a finished request.
PORT_OVERRIDE int LoadFileBG(char *name, u_long128 *buffer, int *out_size) {
    if (out_size) {
        *out_size = 0;
    }
    if (!name || *name == '\0' || !buffer) {
        return 0;
    }

    BG_READ_INFO *info = BgFreeSlot();
    if (!info) {
        return 0;
    }

    std::string key = IndexKey(name);
    int         size = 0;

    if (CacheSlot *cached = CacheFind(key)) {
        std::memcpy(buffer, cached->address, static_cast<std::size_t>(cached->size));
        cached->ref_count--;
        size = cached->size;
    } else {
        const IndexedFile *file = Lookup(key);
        if (!file) {
            return 0;
        }
        size = ReadWhole(*file, buffer);
        if (size < 0) {
            return 0;
        }
    }

    std::string composed = current_dir + name;
    std::snprintf(info->name, sizeof info->name, "%s", composed.c_str());
    info->busy = 1;
    info->dev = FILE_DEV_CDROM;
    info->issued = 1;
    info->done = 1;
    info->buffer = buffer;
    info->size = size;
    info->sector = 0;
    info->sectors = size_to_sector(size);

    if (out_size) {
        *out_size = size;
    }
    return 1;
}

PORT_OVERRIDE BG_READ_INFO *GetReadBGFile(char *name) {
    for (BG_READ_INFO &info : bg_slots) {
        if (info.busy && strcasecmp(name, info.name) == 0) {
            return &info;
        }
    }
    return nullptr;
}

PORT_OVERRIDE BG_READ_INFO *GetReadBGFile(int index) {
    if (index < 0 || index >= BG_READ_MAX) {
        return nullptr;
    }
    return bg_slots[index].busy ? &bg_slots[index] : nullptr;
}

// Nothing is outstanding between LoadFileBG calls, so a vertical sync has nothing to advance.
PORT_OVERRIDE void ReadBG() {}

PORT_OVERRIDE int ReadBGSync() {
    for (const BG_READ_INFO &info : bg_slots) {
        if (info.busy && (!info.issued || !info.done)) {
            return 1;
        }
    }
    return 0;
}

// Retail returns without emptying the queue when no read is outstanding, so finished slots stay
// visible until StartReadBG; kept as it is.
PORT_OVERRIDE void BreakReadBG() {
    if (!ReadBGSync()) {
        return;
    }
    InitReadBG();
}

// Retail's LoadFile stops the game when the file cannot be read. The host returns 0 and reports it,
// so a missing file in a gate run is a visible failure rather than an exit.
PORT_OVERRIDE int LoadFile(char *path, void *buffer, int *out_size) {
    if (!LoadFile2(path, buffer, out_size, LOAD_FILE_READ)) {
        std::fprintf(stderr, "File open error \"%s\"\n", path);
        return 0;
    }
    return 1;
}

PORT_OVERRIDE int LoadFile2(char *path, void *buffer, int *out_size, int mode) {
    if (out_size) {
        *out_size = 0;
    }
    if (!path || *path == '\0' || (mode != LOAD_FILE_SIZE && !buffer)) {
        return 0;
    }

    std::string key = IndexKey(path);

    if (CacheSlot *cached = CacheFind(key)) {
        if (mode != LOAD_FILE_SIZE) {
            std::memcpy(buffer, cached->address, static_cast<std::size_t>(cached->size));
        }
        if (mode == LOAD_FILE_READ) {
            cached->ref_count--;
        }
        if (out_size) {
            *out_size = cached->size;
        }
        return 1;
    }

    const IndexedFile *file = Lookup(key);
    if (!file) {
        return 0;
    }
    std::optional<std::uintmax_t> sized = FileSize(*file);
    if (!sized || *sized > static_cast<std::uintmax_t>(INT32_MAX)) {
        return 0;
    }
    if (out_size) {
        *out_size = static_cast<int>(*sized);
    }
    if (mode == LOAD_FILE_SIZE) {
        return 1;
    }

    int size = ReadWhole(*file, buffer);
    if (size < 0) {
        return 0;
    }
    if (out_size) {
        *out_size = size;
    }
    return 1;
}

PORT_OVERRIDE void InitFileCache(u_long128 *address, int type) {
    cache_base = nullptr;
    for (CacheSlot &slot : cache_slots) {
        slot = CacheSlot{};
    }

    if (type == FILE_CACHE_DOWN || type == FILE_CACHE_UP) {
        cache_base = AlignCacheAddress(address);
        cache_next = cache_base;
        if (type == FILE_CACHE_DOWN) {
            cache_next -= 4;
        }
        cache_type = type;
    }
}

PORT_OVERRIDE int LoadFileCacheBG(char *path) {
    if (!path || *path == '\0' || !cache_base) {
        return 0;
    }

    std::string key = IndexKey(path);
    if (CacheSlot *cached = CacheFind(key)) {
        cached->ref_count++;
        return 1;
    }

    const IndexedFile *file = Lookup(key);
    if (!file) {
        return 0;
    }
    std::optional<std::uintmax_t> sized = FileSize(*file);
    if (!sized || *sized > static_cast<std::uintmax_t>(INT32_MAX)) {
        return 0;
    }
    int         size = static_cast<int>(*sized);
    std::size_t quadwords = PaddedSize(static_cast<std::size_t>(size)) / 16;

    u_long128 *buffer = cache_next;
    if (cache_type == FILE_CACHE_DOWN) {
        cache_next -= quadwords;
        buffer = cache_next;
    } else if (cache_type == FILE_CACHE_UP) {
        cache_next += quadwords;
    }

    if (!LoadFileBG(path, buffer, nullptr)) {
        return 0;
    }
    return CacheRecord(key, buffer, size);
}

PORT_OVERRIDE u_long128 *SearchFileCache(char *path, int *out_size) {
    if (out_size) {
        *out_size = 0;
    }
    if (!path) {
        return nullptr;
    }
    CacheSlot *cached = CacheFind(IndexKey(path));
    if (!cached) {
        return nullptr;
    }
    if (out_size) {
        *out_size = cached->size;
    }
    return cached->address;
}

// Writes go through the host file server, which maps host0: to the save root.
PORT_OVERRIDE int WriteFile(char *path, void *buffer, int size) {
    if (!path || size < 0) {
        return 0;
    }
    if (std::string_view(path).starts_with("net:")) {
        std::fprintf(stderr, "dataread: no network socket on the host, not writing %s\n", path);
        return 0;
    }

    int fd = sceOpen(path, SCE_WRONLY | SCE_CREAT | SCE_TRUNC);
    if (fd < 0) {
        return 0;
    }
    int written = sceWrite(fd, buffer, size);
    sceClose(fd);
    return written == size;
}
