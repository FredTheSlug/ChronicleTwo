#include "save_slots.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>

#include "platform/files.hpp"
#include "platform/paths.hpp"

namespace fs = std::filesystem;

SaveSlotList SaveSlots;
bool         SaveSlotNew = false;

namespace {
constexpr char kPrefix[] = "BESCES-51190dkcl";
constexpr char kSecondCard[] = ".card2";

int CheckDigit(const char *bytes, std::size_t size) {
    int result = 0;
    for (std::size_t i = 0; i < size / 64; ++i) {
        result += static_cast<unsigned char>(bytes[i * 64]) % 255;
    }
    return result;
}

bool ListDirectory(const fs::path &path, std::vector<fs::path> &paths) {
    std::error_code        error;
    fs::directory_iterator iter(path, error), end;
    if (error == std::errc::no_such_file_or_directory) {
        return true;
    }
    if (error) {
        return false;
    }
    while (iter != end) {
        paths.push_back(iter->path());
        iter.increment(error);
        if (error) {
            return false;
        }
    }
    return true;
}

bool Import(const fs::path &from, const fs::path &to) {
    std::error_code error;
    if (!fs::is_regular_file(from, error)) {
        return false;
    }
    auto size = fs::file_size(from, error);
    // Large, unusable files remain visible at their original location. Reading a
    // malformed file must not exhaust memory while opening the menu.
    if (error || size > 16 * 1024 * 1024) {
        return false;
    }
    std::vector<char> bytes(static_cast<std::size_t>(size));
    if (FilesRead(from, bytes.data(), bytes.size()) != bytes.size()) {
        return false;
    }
    if (FilesCreateDirectory(SaveSlotsRoot()) == FilesResult::kFailed) {
        return false;
    }
    return FilesWrite(to, bytes.data(), bytes.size(), false) == FilesResult::kWritten;
}

bool SameBytes(const fs::path &left, const fs::path &right) {
    std::error_code error;
    if (!fs::is_regular_file(left, error) || !fs::is_regular_file(right, error)) {
        return false;
    }
    const auto size = fs::file_size(left, error);
    if (error) {
        return false;
    }
    if (size != fs::file_size(right, error) || error) {
        return false;
    }
    std::ifstream a(left, std::ios::binary), b(right, std::ios::binary);
    if (!a || !b) {
        return false;
    }
    std::array<char, 65536> x, y;
    for (std::uintmax_t remaining = size; remaining;) {
        const auto count = static_cast<std::streamsize>(std::min<std::uintmax_t>(remaining, x.size()));
        if (!a.read(x.data(), count) || !b.read(y.data(), count) ||
            std::memcmp(x.data(), y.data(), static_cast<std::size_t>(count)) != 0) {
            return false;
        }
        remaining -= count;
    }
    return true;
}

struct CachedHeader {
    std::uintmax_t     size;
    fs::file_time_type modified;
    SaveSlotState      state;
    SAVEDATA_INFO      info;
};

std::map<fs::path, CachedHeader> header_cache;

SaveSlotState ReadHeader(const fs::path &path, std::uintmax_t size, SAVEDATA_INFO &info) {
    if (size != sizeof(SAVEDATA_FORMAT)) {
        return SaveSlotState::WrongSize;
    }
    std::ifstream file(path, std::ios::binary);
    auto          image = std::make_unique<SAVEDATA_FORMAT>();
    // Read the retail header and only the two payload fields needed by the list.
    file.read(reinterpret_cast<char *>(image.get()), offsetof(SAVEDATA_FORMAT, save_data));
    file.seekg(offsetof(SAVEDATA_FORMAT, save_data) + offsetof(CSaveData, play_time));
    file.read(reinterpret_cast<char *>(&image->save_data.play_time), sizeof(image->save_data.play_time));
    file.seekg(offsetof(SAVEDATA_FORMAT, save_data) + offsetof(CSaveData, game_progress));
    file.read(reinterpret_cast<char *>(&image->save_data.game_progress), sizeof(image->save_data.game_progress));
    if (!file) {
        return SaveSlotState::Unreadable;
    }
    if (std::memcmp(image->version, "dc2Ver4", 8) != 0 &&
        std::memcmp(image->version, "dc2Ver2", 8) != 0) {
        return SaveSlotState::WrongVersion;
    }
    if (image->incomplete) {
        return SaveSlotState::Incomplete;
    }
    // Ver2 can be loaded and converted, but its raw header must not grant
    // unlocks, debug codes or newest-save priority before that conversion.
    info.state = std::memcmp(image->version, "dc2Ver4", 8) == 0 ? 1 : 0;
    info.map_no = image->map_no;
    info.program_loop_no = image->program_loop_no;
    info.dungeon_no = image->dungeon_no;
    info.floor_id = image->floor_id;
    info.progress = image->save_data.game_progress;
    info.play_time = image->save_data.play_time;
    info.unique_counter = info.state ? image->unique_counter : 0;
    info.omake_flag = info.state ? image->omake_flag : 0;
    info.costume_bit = info.state ? image->costume_bit : 0;
    info.debug_code = info.state ? image->debug_code : 0;
    info.fish_num = image->fish_num;
    return SaveSlotState::Ready;
}

SaveSlotState CachedInfo(const fs::path &path, SAVEDATA_INFO &info) {
    std::error_code error;
    if (!fs::is_regular_file(path, error)) {
        return SaveSlotState::Unreadable;
    }
    const auto size = fs::file_size(path, error);
    if (error) {
        return SaveSlotState::Unreadable;
    }
    const auto modified = fs::last_write_time(path, error);
    if (error) {
        return SaveSlotState::Unreadable;
    }
    const auto old = header_cache.find(path);
    if (old != header_cache.end() && old->second.size == size && old->second.modified == modified) {
        info = old->second.info;
        return old->second.state;
    }
    const auto state = ReadHeader(path, size, info);
    if (state != SaveSlotState::Unreadable) {
        header_cache[path] = {size, modified, state, info};
    }
    return state;
}

fs::path ImportMarker(const fs::path &target) {
    return PathsSaveRoot() / ".save-imports" / target.filename();
}

void RecordImport(const fs::path &target) {
    auto marker = ImportMarker(target);
    if (FilesCreateDirectory(marker.parent_path()) != FilesResult::kFailed) {
        constexpr char imported[] = "imported\n";
        const auto     result = FilesWrite(marker, imported, sizeof(imported) - 1, false);
        if (result == FilesResult::kWritten || result == FilesResult::kExists) {
            return;
        }
    }
    std::fprintf(stderr, "save import: could not record %s; will retry on the next scan\n",
                 PathsDisplay(marker).c_str());
}

int FirstFree(const std::set<int> &occupied) {
    int number = 0;
    for (int used : occupied) {
        if (used == number) {
            ++number;
        } else if (used > number) {
            break;
        }
    }
    return number < 1000000000 ? number : -1;
}
} // namespace

