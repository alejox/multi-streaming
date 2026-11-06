// The seam through which the engine gets video for a destination.
#pragma once

#include "destinations/destination.hpp"

#include <obs.h>

#include <memory>

namespace multistream {

class VideoSources {
public:
	virtual ~VideoSources() = default;

	// Returns nothing when that source has no video right now. Called on the UI thread.
	virtual video_t *video_for(VideoSource source) = 0;

	// Keeps that source's video alive for whoever holds the result, so that an encoder
	// bound to it can finish after the source itself has moved on.
	virtual std::shared_ptr<void> hold(VideoSource source) = 0;
};

} // namespace multistream
