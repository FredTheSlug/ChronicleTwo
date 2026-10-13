#include "identity.hpp"

#include "platform/paths.hpp"
#include "platform/settings.hpp"
#include "platform/window.hpp"

void GameSetIdentity() {
    SettingsSetEnvPrefix("DC2");
    PathsConfigure({.app_dir = "chronicletwo", .env_data = "DC2_DATA", .env_save = "DC2_SAVE",
                    .installed_save_default = true, .coherent_default_root = true});
    WindowSetIdentity({.title = "Dark Chronicle", .app_id = "chronicletwo"});
}
