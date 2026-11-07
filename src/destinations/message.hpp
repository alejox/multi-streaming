// A user-visible message without its text: a locale key and the values for the key's
// %1, %2... placeholders. The core stays free of OBS and Qt, and the UI looks the key up
// in data/locale. Values are data such as paths or raw system error text, never prose.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace multistream {

struct Message {
	std::string key;
	std::vector<std::string> args;

	Message() = default;
	explicit Message(std::string message_key, std::vector<std::string> message_args = {})
		: key(std::move(message_key)),
		  args(std::move(message_args))
	{
	}

	bool operator==(const Message &other) const { return key == other.key && args == other.args; }
	bool operator!=(const Message &other) const { return !(*this == other); }
};

} // namespace multistream
