#include "persistence/vertical_settings_file.hpp"

#include "persistence/atomic_file.hpp"

#include <system_error>

namespace multistream {
namespace {

std::optional<VerticalSettings> read_settings(const std::filesystem::path &path)
{
	std::string text;
	if (!read_whole_file(path, text)) {
		return std::nullopt;
	}
	return decode_vertical_settings(text);
}

} // namespace

VerticalSettings load_vertical_settings(const std::filesystem::path &path)
{
	if (auto settings = read_settings(path)) {
		return *settings;
	}
	return read_settings(backup_path(path)).value_or(VerticalSettings{});
}

std::optional<Message> save_vertical_settings(const std::filesystem::path &path, const VerticalSettings &settings)
{
	std::error_code error;
	std::filesystem::create_directories(path.parent_path(), error);
	if (error) {
		return Message("Storage.CreateDirFailed", {path.parent_path().u8string(), error.message()});
	}
	return replace_file_keeping_backup(path, encode_vertical_settings(settings));
}

} // namespace multistream
