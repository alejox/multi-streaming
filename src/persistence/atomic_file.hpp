// Durable file replacement shared by every settings file the plugin writes. It follows
// the scheme OBS uses for its own config (os_quick_write_utf8_file_safe with "tmp" and
// "bak"): the new content goes to a temporary file that is synced and renamed over the
// target, and the previous version is kept next to it as "<name>.bak", so that loading
// can recover when the target is damaged or missing.
//
// It is portable C++ rather than a call into libobs because the persistence adapters are
// also linked into the standalone tests, which build without OBS.
#pragma once

#include "destinations/message.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace multistream {

std::filesystem::path backup_path(const std::filesystem::path &path);

// Reads the whole file. Returns false when it cannot be opened or read.
bool read_whole_file(const std::filesystem::path &path, std::string &content);

// Atomically replaces the file with a synced copy of the content, which is private to the
// user on POSIX. The file's previous content first becomes the backup. On failure the
// file is untouched and the message says what went wrong.
std::optional<Message> replace_file_keeping_backup(const std::filesystem::path &path,
						       const std::string &content);

} // namespace multistream
