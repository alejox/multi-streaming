#include "destinations/destination.hpp"

#include <random>

namespace multistream {

std::string generate_destination_id()
{
	static constexpr char kHexDigits[] = "0123456789abcdef";

	std::random_device random;
	std::uniform_int_distribution<int> digit(0, 15);

	std::string id(32, '0');
	for (char &character : id) {
		character = kHexDigits[digit(random)];
	}
	return id;
}

} // namespace multistream
