#include "oauth/https_client.hpp"

#include <plugin-support.h>

#include <curl/curl.h>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace multistream {
namespace {

constexpr long kTimeoutSeconds = 20;
constexpr long kConnectTimeoutSeconds = 10;
// Twitch answers with small JSON documents; anything far larger is not one of them.
constexpr std::size_t kMaxResponseBytes = 1024 * 1024;

struct CurlDeleter {
	void operator()(CURL *handle) const { curl_easy_cleanup(handle); }
};

struct SlistDeleter {
	void operator()(curl_slist *list) const { curl_slist_free_all(list); }
};

std::size_t append_body(char *data, std::size_t size, std::size_t count, void *user)
{
	auto *body = static_cast<std::string *>(user);
	const std::size_t bytes = size * count;
	if (body->size() + bytes > kMaxResponseBytes) {
		return 0; // Makes libcurl fail the transfer with CURLE_WRITE_ERROR.
	}
	body->append(data, bytes);
	return bytes;
}

int check_cancelled(void *user, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	return static_cast<const std::atomic<bool> *>(user)->load() ? 1 : 0;
}

} // namespace

HttpsResponse perform_https_request(const HttpsRequest &request, const std::atomic<bool> &cancelled)
{
	HttpsResponse response;
	std::unique_ptr<CURL, CurlDeleter> curl(curl_easy_init());
	if (!curl) {
		response.error = "curl_easy_init failed";
		return response;
	}

	std::unique_ptr<curl_slist, SlistDeleter> headers;
	for (const std::string &line : request.headers) {
		curl_slist *appended = curl_slist_append(headers.get(), line.c_str());
		if (!appended) {
			response.error = "curl_slist_append failed";
			return response;
		}
		headers.release();
		headers.reset(appended);
	}

	const std::string user_agent = std::string("obs-multi-stream/") + PLUGIN_VERSION;
	char error_buffer[CURL_ERROR_SIZE] = {};
	std::string body;
	CURL *handle = curl.get();

	curl_easy_setopt(handle, CURLOPT_URL, request.url.c_str());
#if LIBCURL_VERSION_NUM >= 0x075500
	curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "https");
#endif
	curl_easy_setopt(handle, CURLOPT_USERAGENT, user_agent.c_str());
	curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers.get());
	curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, error_buffer);
	curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, append_body);
	curl_easy_setopt(handle, CURLOPT_WRITEDATA, &body);
	curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(handle, CURLOPT_TIMEOUT, kTimeoutSeconds);
	curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSeconds);
	curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, check_cancelled);
	curl_easy_setopt(handle, CURLOPT_XFERINFODATA, const_cast<std::atomic<bool> *>(&cancelled));
#if defined(_WIN32) && defined(CURLSSLOPT_REVOKE_BEST_EFFORT)
	// Same as OBS: Schannel fails closed when the revocation server is unreachable, which
	// corporate proxies often cause. The peer certificate itself is still verified.
	curl_easy_setopt(handle, CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_REVOKE_BEST_EFFORT));
#endif
	if (request.post_body) {
		curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.post_body->size()));
		curl_easy_setopt(handle, CURLOPT_POSTFIELDS, request.post_body->c_str());
	}

	const CURLcode code = curl_easy_perform(handle);
	if (code != CURLE_OK) {
		response.error = error_buffer[0] != '\0' ? error_buffer : curl_easy_strerror(code);
		return response;
	}
	curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response.status);
	response.body = std::move(body);
	return response;
}

} // namespace multistream
