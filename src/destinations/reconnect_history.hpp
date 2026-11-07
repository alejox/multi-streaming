// What a destination's connection has been doing lately: which retry it is on, and whether
// the server keeps dropping it. A destination that reconnects fine every few seconds looks
// live almost all the time, so this is the only sign of that problem. Seen for real: 42
// drops, about 8 s apart.
#pragma once

#include <deque>

namespace multistream {

class ReconnectHistory {
public:
	// Three drops within two minutes: rare enough for one bad network moment not to count.
	static constexpr int kFlappingDrops = 3;
	static constexpr double kFlappingWindowSeconds = 120.0;

	// libobs scheduled a retry. The first since the last connection is a drop.
	void retry_scheduled(double now_seconds);
	void connected();
	void reset();

	// The current retry, counted from 1. 0 while connected.
	int attempt() const { return attempt_; }

	// Drops within the last kFlappingWindowSeconds.
	int recent_drops(double now_seconds) const;
	bool flapping(double now_seconds) const;

private:
	std::deque<double> drops_;
	int attempt_ = 0;
};

} // namespace multistream
