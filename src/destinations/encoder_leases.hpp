#pragma once

#include <map>
#include <string>

namespace multistream {

// Counts which destinations use a shared encoder, so that an encoder is released only
// when the last one is gone. Holds no encoders: the caller owns what the keys name.
class EncoderLeases {
public:
	// Returns true when this is the first lease on the key: the caller creates the encoder.
	bool acquire(const std::string &key);

	// Returns true when this was the last lease on the key: the caller releases the
	// encoder. Releasing a key without leases does nothing and returns false.
	bool release(const std::string &key);

	int count(const std::string &key) const;

private:
	std::map<std::string, int> counts_;
};

} // namespace multistream
