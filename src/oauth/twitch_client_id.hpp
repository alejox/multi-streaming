// Twitch OAuth application identifiers. The Client ID is not a secret: it appears in the
// authorize URL the browser opens. Replace the placeholder with the real value from
// dev.twitch.tv/console; the OAuth Redirect URL there must be kTwitchRedirectUri exactly.
#pragma once

#include <string_view>

namespace multistream {

inline constexpr std::string_view kTwitchClientId = "REPLACE_ME";
inline constexpr std::string_view kTwitchRedirectUri = "http://localhost:8090/";

} // namespace multistream
