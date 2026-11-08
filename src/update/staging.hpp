// What a downloaded update leaves behind for the installer, and where the running plugin is
// installed. Pure C++: the Qt service does the file work.
#pragma once

#include "update/update_check.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace multistream {

// The manifest written next to a verified package once it is complete.
struct StagedUpdate {
	std::string version;
	// A bare file name inside the staging directory.
	std::string archive_name;
	// Lowercase hex; empty when the release published no digest and only the size was checked.
	std::string sha256;
	std::uint64_t size = 0;
};

std::string encode_staged_update(const StagedUpdate &staged);

// nullopt when the text is not a complete manifest or names an unsafe file.
std::optional<StagedUpdate> decode_staged_update(std::string_view text);

// Whether `name` can be used as a file name inside the staging directory: no separators,
// no parent references, nothing hidden, and not too long.
bool is_safe_file_name(std::string_view name);

// The top-level directory inside a package, which is also the installed directory's name:
// "obs-multi-stream.plugin" on macOS and "obs-multi-stream" elsewhere.
std::string_view package_root_name(UpdatePlatform platform);

// The installed plugin directory from the loaded binary's path, such as
// ".../plugins/obs-multi-stream.plugin" for ".../obs-multi-stream.plugin/Contents/MacOS/obs-multi-stream"
// or ".../plugins/obs-multi-stream" for ".../obs-multi-stream/bin/64bit/obs-multi-stream.dll".
// nullopt for any other layout (a system-wide package install, for instance), which the
// updater must not touch.
std::optional<std::string> install_root_from_binary(std::string_view binary_path, UpdatePlatform platform);

} // namespace multistream
