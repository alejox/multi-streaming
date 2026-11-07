#include "oauth/twitch_oauth.hpp"

#include <json.hpp>

#include <cstddef>

namespace multistream {
namespace {

using nlohmann::json;

bool is_unreserved(unsigned char character)
{
	return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
	       (character >= '0' && character <= '9') || character == '-' || character == '_' ||
	       character == '.' || character == '~';
}

std::string percent_encode(std::string_view value)
{
	static constexpr char kHex[] = "0123456789ABCDEF";
	std::string out;
	out.reserve(value.size());
	for (const unsigned char character : value) {
		if (is_unreserved(character)) {
			out += static_cast<char>(character);
		} else {
			out += '%';
			out += kHex[character >> 4];
			out += kHex[character & 0x0f];
		}
	}
	return out;
}

int hex_digit(char character)
{
	if (character >= '0' && character <= '9') {
		return character - '0';
	}
	if (character >= 'A' && character <= 'F') {
		return character - 'A' + 10;
	}
	if (character >= 'a' && character <= 'f') {
		return character - 'a' + 10;
	}
	return -1;
}

std::string percent_decode(std::string_view value)
{
	std::string out;
	out.reserve(value.size());
	for (std::size_t i = 0; i < value.size(); ++i) {
		if (value[i] == '%' && i + 2 < value.size()) {
			const int high = hex_digit(value[i + 1]);
			const int low = hex_digit(value[i + 2]);
			if (high >= 0 && low >= 0) {
				out += static_cast<char>((high << 4) | low);
				i += 2;
				continue;
			}
		}
		out += value[i];
	}
	return out;
}

void append_param(std::string &out, std::string_view key, std::string_view value)
{
	if (!out.empty()) {
		out += '&';
	}
	out.append(key);
	out += '=';
	out += percent_encode(value);
}

std::optional<std::string> query_value(std::string_view query, std::string_view key)
{
	std::size_t position = 0;
	while (position <= query.size()) {
		const std::size_t ampersand = query.find('&', position);
		const std::size_t length = ampersand == std::string_view::npos ? query.size() - position
									     : ampersand - position;
		const std::string_view pair = query.substr(position, length);
		const std::size_t equals = pair.find('=');
		if (equals != std::string_view::npos && pair.substr(0, equals) == key) {
			return std::string(pair.substr(equals + 1));
		}
		if (ampersand == std::string_view::npos) {
			break;
		}
		position = ampersand + 1;
	}
	return std::nullopt;
}

} // namespace

std::string twitch_authorize_url(std::string_view client_id, std::string_view redirect_uri,
				 std::string_view state, std::string_view code_challenge)
{
	std::string query;
	append_param(query, "client_id", client_id);
	append_param(query, "redirect_uri", redirect_uri);
	append_param(query, "response_type", "code");
	append_param(query, "scope", kTwitchStreamKeyScope);
	append_param(query, "code_challenge", code_challenge);
	append_param(query, "code_challenge_method", "S256");
	append_param(query, "state", state);

	std::string out;
	out.reserve(kTwitchAuthorizeEndpoint.size() + query.size() + 1);
	out.append(kTwitchAuthorizeEndpoint);
	out += '?';
	out += query;
	return out;
}

TwitchRedirect parse_redirect_query(std::string_view query)
{
	TwitchRedirect redirect;
	if (const auto code = query_value(query, "code")) {
		redirect.code = percent_decode(*code);
	}
	if (const auto state = query_value(query, "state")) {
		redirect.state = percent_decode(*state);
	}
	if (const auto error = query_value(query, "error")) {
		redirect.error = percent_decode(*error);
	}
	if (const auto description = query_value(query, "error_description")) {
		redirect.error_description = percent_decode(*description);
	}
	return redirect;
}

std::string redirect_uri_path(std::string_view redirect_uri)
{
	std::size_t authority = redirect_uri.find("://");
	authority = authority == std::string_view::npos ? 0 : authority + 3;
	const std::size_t path_start = redirect_uri.find('/', authority);
	if (path_start == std::string_view::npos) {
		return "/";
	}
	const std::string_view rest = redirect_uri.substr(path_start);
	return std::string(rest.substr(0, rest.find_first_of("?#")));
}

std::optional<std::string> redirect_request_query(std::string_view request_line, std::string_view expected_path)
{
	const std::size_t first_space = request_line.find(' ');
	if (first_space == std::string_view::npos || request_line.substr(0, first_space) != "GET") {
		return std::nullopt;
	}
	const std::size_t second_space = request_line.find(' ', first_space + 1);
	if (second_space == std::string_view::npos) {
		return std::nullopt;
	}
	const std::string_view target = request_line.substr(first_space + 1, second_space - first_space - 1);
	const std::size_t question = target.find('?');
	if (target.substr(0, question) != expected_path) {
		return std::nullopt;
	}
	return question == std::string_view::npos ? std::string() : std::string(target.substr(question + 1));
}

std::string token_exchange_body(std::string_view client_id, std::string_view code, std::string_view redirect_uri,
				std::string_view code_verifier)
{
	std::string out;
	out.reserve(256);
	append_param(out, "client_id", client_id);
	append_param(out, "code", code);
	append_param(out, "grant_type", "authorization_code");
	append_param(out, "redirect_uri", redirect_uri);
	append_param(out, "code_verifier", code_verifier);
	return out;
}

std::string token_refresh_body(std::string_view client_id, std::string_view refresh_token)
{
	std::string out;
	out.reserve(160);
	append_param(out, "client_id", client_id);
	append_param(out, "refresh_token", refresh_token);
	append_param(out, "grant_type", "refresh_token");
	return out;
}

TwitchTokenResponse parse_token_response(std::string_view json_text)
{
	TwitchTokenResponse response;
	try {
		const json parsed = json::parse(json_text.begin(), json_text.end());
		if (parsed.contains("access_token") && parsed["access_token"].is_string()) {
			response.access_token = parsed["access_token"].get<std::string>();
		}
		if (parsed.contains("refresh_token") && parsed["refresh_token"].is_string()) {
			response.refresh_token = parsed["refresh_token"].get<std::string>();
		}
		if (parsed.contains("expires_in") && parsed["expires_in"].is_number_integer()) {
			response.expires_in_seconds = parsed["expires_in"].get<int>();
		}
		if (parsed.contains("error") && parsed["error"].is_string()) {
			response.error = parsed["error"].get<std::string>();
		}
		const std::string description = parsed.value("error_description", parsed.value("message", ""));
		if (!description.empty()) {
			response.error_description = description;
		}
	} catch (const json::exception &exception) {
		response.error = "invalid_response";
		response.error_description = exception.what();
	}
	return response;
}

} // namespace multistream