fs::path SaveSlotsRoot() { return PathsSaveRoot() / "saves"; }

fs::path SaveSlotPath(int number, bool second_card) {
    return SaveSlotsRoot() / (std::string(kPrefix) + std::to_string(number) + (second_card ? kSecondCard : ""));
}

int SaveSlotNumber(const std::string &name, bool &second_card) {
    std::string_view text(name);
    second_card = text.ends_with(kSecondCard);
    if (second_card) {
        text.remove_suffix(sizeof(kSecondCard) - 1);
    }
    if (!text.starts_with(kPrefix)) {
        return -1;
    }
    text.remove_prefix(sizeof(kPrefix) - 1);
    if (text.empty() || text.size() > 9 || (text.size() > 1 && text.front() == '0')) {
        return -1;
    }
    int number = 0;
    for (char c : text) {
        if (c < '0' || c > '9') {
            return -1;
        }
        number = number * 10 + c - '0';
    }
    return number;
}

int SaveSlotFirstFree(const SaveSlotList &list) {
    return list.first_free;
}

int SaveSlotRows(const SaveSlotList &list, bool saving) {
    return static_cast<int>(list.saves.size()) + (saving && list.first_free >= 0 ? 1 : 0);
}

bool SaveSlotIsNew(const SaveSlotList &list, int row, bool saving) {
    return saving && list.first_free >= 0 && row == static_cast<int>(list.saves.size());
}

const char *SaveSlotStatus(SaveSlotState state) {
    switch (state) {
        case SaveSlotState::Ready:
            return "";
        case SaveSlotState::Unreadable:
            return "Cannot read save";
        case SaveSlotState::WrongSize:
            return "Unusable save (size)";
        case SaveSlotState::WrongVersion:
            return "Unsupported save version";
        case SaveSlotState::Incomplete:
            return "Incomplete save";
        case SaveSlotState::BadChecksum:
            return "Corrupt save";
    }
    return "Unusable save";
}

