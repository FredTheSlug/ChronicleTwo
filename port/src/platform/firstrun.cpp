#include "platform/firstrun.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#include "exitcodes.hpp"
#include "platform/paths.hpp"

namespace fs = std::filesystem;

bool FirstRunDataMissing(const fs::path &root) {
    std::error_code error;
    if (!fs::is_directory(root, error)) {
        return true;
    }
    return !fs::is_regular_file(root / "manifest.txt", error);
}

std::string FirstRunDataMessage(const fs::path &root) {
    const std::string where = PathsDisplay(root);
    return "No extracted Dark Chronicle data at " + where + ".\n\n"
           "Extract it once from the disc with:\n"
           "  dc2data extract <disc image or folder> " + where + "\n\n"
           "The source is an image of the Dark Chronicle disc, or a folder holding DATA.HD4, DATA.DAT, SOUND.HD3 and SOUND.DAT.\n\n"
           "Or point the game at an existing extraction with --data <dir> or the DC2_DATA environment variable.\n";
}

void FirstRunRequireData(const fs::path &root, bool headless) {
    if (!FirstRunDataMissing(root)) {
        return;
    }
    const std::string text = FirstRunDataMessage(root);
    std::fputs(text.c_str(), stderr);
    if (!headless) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Dark Chronicle", text.c_str(), nullptr);
    }
    std::exit(kExitNoData);
}
