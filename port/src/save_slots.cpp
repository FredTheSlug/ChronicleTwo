#include "save_slots.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>

#include "platform/files.hpp"
#include "platform/paths.hpp"

namespace fs = std::filesystem;

SaveSlotList SaveSlots;
bool SaveSlotNew = false;

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
    std::error_code error;
    fs::directory_iterator iter(path, error), end;
    if (error == std::errc::no_such_file_or_directory) return true;
    if (error) return false;
    while (iter != end) {
        paths.push_back(iter->path());
        iter.increment(error);
        if (error) return false;
    }
    return true;
}

void Import(const fs::path &from, const fs::path &to) {
    std::error_code error;
    if (!fs::is_regular_file(from, error)) return;
    auto size = fs::file_size(from, error);
    // Large, unusable files remain visible at their original location. Reading a
    // malformed file must not exhaust memory while opening the menu.
    if (error || size > 16 * 1024 * 1024) return;
    std::vector<char> bytes(static_cast<std::size_t>(size));
    if (FilesRead(from, bytes.data(), bytes.size()) != bytes.size()) return;
    if (FilesCreateDirectory(SaveSlotsRoot()) == FilesResult::kFailed) return;
    FilesWrite(to, bytes.data(), bytes.size(), false);
}
} // namespace

fs::path SaveSlotsRoot() { return PathsSaveRoot() / "saves"; }

fs::path SaveSlotPath(int number, bool second_card) {
    return SaveSlotsRoot() / (std::string(kPrefix) + std::to_string(number) + (second_card ? kSecondCard : ""));
}

int SaveSlotNumber(const std::string &name, bool &second_card) {
    std::string_view text(name);
    second_card = text.ends_with(kSecondCard);
    if (second_card) text.remove_suffix(sizeof(kSecondCard) - 1);
    if (!text.starts_with(kPrefix)) return -1;
    text.remove_prefix(sizeof(kPrefix) - 1);
    if (text.empty() || text.size() > 9 || (text.size() > 1 && text.front() == '0')) return -1;
    int number = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return -1;
        number = number * 10 + c - '0';
    }
    return number;
}

int SaveSlotFirstFree(const SaveSlotList &list) {
    int number = 0;
    for (const auto &slot : list.saves) {
        if (slot.number == number) ++number;
        else if (slot.number > number) break;
    }
    return number < 1000000000 ? number : -1;
}

int SaveSlotRows(const SaveSlotList &list, bool saving) {
    return static_cast<int>(list.saves.size()) + (saving ? 1 : 0);
}

bool SaveSlotIsNew(const SaveSlotList &list, int row, bool saving) {
    return saving && row == static_cast<int>(list.saves.size());
}

const char *SaveSlotStatus(SaveSlotState state) {
    switch (state) {
        case SaveSlotState::Ready: return "";
        case SaveSlotState::Unreadable: return "Cannot read save";
        case SaveSlotState::WrongSize: return "Unusable save (size)";
        case SaveSlotState::WrongVersion: return "Unsupported save version";
        case SaveSlotState::Incomplete: return "Incomplete save";
        case SaveSlotState::BadChecksum: return "Corrupt save";
    }
    return "Unusable save";
}

SaveSlotState SaveSlotRead(const fs::path &path, SAVEDATA_FORMAT &image) {
    std::error_code error;
    if (!fs::is_regular_file(path, error)) return SaveSlotState::Unreadable;
    auto size = fs::file_size(path, error);
    if (error) return SaveSlotState::Unreadable;
    if (size != sizeof(image)) return SaveSlotState::WrongSize;
    if (FilesRead(path, &image, sizeof(image)) != sizeof(image)) return SaveSlotState::Unreadable;
    if (std::memcmp(image.version, "dc2Ver4", 8) != 0) return SaveSlotState::WrongVersion;
    if (image.incomplete) return SaveSlotState::Incomplete;
    const char *data = reinterpret_cast<const char *>(&image.save_data);
    // Retail permits zero check digits in older images.
    if ((image.check_digit && image.check_digit != CheckDigit(data, sizeof(CSaveData))) ||
        (image.check_digit_half && image.check_digit_half != CheckDigit(data, sizeof(CSaveData) / 2))) {
        return SaveSlotState::BadChecksum;
    }
    return SaveSlotState::Ready;
}

SaveSlotList SaveSlotScan() {
    SaveSlotList list;
    std::map<std::pair<int, bool>, fs::path> entries;
    std::vector<fs::path> paths;
    list.read_failed = !ListDirectory(SaveSlotsRoot(), paths);
    for (const auto &path : paths) {
        bool second;
        int number = SaveSlotNumber(path.filename().string(), second);
        if (number >= 0) entries[{number, second}] = path;
    }
    for (int card = 0; card < 2; ++card) {
        paths.clear();
        if (!ListDirectory(PathsSaveRoot() / (card ? "mc1" : "mc0"), paths)) list.read_failed = true;
        for (const auto &dir : paths) {
            bool second;
            int number = SaveSlotNumber(dir.filename().string(), second);
            if (number < 0 || second) continue;
            auto key = std::make_pair(number, card != 0);
            if (entries.contains(key)) continue;
            auto source = dir / dir.filename();
            auto target = SaveSlotPath(number, card != 0);
            Import(source, target);
            std::error_code error;
            // An incomplete/missing legacy payload still owns a row and a name.
            entries[key] = fs::exists(target, error) ? target : source;
        }
    }
    auto image = std::make_unique<SAVEDATA_FORMAT>();
    for (const auto &[key, path] : entries) {
        SaveSlot slot;
        slot.number = key.first;
        slot.second_card = key.second;
        slot.path = path;
        slot.state = SaveSlotRead(path, *image);
        slot.info.file_no = slot.number;
        if (slot.state == SaveSlotState::Ready) {
            auto &info = slot.info;
            info.state = 1;
            info.map_no = image->map_no;
            info.program_loop_no = image->program_loop_no;
            info.dungeon_no = image->dungeon_no;
            info.floor_id = image->floor_id;
            info.progress = image->save_data.game_progress;
            info.play_time = image->save_data.play_time;
            info.unique_counter = image->unique_counter;
            info.omake_flag = image->omake_flag;
            info.costume_bit = image->costume_bit;
            info.debug_code = image->debug_code;
            info.fish_num = image->fish_num;
        }
        list.saves.push_back(slot);
    }
    return list;
}
