#include "vertical_canvas/twitch_dual_format.hpp"

namespace multistream {

TwitchDualFormat twitch_dual_format(const TwitchDualFormatFacts &facts)
{
	if (!facts.service_is_twitch) {
		return TwitchDualFormat::NotTwitch;
	}
	const bool ours = !facts.vertical_canvas_uuid.empty() &&
			  facts.additional_canvas_uuid == facts.vertical_canvas_uuid;
	if (!facts.enhanced_broadcasting || !ours) {
		return TwitchDualFormat::Off;
	}
	return facts.streaming ? TwitchDualFormat::Live : TwitchDualFormat::Ready;
}

} // namespace multistream
