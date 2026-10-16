#include "identity.hpp"

#include "icon/chronicletwo.hpp"
#include "platform/game_icon.hpp"
#include "platform/paths.hpp"
#include "platform/settings.hpp"
#include "platform/window.hpp"

void GameSetIdentity() {
    SettingsSetEnvPrefix("DC2");
    PathsConfigure({.app_dir = "chronicletwo", .env_data = "DC2_DATA", .env_save = "DC2_SAVE",
                    .installed_save_default = true, .coherent_default_root = true});
    WindowSetIdentity({.title = "Dark Chronicle",
                       .app_id = "chronicletwo",
                       .icon_rgba = kIconRgba,
                       .icon_size = kIconSize});
}

void GameSetDataIcon() {
    // Every language folder's save.pac carries the same memory card icon; English is checked first.
    GameIconSource source;
    for (const char *language : {"1", "0", "2", "3", "4", "5"}) {
        source.packs.push_back(std::filesystem::path("menu") / language / "save.pac");
    }
    source.member = "dc2_ic.ico";
    source.ico_file = L"chronicletwo.ico";
    source.shortcut = L"Dark Chronicle";
    GameIconInstall(PathsDataRoot(), source);
}
