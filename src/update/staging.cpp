#include "update/staging.hpp"

#include <json.hpp>

#include <vector>

namespace multistream {
namespace {

using nlohmann::json;

constexpr std::size_t kMaxFileNameLength = 200;

bool is_separator(char c)
{
	return c == '/' || c == '\\';
}

// Splits on either separator, keeping the offset where each component starts.
struct Component {
	std::size_t offset;
	std::string_view name;
};

std::vector<Component> split_path(std::string_view path)
{
	std::vector<Component> components;
	std::size_t start = 0;
	for (std::size_t i = 0; i <= path.size(); ++i) {
		if (i == path.size() || is_separator(path[i])) {
			if (i > start) {
				components.push_back({start, path.substr(start, i - start)});
			}
			start = i + 1;
		}
	}
	return components;
}

bool is_lower_hex(std::string_view text)
{
	for (const char c : text) {
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
			return false;
		}
	}
	return true;
}

} // namespace

std::string encode_staged_update(const StagedUpdate &staged)
{
	const json document = {{"version", staged.version},
			       {"archive", staged.archive_name},
			       {"sha256", staged.sha256},
			       {"size", staged.size}};
	return document.dump(2);
}

std::optional<StagedUpdate> decode_staged_update(std::string_view text)
{
	try {
		const json parsed = json::parse(text.begin(), text.end());
		if (!parsed.is_object()) {
			return std::nullopt;
		}
		StagedUpdate staged;
		staged.version = parsed.value("version", "");
		staged.archive_name = parsed.value("archive", "");
		staged.sha256 = parsed.value("sha256", "");
		const auto size = parsed.find("size");
		if (size == parsed.end() || !size->is_number_unsigned()) {
			return std::nullopt;
		}
		staged.size = size->get<std::uint64_t>();
		if (!parse_semver(staged.version) || !is_safe_file_name(staged.archive_name)) {
			return std::nullopt;
		}
		if (!staged.sha256.empty() && (staged.sha256.size() != 64 || !is_lower_hex(staged.sha256))) {
			return std::nullopt;
		}
		return staged;
	} catch (const json::exception &) {
		return std::nullopt;
	}
}

bool is_safe_file_name(std::string_view name)
{
	if (name.empty() || name.size() > kMaxFileNameLength || name.front() == '.') {
		return false;
	}
	for (const char c : name) {
		if (is_separator(c) || c == ':' || static_cast<unsigned char>(c) < 0x20) {
			return false;
		}
	}
	return true;
}

std::string_view package_root_name(UpdatePlatform platform)
{
	return platform == UpdatePlatform::MacosArm64 ? "obs-multi-stream.plugin" : "obs-multi-stream";
}

std::optional<std::string> install_root_from_binary(std::string_view binary_path, UpdatePlatform platform)
{
	const std::vector<Component> parts = split_path(binary_path);
	const std::string_view root = package_root_name(platform);

	// The root must sit directly in a "plugins" folder, the per-user (or ProgramData) plugin
	// location OBS scans; anything else belongs to an installer or a build tree.
	std::size_t root_index = 0;
	if (platform == UpdatePlatform::MacosArm64) {
		// <plugins>/obs-multi-stream.plugin/Contents/MacOS/<binary>
		if (parts.size() < 5 || parts[parts.size() - 3].name != "Contents" ||
		    parts[parts.size() - 2].name != "MacOS") {
			return std::nullopt;
		}
		root_index = parts.size() - 4;
	} else {
		// <plugins>/obs-multi-stream/bin/64bit/<binary>
		if (parts.size() < 5 || parts[parts.size() - 3].name != "bin" ||
		    parts[parts.size() - 2].name != "64bit") {
			return std::nullopt;
		}
		root_index = parts.size() - 4;
	}
	if (parts[root_index].name != root || parts[root_index - 1].name != "plugins") {
		return std::nullopt;
	}
	const Component &last = parts[root_index];
	return std::string(binary_path.substr(0, last.offset + last.name.size()));
}

} // namespace multistream
