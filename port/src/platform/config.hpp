#pragma once

#include <string>
#include <string_view>

#include "platform/settings.hpp"

// The disc is the PAL release, so its logic runs at the PAL field rate, not the NTSC 60 the host defaults to.
inline constexpr double kConfigTickRateDefault = 50.0;
inline constexpr double kConfigTickRateMin = 25.0;
inline constexpr double kConfigTickRateMax = 240.0;

struct Config : HostSettings {
    Config() { tick_rate = kConfigTickRateDefault; }

    bool operator==(const Config &) const = default;
};

// <save root>/config.json. A missing file is written with the defaults; returns whether it was read.
bool ConfigLoad();
bool ConfigSave();

// Unknown keys and values of the wrong type or range keep their defaults and are reported on stderr.
Config      ConfigParse(std::string_view text);
std::string ConfigSerialize(const Config &config);
