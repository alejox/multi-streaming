#include "persistence/vertical_settings_file.hpp"

#include <fstream>
#include <sstream>
#include <system_error>

namespace multistream {

VerticalSettings load_vertical_settings(const std::filesystem::path &path)
{
	std::ifstream file(path);
	if (!file) {
		return {};
	}
	std::ostringstream text;
	text << file.rdbuf();
	return decode_vertical_settings(text.str()).value_or(VerticalSettings{});
}

std::optional<std::string> save_vertical_settings(const std::filesystem::path &path, const VerticalSettings &settings)
{
	std::error_code error;
	std::filesystem::create_directories(path.parent_path(), error);

	std::filesystem::path temp_path = path;
	temp_path += ".tmp";
	{
		std::ofstream file(temp_path, std::ios::trunc);
		file << encode_vertical_settings(settings);
		file.flush();
		if (!file) {
			return "Could not write " + temp_path.string();
		}
	}

	std::filesystem::rename(temp_path, path, error);
	if (error) {
		return "Could not replace " + path.string() + ": " + error.message();
	}
	return std::nullopt;
}

} // namespace multistream
