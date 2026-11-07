#include "oauth/base64url.hpp"

#include <cstdint>

namespace multistream {
namespace {

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

} // namespace

std::string base64url_encode(std::string_view data)
{
	std::string out;
	out.reserve((data.size() + 2) / 3 * 4);

	const std::uint8_t *bytes = reinterpret_cast<const std::uint8_t *>(data.data());
	std::size_t i = 0;
	for (; i + 3 <= data.size(); i += 3) {
		const std::uint32_t block = (static_cast<std::uint32_t>(bytes[i]) << 16) |
					    (static_cast<std::uint32_t>(bytes[i + 1]) << 8) |
					    static_cast<std::uint32_t>(bytes[i + 2]);
		out += kAlphabet[(block >> 18) & 0x3f];
		out += kAlphabet[(block >> 12) & 0x3f];
		out += kAlphabet[(block >> 6) & 0x3f];
		out += kAlphabet[block & 0x3f];
	}

	const std::size_t rest = data.size() - i;
	if (rest == 1) {
		const std::uint32_t block = static_cast<std::uint32_t>(bytes[i]) << 16;
		out += kAlphabet[(block >> 18) & 0x3f];
		out += kAlphabet[(block >> 12) & 0x3f];
	} else if (rest == 2) {
		const std::uint32_t block = (static_cast<std::uint32_t>(bytes[i]) << 16) |
					    (static_cast<std::uint32_t>(bytes[i + 1]) << 8);
		out += kAlphabet[(block >> 18) & 0x3f];
		out += kAlphabet[(block >> 12) & 0x3f];
		out += kAlphabet[(block >> 6) & 0x3f];
	}
	return out;
}

} // namespace multistream
