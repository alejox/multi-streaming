#include "persistence/file_config_repository.hpp"

#include "destinations/config_codec.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <variant>

namespace multistream {
namespace fs = std::filesystem;
namespace {

std::string errno_text()
{
	return std::strerror(errno);
}

bool read_whole_file(const fs::path &path, std::string &content)
{
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		return false;
	}
	content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	return !file.bad();
}

bool write_and_sync(int descriptor, const std::string &content)
{
	std::size_t written = 0;
	while (written < content.size()) {
		const ssize_t count = write(descriptor, content.data() + written, content.size() - written);
		if (count < 0) {
			if (errno == EINTR) {
				continue;
			}
			return false;
		}
		written += static_cast<std::size_t>(count);
	}
	return fsync(descriptor) == 0;
}

// Best effort: the rename already happened, this only makes it durable sooner.
void sync_directory(const fs::path &directory)
{
	const int descriptor = open(directory.c_str(), O_RDONLY);
	if (descriptor >= 0) {
		fsync(descriptor);
		close(descriptor);
	}
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
		if (std::rename(path.c_str(), target.c_str()) != 0) {
			return {};
		}
		return target;
	}
	return {};
}

} // namespace

ConfigLoad FileConfigRepository::refuse_saving(std::string problem)
{
	safe_to_save_ = false;
	return {{}, std::move(problem)};
}

ConfigLoad FileConfigRepository::load()
{
	std::error_code error;
	const fs::file_status status = fs::symlink_status(path_, error);
	if (error && status.type() != fs::file_type::not_found) {
		return refuse_saving("The destinations config at " + path_.string() +
				     " could not be checked: " + error.message() + ". Saving is disabled.");
	}
	if (status.type() == fs::file_type::not_found) {
		safe_to_save_ = true;
		return {};
	}

	std::string content;
	if (!fs::is_regular_file(status) && !fs::is_symlink(status)) {
		return refuse_saving("The destinations config at " + path_.string() +
				     " is not a regular file. Saving is disabled.");
	}
	if (!read_whole_file(path_, content)) {
		return refuse_saving("The destinations config at " + path_.string() +
				     " could not be read. It was left in place and saving is disabled.");
	}

	auto decoded = decode_config(content);
	if (auto *config = std::get_if<DestinationsConfig>(&decoded)) {
		safe_to_save_ = true;
		return {std::move(*config), std::nullopt};
	}

	const ConfigError &config_error = std::get<ConfigError>(decoded);
	const fs::path preserved = move_aside(path_);
	if (preserved.empty()) {
		return refuse_saving("The destinations config at " + path_.string() +
				     " is unusable (" + config_error.detail +
				     ") and could not be moved aside. It was left in place and saving is disabled.");
	}

	safe_to_save_ = true;
	return {{},
		"The destinations config was unusable (" + config_error.detail + "). It was preserved at " +
			preserved.string() + " and the plugin started with no destinations."};
}

std::optional<std::string> FileConfigRepository::save(const DestinationsConfig &config)
{
	if (!safe_to_save_) {
		return "Saving is disabled because the existing destinations config at " + path_.string() +
		       " has not been loaded successfully.";
	}

	std::error_code error;
	fs::create_directories(path_.parent_path(), error);
	if (error) {
		return "Could not create " + path_.parent_path().string() + ": " + error.message();
	}

	// mkstemp creates the file with mode 0600, and the rename keeps that mode.
	std::string temp_path = path_.string() + ".tmp-XXXXXX";
	const int descriptor = mkstemp(temp_path.data());
	if (descriptor < 0) {
		return "Could not create a temporary file next to " + path_.string() + ": " + errno_text();
	}

	const bool written = write_and_sync(descriptor, encode_config(config));
	const std::string write_error = written ? std::string() : errno_text();
	close(descriptor);
	if (!written) {
		unlink(temp_path.c_str());
		return "Could not write " + temp_path + ": " + write_error;
	}

	if (std::rename(temp_path.c_str(), path_.c_str()) != 0) {
		const std::string rename_error = errno_text();
		unlink(temp_path.c_str());
		return "Could not replace " + path_.string() + ": " + rename_error;
	}

	sync_directory(path_.parent_path());
	return std::nullopt;
}

} // namespace multistream
