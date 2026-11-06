#include "destinations/validation.hpp"

#include <algorithm>
#include <cctype>

namespace multistream {
namespace {

constexpr std::size_t kMaxIdLength = 64;
constexpr int kAudioMixCount = 6;
constexpr int kMaxVideoBitrateKbps = 500000;

bool is_ascii_alphanumeric(char character)
{
	return std::isalnum(static_cast<unsigned char>(character)) != 0;
}

bool is_ascii_hex_digit(char character)
{
	return std::isxdigit(static_cast<unsigned char>(character)) != 0;
}

bool is_ascii_digit(char character)
{
	return std::isdigit(static_cast<unsigned char>(character)) != 0;
}

bool is_blank_or_control(char character)
{
	const auto byte = static_cast<unsigned char>(character);
	return byte <= 0x20 || byte == 0x7f;
}

bool equals_ignoring_case(std::string_view left, std::string_view right)
{
	return std::equal(left.begin(), left.end(), right.begin(), right.end(), [](char a, char b) {
		return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
	});
}

bool is_valid_id(std::string_view id)
{
	return !id.empty() && id.size() <= kMaxIdLength && std::all_of(id.begin(), id.end(), [](char character) {
		return is_ascii_alphanumeric(character) || character == '-' || character == '_';
	});
}

bool is_valid_hostname(std::string_view host)
{
	return !host.empty() && std::all_of(host.begin(), host.end(), [](char character) {
		return is_ascii_alphanumeric(character) || character == '.' || character == '-' || character == '_';
	});
}

bool is_valid_ip_literal(std::string_view address)
{
	return !address.empty() && std::all_of(address.begin(), address.end(), [](char character) {
		return is_ascii_hex_digit(character) || character == ':' || character == '.';
	});
}

bool is_valid_port(std::string_view port)
{
	if (port.empty() || port.size() > 5 || !std::all_of(port.begin(), port.end(), is_ascii_digit)) {
		return false;
	}
	int value = 0;
	for (char digit : port) {
		value = value * 10 + (digit - '0');
	}
	return value >= 1 && value <= 65535;
}

// Validates "host", "host:port", "[ip]", or "[ip]:port".
std::optional<std::string> validate_authority(std::string_view authority)
{
	std::string_view host = authority;
	std::string_view port;
	bool has_port = false;

	if (!authority.empty() && authority.front() == '[') {
		const auto closing = authority.find(']');
		if (closing == std::string_view::npos || !is_valid_ip_literal(authority.substr(1, closing - 1))) {
			return "The IP address in the server URL is invalid.";
		}
		std::string_view rest = authority.substr(closing + 1);
		if (!rest.empty() && rest.front() != ':') {
			return "The server URL has unexpected text after the IP address.";
		}
		has_port = !rest.empty();
		port = has_port ? rest.substr(1) : rest;
	} else {
		const auto colon = authority.find(':');
		if (colon != std::string_view::npos) {
			host = authority.substr(0, colon);
			port = authority.substr(colon + 1);
			has_port = true;
		}
		if (!is_valid_hostname(host)) {
			return "The server URL needs a valid host name.";
		}
	}

	if (has_port && !is_valid_port(port)) {
		return "The port in the server URL must be a number from 1 to 65535.";
	}
	return std::nullopt;
}

} // namespace

std::optional<std::string> validate_server_url(std::string_view url)
{
	if (std::any_of(url.begin(), url.end(), is_blank_or_control)) {
		return "The server URL must not contain spaces or control characters.";
	}

	const auto scheme_end = url.find("://");
	if (scheme_end == std::string_view::npos) {
		return "The server URL must start with rtmp:// or rtmps://.";
	}
	const std::string_view scheme = url.substr(0, scheme_end);
	if (!equals_ignoring_case(scheme, "rtmp") && !equals_ignoring_case(scheme, "rtmps")) {
		return "The server URL must start with rtmp:// or rtmps://.";
	}

	const std::string_view rest = url.substr(scheme_end + 3);
	if (rest.find_first_of("?#") != std::string_view::npos) {
		return "The server URL must not have a query or fragment. Put the stream key in its own field.";
	}

	const std::string_view authority = rest.substr(0, rest.find('/'));
	if (authority.find('@') != std::string_view::npos) {
		return "The server URL must not contain a user name or password.";
	}
	return validate_authority(authority);
}

std::vector<ValidationIssue> validate_destination(const Destination &destination)
{
	std::vector<ValidationIssue> issues;

	if (!is_valid_id(destination.id)) {
		issues.push_back({DestinationField::Id, "The destination id is invalid."});
	}
	if (std::all_of(destination.name.begin(), destination.name.end(),
			[](char character) { return std::isspace(static_cast<unsigned char>(character)) != 0; })) {
		issues.push_back({DestinationField::Name, "The destination needs a name."});
	}
	if (auto problem = validate_server_url(destination.server_url)) {
		issues.push_back({DestinationField::ServerUrl, std::move(*problem)});
	}
	if (destination.audio_track < 1 || destination.audio_track > kAudioMixCount) {
		issues.push_back({DestinationField::AudioTrack, "The audio track must be from 1 to 6."});
	}
	if (destination.encoder.mode == EncoderMode::Custom) {
		if (destination.encoder.video_bitrate_kbps < 1 ||
		    destination.encoder.video_bitrate_kbps > kMaxVideoBitrateKbps) {
			issues.push_back(
				{DestinationField::VideoBitrate, "The video bitrate must be from 1 to 500000 kbps."});
		}
		if (destination.encoder.video_encoder_id.empty()) {
			issues.push_back({DestinationField::VideoEncoder, "Choose a video encoder."});
		}
	}

	return issues;
}

} // namespace multistream
