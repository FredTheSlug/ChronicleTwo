#pragma once

#include <filesystem>
#include <string>

bool        FirstRunDataMissing(const std::filesystem::path &root);
std::string FirstRunDataMessage(const std::filesystem::path &root);

// Exits with kExitNoData when the data root is not an extraction. A headless run has no desktop to show a dialog on.
void FirstRunRequireData(const std::filesystem::path &root, bool headless);
