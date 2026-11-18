// Twitch Dual Format: OBS's own Twitch stream, with Enhanced Broadcasting on, carries a
// second, vertical track from an additional canvas. Phones held upright get that track.
// This reads OBS's stream settings to tell the user whether this plugin's canvas is that
// additional canvas, and whether it is on air. No OBS or Qt here.
#pragma once

#include <string>

namespace multistream {

struct TwitchDualFormatFacts {
	bool service_is_twitch = false;
	bool enhanced_broadcasting = false;
	// The canvas OBS sends as the additional track, by uuid; empty when none.
	std::string additional_canvas_uuid;
	std::string vertical_canvas_uuid;
	bool streaming = false;
};

enum class TwitchDualFormat {
	NotTwitch, // OBS streams somewhere else: nothing to say
	Off,       // Twitch, but this canvas is not the additional canvas
	Ready,     // goes out with OBS's Start Streaming
	Live,      // on air in vertical on Twitch
};

TwitchDualFormat twitch_dual_format(const TwitchDualFormatFacts &facts);

} // namespace multistream
