// Decides whether a start that was waiting for its stream key may still go on. Reading a
// key can take as long as a Keychain prompt stays open, and a stop or teardown can arrive
// meanwhile. Whichever read finishes, only the latest ticket for a destination counts.
#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace multistream {

class StartTickets {
public:
	// Replaces any earlier ticket for this destination.
	std::uint64_t issue(const std::string &destination_id);

	void cancel(const std::string &destination_id);
	void cancel_all();

	// True once, for the destination's current ticket.
	bool redeem(const std::string &destination_id, std::uint64_t ticket);

	bool pending(const std::string &destination_id) const;

private:
	std::map<std::string, std::uint64_t> current_;
	std::uint64_t last_ = 0;
};

} // namespace multistream
