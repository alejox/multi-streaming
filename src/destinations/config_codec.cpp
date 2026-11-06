#include "destinations/config_codec.hpp"

#include <json.hpp>

#include <limits>
#include <optional>
#include <set>

namespace multistream {
namespace {

using nlohmann::json;

struct EntryError {
	std::string detail;
};

const char *to_json_name(VideoSource source)
{
	return source == VideoSource::Vertical ? "vertical" : "main";
}

const char *to_json_name(EncoderMode mode)
{
	return mode == EncoderMode::Custom ? "custom" : "share";
}

json encode_destination(const Destination &destination)
{
	return {
		{"id", destination.id},
		{"name", destination.name},
		{"server_url", destination.server_url},
		{"enabled", destination.enabled},
		{"video_source", to_json_name(destination.video_source)},
		{"encoder",
		 {{"mode", to_json_name(destination.encoder.mode)},
		  {"video_bitrate_kbps", destination.encoder.video_bitrate_kbps},
		  {"video_encoder_id", destination.encoder.video_encoder_id}}},
		{"audio_track", destination.audio_track},
		{"sync_with_main", destination.sync_with_main},
		{"reconnect",
		 {{"max_retries", destination.reconnect.max_retries()},
		  {"base_delay_seconds", destination.reconnect.base_delay_seconds()},
		  {"max_delay_seconds", destination.reconnect.max_delay_seconds()}}},
	};
}

std::string required_string(const json &object, const char *field)
{
	const auto found = object.find(field);
	if (found == object.end() || !found->is_string()) {
		throw EntryError{std::string("\"") + field + "\" must be a string"};
	}
	return found->get<std::string>();
}

std::string optional_string(const json &object, const char *field, std::string fallback)
{
	return object.contains(field) ? required_string(object, field) : std::move(fallback);
}

bool optional_bool(const json &object, const char *field, bool fallback)
{
	const auto found = object.find(field);
	if (found == object.end()) {
		return fallback;
	}
	if (!found->is_boolean()) {
		throw EntryError{std::string("\"") + field + "\" must be a boolean"};
	}
	return found->get<bool>();
}

int optional_int(const json &object, const char *field, int fallback)
{
	const auto found = object.find(field);
	if (found == object.end()) {
		return fallback;
	}
	if (!found->is_number_integer() || found->get<json::number_integer_t>() < std::numeric_limits<int>::min() ||
	    found->get<json::number_integer_t>() > std::numeric_limits<int>::max()) {
		throw EntryError{std::string("\"") + field + "\" must be an integer"};
	}
	return static_cast<int>(found->get<json::number_integer_t>());
}

const json *optional_object(const json &object, const char *field)
{
	const auto found = object.find(field);
	if (found == object.end()) {
		return nullptr;
	}
	if (!found->is_object()) {
		throw EntryError{std::string("\"") + field + "\" must be an object"};
	}
	return &*found;
}

VideoSource decode_video_source(const json &entry)
{
	const std::string name = optional_string(entry, "video_source", "main");
	if (name == "main") {
		return VideoSource::Main;
	}
	if (name == "vertical") {
		return VideoSource::Vertical;
	}
	throw EntryError{"unknown video_source \"" + name + "\""};
}

EncoderPreference decode_encoder(const json &entry)
{
	EncoderPreference encoder;
	const json *object = optional_object(entry, "encoder");
	if (object == nullptr) {
		return encoder;
	}

	const std::string mode = optional_string(*object, "mode", "share");
	if (mode == "custom") {
		encoder.mode = EncoderMode::Custom;
	} else if (mode != "share") {
		throw EntryError{"unknown encoder mode \"" + mode + "\""};
	}
	encoder.video_bitrate_kbps = optional_int(*object, "video_bitrate_kbps", 0);
	encoder.video_encoder_id = optional_string(*object, "video_encoder_id", "");
	return encoder;
}

ReconnectPolicy decode_reconnect(const json &entry)
{
	const ReconnectPolicy defaults;
	const json *object = optional_object(entry, "reconnect");
	if (object == nullptr) {
		return defaults;
	}

	const auto policy =
		ReconnectPolicy::create(optional_int(*object, "max_retries", defaults.max_retries()),
					optional_int(*object, "base_delay_seconds", defaults.base_delay_seconds()),
					optional_int(*object, "max_delay_seconds", defaults.max_delay_seconds()));
	if (!policy) {
		throw EntryError{"\"reconnect\" values are out of range"};
	}
	return *policy;
}

Destination decode_destination(const json &entry)
{
	if (!entry.is_object()) {
		throw EntryError{"a destination must be an object"};
	}

	Destination destination;
	destination.id = required_string(entry, "id");
	destination.name = required_string(entry, "name");
	destination.server_url = required_string(entry, "server_url");
	destination.enabled = optional_bool(entry, "enabled", destination.enabled);
	destination.video_source = decode_video_source(entry);
	destination.encoder = decode_encoder(entry);
	destination.audio_track = optional_int(entry, "audio_track", destination.audio_track);
	destination.sync_with_main = optional_bool(entry, "sync_with_main", destination.sync_with_main);
	destination.reconnect = decode_reconnect(entry);
	return destination;
}

} // namespace

std::string encode_config(const DestinationsConfig &config)
{
	json destinations = json::array();
	for (const Destination &destination : config.destinations) {
		destinations.push_back(encode_destination(destination));
	}

	const json document = {{"schema_version", kConfigSchemaVersion}, {"destinations", std::move(destinations)}};
	// Replacing invalid UTF-8 keeps a bad name from turning a save into an exception.
	return document.dump(2, ' ', false, json::error_handler_t::replace) + "\n";
}

std::variant<DestinationsConfig, ConfigError> decode_config(std::string_view text)
{
	const json document = json::parse(text.begin(), text.end(), nullptr, false);
	if (document.is_discarded() || !document.is_object()) {
		return ConfigError{ConfigErrorKind::Malformed, "the file is not a JSON object"};
	}

	const auto version = document.find("schema_version");
	if (version == document.end() || !version->is_number_integer() ||
	    version->get<json::number_integer_t>() < 1) {
		return ConfigError{ConfigErrorKind::Malformed, "\"schema_version\" is missing or invalid"};
	}
	if (version->get<json::number_integer_t>() > kConfigSchemaVersion) {
		return ConfigError{ConfigErrorKind::UnsupportedSchema,
				   "schema version " + std::to_string(version->get<json::number_integer_t>()) +
					   " is newer than this plugin supports"};
	}

	const auto entries = document.find("destinations");
	if (entries == document.end() || !entries->is_array()) {
		return ConfigError{ConfigErrorKind::Malformed, "\"destinations\" is missing or not an array"};
	}

	DestinationsConfig config;
	std::set<std::string> seen_ids;
	std::size_t index = 0;
	for (const json &entry : *entries) {
		try {
			config.destinations.push_back(decode_destination(entry));
		} catch (const EntryError &error) {
			return ConfigError{ConfigErrorKind::InvalidEntry,
					   "destination " + std::to_string(index) + ": " + error.detail};
		}
		if (!seen_ids.insert(config.destinations.back().id).second) {
			return ConfigError{ConfigErrorKind::InvalidEntry,
					   "destination " + std::to_string(index) + ": duplicate id"};
		}
		++index;
	}
	return config;
}

} // namespace multistream
