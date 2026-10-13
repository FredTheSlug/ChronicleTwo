#pragma once

#include <filesystem>
#include <string>

bool        FirstRunDataMissing(const std::filesystem::path &root);
std::string FirstRunDataMessage(const std::filesystem::path &root);

/**
 * Prepares missing data in a desktop setup window, or exits with kExitNoData if unavailable or cancelled.
 */
void FirstRunRequireData(const std::filesystem::path &root, bool headless);
