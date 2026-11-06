#include "destinations/encoder_leases.hpp"

namespace multistream {

bool EncoderLeases::acquire(const std::string &key)
{
	return ++counts_[key] == 1;
}

bool EncoderLeases::release(const std::string &key)
{
	const auto found = counts_.find(key);
	if (found == counts_.end()) {
		return false;
	}
	if (--found->second > 0) {
		return false;
	}
	counts_.erase(found);
	return true;
}

int EncoderLeases::count(const std::string &key) const
{
	const auto found = counts_.find(key);
	return found == counts_.end() ? 0 : found->second;
}

} // namespace multistream
