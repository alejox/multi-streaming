#include "oauth/sha256.hpp"

#include <cstring>

namespace multistream {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants = {{
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
}};

constexpr std::array<std::uint32_t, 8> kInitialHash = {
	0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

constexpr std::uint32_t rotate_right(std::uint32_t value, int bits)
{
	return (value >> bits) | (value << (32 - bits));
}

} // namespace

std::array<std::uint8_t, 32> sha256_digest(std::string_view data)
{
	std::array<std::uint32_t, 8> hash = kInitialHash;

	const auto process_block = [&](const std::uint8_t *block) {
		std::array<std::uint32_t, 64> schedule{};
		for (int i = 0; i < 16; ++i) {
			schedule[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
				      (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
				      (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
				      static_cast<std::uint32_t>(block[i * 4 + 3]);
		}
		for (int i = 16; i < 64; ++i) {
			const std::uint32_t small0 = rotate_right(schedule[i - 15], 7) ^
						     rotate_right(schedule[i - 15], 18) ^ (schedule[i - 15] >> 3);
			const std::uint32_t small1 = rotate_right(schedule[i - 2], 17) ^
						     rotate_right(schedule[i - 2], 19) ^ (schedule[i - 2] >> 10);
			schedule[i] = schedule[i - 16] + small0 + schedule[i - 7] + small1;
		}

		std::uint32_t a = hash[0];
		std::uint32_t b = hash[1];
		std::uint32_t c = hash[2];
		std::uint32_t d = hash[3];
		std::uint32_t e = hash[4];
		std::uint32_t f = hash[5];
		std::uint32_t g = hash[6];
		std::uint32_t h = hash[7];

		for (int i = 0; i < 64; ++i) {
			const std::uint32_t big1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
			const std::uint32_t choose = (e & f) ^ (~e & g);
			const std::uint32_t temp1 = h + big1 + choose + kRoundConstants[i] + schedule[i];
			const std::uint32_t big0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
			const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
			const std::uint32_t temp2 = big0 + majority;

			h = g;
			g = f;
			f = e;
			e = d + temp1;
			d = c;
			c = b;
			b = a;
			a = temp1 + temp2;
		}

		hash[0] += a;
		hash[1] += b;
		hash[2] += c;
		hash[3] += d;
		hash[4] += e;
		hash[5] += f;
		hash[6] += g;
		hash[7] += h;
	};

	const std::uint8_t *bytes = reinterpret_cast<const std::uint8_t *>(data.data());
	std::size_t remaining = data.size();
	while (remaining >= 64) {
		process_block(bytes);
		bytes += 64;
		remaining -= 64;
	}

	// Append 0x80, zero-pad to 56 mod 64, then the 64-bit big-endian bit length.
	std::array<std::uint8_t, 128> padded{};
	std::size_t padded_size = 0;
	if (remaining > 0) {
		std::memcpy(padded.data(), bytes, remaining);
		padded_size = remaining;
	}
	padded[padded_size++] = 0x80;

	if (padded_size > 56) {
		std::memset(padded.data() + padded_size, 0, 64 - padded_size);
		process_block(padded.data());
		std::memset(padded.data(), 0, 56);
		padded_size = 56;
	} else {
		std::memset(padded.data() + padded_size, 0, 56 - padded_size);
		padded_size = 56;
	}

	const std::uint64_t bit_length = static_cast<std::uint64_t>(data.size()) * 8;
	for (int i = 0; i < 8; ++i) {
		padded[padded_size + i] = static_cast<std::uint8_t>(bit_length >> (56 - i * 8));
	}
	process_block(padded.data());

	std::array<std::uint8_t, 32> digest{};
	for (int i = 0; i < 8; ++i) {
		digest[i * 4] = static_cast<std::uint8_t>(hash[i] >> 24);
		digest[i * 4 + 1] = static_cast<std::uint8_t>(hash[i] >> 16);
		digest[i * 4 + 2] = static_cast<std::uint8_t>(hash[i] >> 8);
		digest[i * 4 + 3] = static_cast<std::uint8_t>(hash[i]);
	}
	return digest;
}

std::string sha256_hex(std::string_view data)
{
	static constexpr char kHex[] = "0123456789abcdef";
	const std::array<std::uint8_t, 32> digest = sha256_digest(data);
	std::string out;
	out.reserve(64);
	for (const std::uint8_t byte : digest) {
		out += kHex[byte >> 4];
		out += kHex[byte & 0x0f];
	}
	return out;
}

} // namespace multistream
