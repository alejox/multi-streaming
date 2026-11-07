// Turns core messages into text in the user's language, from the plugin's locale files.
#pragma once

#include "destinations/message.hpp"
#include "destinations/secret_store.hpp"

#include <obs-module.h>

#include <string>

namespace multistream {

// The key's text with %1 to %9 replaced by the message's values in one pass, so a value
// that itself contains "%1" is never substituted again.
inline std::string localize(const Message &message)
{
	const std::string pattern = obs_module_text(message.key.c_str());
	std::string text;
	text.reserve(pattern.size());
	for (std::size_t index = 0; index < pattern.size(); ++index) {
		const char next = index + 1 < pattern.size() ? pattern[index + 1] : '\0';
		if (pattern[index] == '%' && next >= '1' && next <= '9') {
			const auto argument = static_cast<std::size_t>(next - '1');
			if (argument < message.args.size()) {
				text += message.args[argument];
				++index;
				continue;
			}
		}
		text += pattern[index];
	}
	return text;
}

// What went wrong with the secure store, followed by the store's own error text, if any.
inline std::string localize(const SecretError &error)
{
	const char *key = "Secret.Failure";
	switch (error.kind) {
	case SecretError::Kind::Unavailable:
		key = "Secret.Unavailable";
		break;
	case SecretError::Kind::AccessDenied:
		key = "Secret.AccessDenied";
		break;
	case SecretError::Kind::Failure:
		break;
	}
	const std::string summary = obs_module_text(key);
	if (error.message.empty()) {
		return summary;
	}
	return localize(Message("Message.WithDetail", {summary, error.message}));
}

} // namespace multistream
