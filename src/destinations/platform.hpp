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

// The ingest URL to prefill after picking a service, when every user of that service can
// use it. Twitch's default is its classic universal endpoint; its regional servers are
// per-region. Others hand out per-user URLs, which the user has to paste.
std::optional<std::string> preset_ingest_url(Platform platform);

// The service's public dashboard page that shows the user's stream key, for services whose
// page has a stable address that works for every account.
std::optional<std::string> stream_key_page_url(Platform platform);

// The URL field after picking a service: its preset if it has one. Without one, a URL the
// user pasted stays, and a preset left from the previous choice goes.
std::string url_after_picking(Platform platform, const std::string &current_url);

} // namespace multistream
