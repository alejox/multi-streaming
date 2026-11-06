// The seam through which the engine gets video for a destination. The vertical canvas
// plugs in here, without changes to the engine.
#pragma once

#include "destinations/destination.hpp"

#include <obs.h>

namespace multistream {

class VideoSources {
public:
	virtual ~VideoSources() = default;

	// Returns nothing when that source has no video right now. Called on the UI thread.
	virtual video_t *video_for(VideoSource source) = 0;
};

class MainMixVideoSources final : public VideoSources {
public:
	video_t *video_for(VideoSource source) override
	{
		return source == VideoSource::Main ? obs_get_video() : nullptr;
	}
};

} // namespace multistream
