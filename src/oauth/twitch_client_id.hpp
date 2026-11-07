// Twitch OAuth application identifiers. The Client ID is not a secret: it appears in the
// authorize URL the browser opens. Replace the placeholder with the real value from
// dev.twitch.tv/console.
#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace multistream {

inline constexpr std::string_view kTwitchClientId = "53k1chs97qk6taty3vxhr0zzg2n4kh";

// Redirect URIs in the order the plugin tries to bind them. Every one must also be
// registered in the Twitch app at dev.twitch.tv/console, or the OAuth redirect will not
// come back to that port.
inline constexpr std::array<std::string_view, 5> kTwitchRedirectUris = {{
	"http://localhost:8090/",
	"http://localhost:8091/",
	"http://localhost:8092/",
	"http://localhost:8093/",
	"http://localhost:8094/",
}};

inline std::vector<std::string> twitch_redirect_uris()
{
	std::vector<std::string> uris;
	uris.reserve(kTwitchRedirectUris.size());
	for (const std::string_view uri : kTwitchRedirectUris) {
		uris.emplace_back(uri);
	}
	return uris;
}

} // namespace multistream
