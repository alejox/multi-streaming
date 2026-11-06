#include "vertical_canvas/source_catalog.hpp"

#include <algorithm>
#include <cctype>

namespace multistream {
namespace {

std::string lowercase(std::string_view text)
{
	std::string result(text);
	std::transform(result.begin(), result.end(), result.begin(),
		       [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
	return result;
}

std::string_view trimmed(std::string_view text)
{
	const auto first = text.find_first_not_of(" \t");
	if (first == std::string_view::npos) {
		return {};
	}
	return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

} // namespace

std::vector<InputType> creatable_video_types(std::vector<InputType> types)
{
	types.erase(std::remove_if(types.begin(), types.end(),
				   [](const InputType &type) {
					   return !type.has_video || type.disabled || type.deprecated;
				   }),
		    types.end());
	std::sort(types.begin(), types.end(), [](const InputType &left, const InputType &right) {
		return lowercase(left.display_name) < lowercase(right.display_name);
	});
	return types;
}

std::vector<std::string> filter_names(const std::vector<std::string> &names, std::string_view query)
{
	const std::string wanted = lowercase(trimmed(query));
	std::vector<std::string> result;
	for (const std::string &name : names) {
		if (lowercase(name).find(wanted) != std::string::npos) {
			result.push_back(name);
		}
	}
	std::sort(result.begin(), result.end(),
		  [](const std::string &left, const std::string &right) { return lowercase(left) < lowercase(right); });
	return result;
}

} // namespace multistream
