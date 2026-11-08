#include "update/update_settings.hpp"

#include <json.hpp>

namespace multistream {

using nlohmann::json;

std::string encode_update_settings(const UpdateSettings &settings)
{
	const json document = {{"auto_install", settings.auto_install}};
	return document.dump(2);
}

std::optional<UpdateSettings> decode_update_settings(const std::string &text)
{
	const json document = json::parse(text, nullptr, false);
	if (!document.is_object()) {
		return std::nullopt;
	}
	UpdateSettings settings;
	const auto auto_install = document.find("auto_install");
	if (auto_install != document.end() && auto_install->is_boolean()) {
		settings.auto_install = auto_install->get<bool>();
	}
	return settings;
}

} // namespace multistream
