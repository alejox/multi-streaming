#include "persistence/file_config_repository.hpp"

#include "destinations/config_codec.hpp"
#include "persistence/atomic_file.hpp"

#include <variant>

namespace multistream {
namespace fs = std::filesystem;
namespace {

// Paths in messages are UTF-8, as OBS logs expect. Unlike string(), u8string() cannot throw
// on Windows for a path outside the system code page.
std::string display(const fs::path &path)
{
	return path.u8string();
}

// Moves the file to the first unused "<name>.corrupt-N" and returns that path, or
// an empty path on failure.
fs::path move_aside(const fs::path &path)
{
	for (int suffix = 1; suffix <= 1000; ++suffix) {
		fs::path target = path;
		target += ".corrupt-" + std::to_string(suffix);

		std::error_code error;
		if (fs::exists(fs::symlink_status(target, error))) {
			continue;
		}
		fs::rename(path, target, error);
		if (error) {
			return {};
		}
		return target;
	}
	return {};
}

// The backup from the last save, if one exists and can be used.
std::optional<DestinationsConfig> read_backup(const fs::path &path)
{
	std::string content;
	if (!read_whole_file(backup_path(path), content)) {
		return std::nullopt;
	}
	auto decoded = decode_config(content);
	if (auto *config = std::get_if<DestinationsConfig>(&decoded)) {
		return std::move(*config);
	}
	return std::nullopt;
}

} // namespace

ConfigLoad FileConfigRepository::refuse_saving(Message problem)
{
	safe_to_save_ = false;
	return {{}, std::move(problem)};
}

ConfigLoad FileConfigRepository::load()
{
	std::error_code error;
	const fs::file_status status = fs::symlink_status(path_, error);
	if (error && status.type() != fs::file_type::not_found) {
		return refuse_saving(Message("Config.CheckFailed", {display(path_), error.message()}));
	}
	if (status.type() == fs::file_type::not_found) {
		safe_to_save_ = true;
		if (auto backup = read_backup(path_)) {
			return {std::move(*backup), Message("Config.RestoredMissing", {display(path_)})};
		}
		return {};
	}

	std::string content;
	if (!fs::is_regular_file(status) && !fs::is_symlink(status)) {
		return refuse_saving(Message("Config.NotRegularFile", {display(path_)}));
	}
	if (!read_whole_file(path_, content)) {
		return refuse_saving(Message("Config.ReadFailed", {display(path_)}));
	}

	auto decoded = decode_config(content);
	if (auto *config = std::get_if<DestinationsConfig>(&decoded)) {
		safe_to_save_ = true;
		return {std::move(*config), std::nullopt};
	}

	const ConfigError &config_error = std::get<ConfigError>(decoded);
	const fs::path preserved = move_aside(path_);
	if (preserved.empty()) {
		return refuse_saving(Message("Config.UnusableKept", {display(path_), config_error.detail}));
	}

	safe_to_save_ = true;
	if (auto backup = read_backup(path_)) {
		return {std::move(*backup),
			Message("Config.UnusableRestored", {config_error.detail, display(preserved)})};
	}
	return {{}, Message("Config.UnusableReset", {config_error.detail, display(preserved)})};
}

std::optional<Message> FileConfigRepository::save(const DestinationsConfig &config)
{
	if (!safe_to_save_) {
		return Message("Config.SavingDisabled", {display(path_)});
	}

	std::error_code error;
	fs::create_directories(path_.parent_path(), error);
	if (error) {
		return Message("Storage.CreateDirFailed", {display(path_.parent_path()), error.message()});
	}

	return replace_file_keeping_backup(path_, encode_config(config));
}

} // namespace multistream
