#pragma once

#include <filesystem>
#include <optional>

// Rebuilds the index of the extracted data under root: every regular file, keyed by its path below
// root, lower-cased with bytes >= 0x80 and '%' percent-encoded as dc2data writes them. Logs its count
// to stderr.
void DataReadIndex(const std::filesystem::path &root);

// The file a game path names, through the same device, current-directory and version rules the
// loaders use; empty when the index holds no such file.
std::optional<std::filesystem::path> DataReadFind(const char *game_name);
