#include "destinations/platform.hpp"

#include <algorithm>
#include <cctype>

namespace multistream {
namespace {

struct KnownHost {
	std::string_view domain;
	Platform platform;
};

// A host matches its domain or anything under it. Twitch's regional ingest comes from OBS
// 33.0.1's service list. *.global-contribute.live-video.net is left out on purpose, since
// several services share it; it does not end in ".contribute.live-video.net" either.
constexpr std::array<KnownHost, 11> kKnownHosts = {{
	{"youtube.com", Platform::YouTube},
	{"kick.com", Platform::Kick},
	{"twitch.tv", Platform::Twitch},
	{"contribute.live-video.net", Platform::Twitch},
	{"facebook.com", Platform::Facebook},
	{"tiktok.com", Platform::TikTok},
	{"tiktokcdn.com", Platform::TikTok},
	{"pscp.tv", Platform::X},
	{"x.com", Platform::X},
	{"twitter.com", Platform::X},
	{"instagram.com", Platform::Instagram},
}};

std::string lowercase_host(std::string_view url)
{
	const auto scheme = url.find("://");
	if (scheme == std::string_view::npos) {
		return {};
	}
	std::string_view authority = url.substr(scheme + 3);
	authority = authority.substr(0, authority.find_first_of("/?#"));
	if (!authority.empty() && authority.front() == '[') {
		return {};
	}
	authority = authority.substr(0, authority.find(':'));

	std::string host(authority);
	std::transform(host.begin(), host.end(), host.begin(),
		       [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
	return host;
}

bool host_is_under(std::string_view host, std::string_view domain)
{
	if (host == domain) {
		return true;
	}
	return host.size() > domain.size() && host.substr(host.size() - domain.size()) == domain &&
	       host[host.size() - domain.size() - 1] == '.';
}

} // namespace

const char *platform_id(Platform platform)
{
	switch (platform) {
	case Platform::Custom:
		return "custom";
	case Platform::YouTube:
		return "youtube";
	case Platform::Kick:
		return "kick";
	case Platform::Twitch:
		return "twitch";
	case Platform::Facebook:
		return "facebook";
	case Platform::TikTok:
		return "tiktok";
	case Platform::X:
		return "x";
	case Platform::Instagram:
		return "instagram";
	}
	return "custom";
}

std::optional<Platform> platform_from_id(std::string_view id)
{
	for (const Platform platform : kAllPlatforms) {
		if (id == platform_id(platform)) {
			return platform;
		}
	}
	return std::nullopt;
}

Platform detect_platform(std::string_view server_url)
{
	const std::string host = lowercase_host(server_url);
	for (const KnownHost &known : kKnownHosts) {
		if (host_is_under(host, known.domain)) {
			return known.platform;
		}
	}
	return Platform::Custom;
}

Platform effective_platform(std::optional<Platform> stored, std::string_view server_url)
{
	return stored ? *stored : detect_platform(server_url);
}

std::optional<std::string> preset_ingest_url(Platform platform)
{
	switch (platform) {
	case Platform::YouTube:
		return "rtmps://a.rtmps.youtube.com:443/live2";
	case Platform::Facebook:
		return "rtmps://rtmp-api.facebook.com:443/rtmp/";
	case Platform::Twitch:
		return "rtmp://live.twitch.tv/app";
	default:
		return std::nullopt;
	}
}

std::string url_after_picking(Platform platform, const std::string &current_url)
{
	if (const auto preset = preset_ingest_url(platform)) {
		return *preset;
	}
	for (const Platform other : kAllPlatforms) {
		if (preset_ingest_url(other) == current_url) {
			return {};
		}
	}
	return current_url;
}

} // namespace multistream
