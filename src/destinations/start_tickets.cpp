#include "destinations/start_tickets.hpp"

namespace multistream {

std::uint64_t StartTickets::issue(const std::string &destination_id)
{
	current_[destination_id] = ++last_;
	return last_;
}

void StartTickets::cancel(const std::string &destination_id)
{
	current_.erase(destination_id);
}

void StartTickets::cancel_all()
{
	current_.clear();
}

bool StartTickets::redeem(const std::string &destination_id, std::uint64_t ticket)
{
	const auto found = current_.find(destination_id);
	if (found == current_.end() || found->second != ticket) {
		return false;
	}
	current_.erase(found);
	return true;
}

bool StartTickets::pending(const std::string &destination_id) const
{
	return current_.count(destination_id) > 0;
}

} // namespace multistream
