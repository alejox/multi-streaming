// Blocking HTTPS download into a file over libcurl, following the redirect GitHub uses for
// release assets. Call it from a worker thread.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

namespace multistream {

struct DownloadResult {
	bool ok = false;
	std::uint64_t bytes = 0;
	// Why it failed; for logs only.
	std::string error;
};

// Writes the body to `destination`, replacing it. Fails without keeping more than
// `max_bytes`, on an HTTP error status, or once `cancelled` turns true (within about a second).
DownloadResult download_to_file(const std::string &url, const std::filesystem::path &destination,
				std::uint64_t max_bytes, const std::atomic<bool> &cancelled);

} // namespace multistream
