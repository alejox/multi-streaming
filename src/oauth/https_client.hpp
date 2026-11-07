// Blocking HTTPS requests over libcurl, the same HTTP stack OBS and its first-party plugins
// use. Call it from a worker thread: it blocks until the response, a timeout, or `cancelled`.
#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <vector>

namespace multistream {

struct HttpsRequest {
	std::string url;
	// Complete header lines, such as "Authorization: Bearer <token>".
	std::vector<std::string> headers;
	// Sent as the POST body when set; the request is a GET otherwise.
	std::optional<std::string> post_body;
};

struct HttpsResponse {
	// Set when the server answered, whatever the HTTP status.
	std::optional<std::string> body;
	long status = 0;
	// Why no answer arrived (transport failure, timeout, cancellation); for logs only.
	std::string error;
};

// Verifies the server certificate with the platform's normal trust store. A transfer that
// sees `cancelled` turn true stops within about a second.
HttpsResponse perform_https_request(const HttpsRequest &request, const std::atomic<bool> &cancelled);

} // namespace multistream
