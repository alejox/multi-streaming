// Which streaming service a destination goes to, for its icon and its editor preset.
//
// A host alone cannot always tell. Amazon IVS customers share *.global-contribute.live-video.net,
// Kick among them, so such a host reads as custom unless the user picked the service.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace multistream {

enum class Platform { Custom, YouTube, Kick, Twitch, Facebook, TikTok, X, Instagram };

inline constexpr std::array<Platform, 8> kAllPlatforms = {Platform::Custom, Platform::YouTube,  Platform::Kick,
							  Platform::Twitch, Platform::Facebook, Platform::TikTok,
							  Platform::X,      Platform::Instagram};

// The name stored in the config, which is also part of its icon's and text's names.
const char *platform_id(Platform platform);
std::optional<Platform> platform_from_id(std::string_view id);

Platform detect_platform(std::string_view server_url);

// What the user picked, else what the URL shows.
Platform effective_platform(std::optional<Platform> stored, std::string_view server_url);

// The ingest URL that OBS 33.0.1's own service list gives every user of the service, if
// there is one. Others hand out per-user or regional URLs, which the user has to paste.
std::optional<std::string> preset_ingest_url(Platform platform);

// The URL field after picking a service: its preset if it has one. Without one, a URL the
// user pasted stays, and a preset left from the previous choice goes.
std::string url_after_picking(Platform platform, const std::string &current_url);

} // namespace multistream