SaveSlotState SaveSlotRead(const fs::path &path, SAVEDATA_FORMAT &image) {
    std::error_code error;
    if (!fs::is_regular_file(path, error)) {
        return SaveSlotState::Unreadable;
    }
    auto size = fs::file_size(path, error);
    if (error) {
        return SaveSlotState::Unreadable;
    }
    if (size != sizeof(image)) {
        return SaveSlotState::WrongSize;
    }
    if (FilesRead(path, &image, sizeof(image)) != sizeof(image)) {
        return SaveSlotState::Unreadable;
    }
    const bool old_version = std::memcmp(image.version, "dc2Ver2", 8) == 0;
    if (!old_version && std::memcmp(image.version, "dc2Ver4", 8) != 0) {
        return SaveSlotState::WrongVersion;
    }
    if (image.incomplete) {
        return SaveSlotState::Incomplete;
    }
    // Retail accepts Ver2 without check digits before converting its cards.
    if (old_version) {
        return SaveSlotState::Ready;
    }
    const char *data = reinterpret_cast<const char *>(&image.save_data);
    // Retail permits zero check digits in older images.
    if ((image.check_digit && image.check_digit != CheckDigit(data, sizeof(CSaveData))) ||
        (image.check_digit_half && image.check_digit_half != CheckDigit(data, sizeof(CSaveData) / 2))) {
        return SaveSlotState::BadChecksum;
    }
    return SaveSlotState::Ready;
}

SaveSlotList SaveSlotScan() {
    SaveSlotList                             list;
    std::map<std::pair<int, bool>, fs::path> entries;
    std::vector<fs::path>                    paths;
    list.read_failed = !ListDirectory(SaveSlotsRoot(), paths);
    for (const auto &path : paths) {
        bool second;
        int  number = SaveSlotNumber(path.filename().string(), second);
        if (number >= 0) {
            entries[{number, second}] = path;
            list.occupied.insert(number);
        }
    }
    std::vector<std::pair<fs::path, bool> > legacy;
    for (int card = 0; card < 2; ++card) {
        paths.clear();
        if (!ListDirectory(PathsSaveRoot() / (card ? "mc1" : "mc0"), paths)) {
            list.read_failed = true;
        }
        for (const auto &dir : paths) {
            bool second;
            int  number = SaveSlotNumber(dir.filename().string(), second);
            if (number < 0 || second) {
                continue;
            }
            auto            target = SaveSlotPath(number, card != 0);
            std::error_code error;
            if (fs::exists(ImportMarker(target), error)) {
                continue;
            }
            list.occupied.insert(number);
            legacy.emplace_back(dir, card != 0);
        }
    }
    // Reserve all legacy names before relocating any collision, including names
    // whose payload is temporarily unavailable.
    for (const auto &[dir, second_card] : legacy) {
        bool            second;
        int             number = SaveSlotNumber(dir.filename().string(), second);
        auto            key = std::make_pair(number, second_card);
        auto            target = SaveSlotPath(number, second_card);
        const auto      marker_target = target;
        const auto      source = dir / dir.filename();
        std::error_code error;
        if (entries.contains(key)) {
            // A matching number alone does not prove an import. Search flat
            // files too, so a failed marker write can retry a relocated copy.
            const bool imported = std::ranges::any_of(entries, [&](const auto &entry) {
                return entry.second.parent_path() == SaveSlotsRoot() && SameBytes(source, entry.second);
            });
            if (imported) {
                RecordImport(marker_target);
                continue;
            }
            number = list.read_failed ? -1 : FirstFree(list.occupied);
            if (number < 0) {
                continue;
            }
            key = {number, second_card};
            target = SaveSlotPath(number, second_card);
            list.occupied.insert(number);
        }
        if (Import(source, target)) {
            header_cache.erase(target);
            RecordImport(marker_target);
        }
        // Unreadable payloads are omitted from rows but retain their names.
        entries[key] = fs::exists(target, error) ? target : source;
    }
    std::set<fs::path> seen;
    for (const auto &[key, path] : entries) {
        SaveSlot slot;
        slot.number = key.first;
        slot.second_card = key.second;
        slot.path = path;
        seen.insert(path);
        slot.state = CachedInfo(path, slot.info);
        if (slot.state == SaveSlotState::Unreadable) {
            continue;
        }
        slot.info.file_no = slot.number;
        list.saves.push_back(slot);
    }
    std::erase_if(header_cache, [&](const auto &entry) { return !seen.contains(entry.first); });
    list.first_free = list.read_failed ? -1 : FirstFree(list.occupied);
    return list;
}

FilesResult SaveSlotWrite(const fs::path &path, const SAVEDATA_FORMAT &image, bool replace) {
    const auto result = FilesWrite(path, &image, sizeof(image), replace);
    if (result == FilesResult::kWritten) {
        header_cache.erase(path);
    }
    return result;
}
